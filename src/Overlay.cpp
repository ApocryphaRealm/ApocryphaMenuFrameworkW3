#include "Overlay.h"

#include "DevBenchTool.h"
#include "Gfx.h"
#include "HdrComposite.h"
#include "Input.h"
#include "PresentHooks.h"
#include "Preview3D.h"
#include "Renderer.h"
#include "Settings.h"

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <wincodec.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <shlobj.h>

namespace
{
	// ---- state ----------------------------------------------------------------------------------------------
	ID3D12CommandQueue*        g_queue = nullptr;         // the presenting queue, captured at swap-chain creation
	IDXGISwapChain*            g_swapChain = nullptr;     // the swap chain we initialised for
	ID3D12Device*              g_device = nullptr;
	ID3D12DescriptorHeap*      g_rtvHeap = nullptr;
	ID3D12DescriptorHeap*      g_srvHeap = nullptr;
	ID3D12GraphicsCommandList* g_list = nullptr;
	ID3D12Fence*               g_fence = nullptr;
	HANDLE                     g_fenceEvent = nullptr;
	std::atomic<UINT64>        g_fenceValue{ 0 };
	HWND                       g_hwnd = nullptr;
	WNDPROC                    g_origWndProc = nullptr;
	DXGI_FORMAT                g_format = DXGI_FORMAT_R8G8B8A8_UNORM;
	UINT                       g_width = 0, g_height = 0;
	std::atomic_bool           g_ready{ false };
	bool                       g_failed = false;
	bool                       g_backendUp = false;
	thread_local bool          t_inPresent = false;
	thread_local bool          t_inDxgiPresent = false;   // uDrawPath=1: a dxgi Present nested inside ours does not draw twice
	std::recursive_mutex       g_gpuLock;                 // one submitter at a time on our fence and the upload list

	// Two levels of hook (owner, 2026-10-05: Steam's own F12 screenshot must show the AMF window).
	// - GAME-FACING: vtable slots of the objects the game itself holds. On Witcher 3 5.0 those are NVIDIA Streamline's
	//   proxies (sl.interposer.dll). A slot write runs before the call reaches any code, so AMF draws before Streamline,
	//   before dxgi and before every inline hook on dxgi's Present - Steam's overlay, whose screenshot reads the back buffer
	//   as its hook is entered. Hooked last-in-first-out at dxgi level, AMF could be inside Steam's hook (Oblivion's miss).
	// - DXGI-LEVEL: MinHook on System32 dxgi's own functions. Captures the real command queue and swap chain, handles
	//   ResizeBuffers, and draws ONLY until the game-facing Present has drawn (a fallback if the slots are never reached) -
	//   or, with frame generation, for good: its own thread presents the dxgi swap chain (g_foreignPresenter below).
	std::atomic_bool                 g_outerDraws{ false };      // the game-facing Present has drawn; dxgi-level stops drawing
	std::atomic<IDXGISwapChain*>     g_outerSwapChain{ nullptr }; // the swap chain the game holds (Streamline's proxy)
	std::atomic<IDXGISwapChain*>     g_innerSwapChain{ nullptr }; // the dxgi swap chain created inside its creation
	thread_local IDXGISwapChain*     t_created = nullptr;         // set by the dxgi-level creation hooks, read around them
	// A PRESENTER OF ITS OWN (1.0.5): if the inner dxgi swap chain is ever presented from outside the game-facing Present
	// (a thread that does not go through the proxy AMF hooks), drawing into it from the game-facing path would put two
	// threads on one swap chain. Once that is seen, AMF draws at dxgi's Present on that thread only. On Witcher 3 with AMD
	// FSR frame generation this does NOT fire: frame generation's own thread presents through the Streamline proxy, so the
	// game-facing hook already runs on that thread (checked 2026-10-05 - the 19:44 crash there is not explained by this).
	std::atomic_bool                 g_foreignPresenter{ false };
	std::mutex                       g_renderLock;                // one Render at a time, whichever thread presents
	bool                             g_gameUsesDxgi = false;      // the exe takes CreateDXGIFactory* straight from dxgi

	// WHICH PATH DRAWS (1.0.6, a Nexus report: with ReShade, F1 did nothing and no Present hook ever fired). Read by the log,
	// the 15-second no-Present warning and DevBench (op=state "present").
	thread_local bool                t_probe = false;             // the start-up probe's own calls pass every hook untouched
	thread_local int                 t_presentDepth = 0;          // hooked Presents this thread is inside
	thread_local const char*         t_drawnBy = nullptr;         // the path that drew this frame on this thread
	thread_local const std::string*  t_drawnModule = nullptr;
	std::atomic_bool                 g_anyPresent{ false };       // a Present has reached one of AMF's hooks
	std::atomic<const char*>         g_drawPath{ nullptr };       // the last path that drew (a literal below)
	std::atomic<const std::string*>  g_drawModule{ nullptr };     // the module of its hooked function (an entry's, stable)
	std::atomic<const char*>         g_queueFrom{ "none" };       // where g_queue came from
	std::mutex                       g_probeLock;
	std::string                      g_probeSummary = "not run";
	std::atomic<ULONGLONG>           g_installTick{ 0 };
	std::atomic<ULONGLONG>           g_firstCreateTick{ 0 };      // the first swap-chain creation AMF saw
	std::vector<std::string>         g_exportHooks;               // written by Install before the start-up thread exists
	// The queue fallback (a swap chain whose creation was not seen): ExecuteCommandLists is hooked only then, and notes the
	// last DIRECT queue each thread submitted on while g_wantQueue holds.
	thread_local ID3D12CommandQueue* t_lastDirectQueue = nullptr;
	std::atomic_bool                 g_wantQueue{ false };
	int                              g_queueWaitFrames = 0;

	// Descriptor 0 is the font atlas (ImGui's DX12 backend owns it); 1..N are textures handed out below.
	constexpr UINT             kSrvCount = 512;
	UINT                       g_srvStep = 0;
	std::vector<UINT>          g_freeSlots;
	struct Texture
	{
		ID3D12Resource* resource = nullptr;
		UINT            slot = 0;
	};
	std::unordered_map<std::uint64_t, Texture> g_textures;   // GPU handle ptr -> texture

	ID3D12CommandAllocator*    g_uploadAllocator = nullptr;
	ID3D12GraphicsCommandList* g_uploadList = nullptr;

	// HDR (HdrComposite.h, 1.0.2): the colour space the game set on its swap chain (-1 = never seen), the game's own HDR
	// switch and paper white from Documents\The Witcher 3\dx12user.settings (re-read every few seconds), what was decided
	std::atomic<int>   g_colorSpace{ -1 };
	std::atomic<int>   g_gameHdr{ -1 };
	std::atomic<float> g_paperWhite{ 200.0f };
	int                g_hdrLogged = -1;
	bool               g_hdrFailed = false;
	void               ReadGameHdr();   // below, beside the colour-space hook

	// DEFERRED RELEASE (1.0.2, Item Explorer's 3D card run 2026-10-06: Close freed the preview's target in the same frame
	// whose draw list still showed it, the GPU read a destroyed resource and the driver's crash reporter came up). Nothing
	// a frame may still use is freed at once: it waits here, tagged with the fence of the first frame submitted after it
	// was handed over, and is freed once that fence has passed. Guarded by g_gpuLock.
	struct Deferred
	{
		UINT64                fence = 0;   // 0 = no frame submitted since
		std::function<void()> free;
	};
	std::vector<Deferred> g_deferred;

	// g_gpuLock held. a_tag: the fence the untagged entries wait for; then everything whose fence has passed is freed.
	void RunDeferred(UINT64 a_tag)
	{
		for (auto& d : g_deferred) {
			if (d.fence == 0) d.fence = a_tag;
		}
		const UINT64 done = g_fence ? g_fence->GetCompletedValue() : 0;
		for (auto it = g_deferred.begin(); it != g_deferred.end();) {
			if (it->fence != 0 && done >= it->fence) {
				it->free();
				it = g_deferred.erase(it);
			} else {
				++it;
			}
		}
	}

	struct Frame
	{
		ID3D12CommandAllocator*     allocator = nullptr;
		ID3D12Resource*             backBuffer = nullptr;
		D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
		UINT64                      fence = 0;
	};
	std::vector<Frame> g_frames;

	// ---- originals ------------------------------------------------------------------------------------------
	using CreateFactory_t = HRESULT(WINAPI*)(REFIID, void**);
	using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);
	using CreateSwapChain_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
	using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	using SetColorSpace1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);
	using ExecuteCommandLists_t = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

	// the DXGI factory exports (System32's dxgi.dll, and the module the exe takes them from): one function each
	CreateFactory_t          o_CreateDXGIFactory = nullptr;
	CreateFactory_t          o_CreateDXGIFactory1 = nullptr;
	CreateFactory2_t         o_CreateDXGIFactory2 = nullptr;
	CreateFactory_t          o_OuterCreateDXGIFactory = nullptr;
	CreateFactory_t          o_OuterCreateDXGIFactory1 = nullptr;
	CreateFactory2_t         o_OuterCreateDXGIFactory2 = nullptr;
	// The vtable hooks keep one original PER CLASS (PresentHooks.h): the sets are defined with the detours, below.

	template <class T>
	void Release(T*& a_p)
	{
		if (a_p) {
			a_p->Release();
			a_p = nullptr;
		}
	}

	// The file name of the module a code address lives in - which Present the game really calls is read from this.
	using presenthooks::ModuleOf;

	void* SlotOf(void* a_object, std::size_t a_index)
	{
		auto** vtbl = a_object ? *reinterpret_cast<void***>(a_object) : nullptr;
		return vtbl ? vtbl[a_index] : nullptr;
	}

	// ---- GPU sync -------------------------------------------------------------------------------------------------
	void WaitFor(UINT64 a_value, DWORD a_ms)
	{
		if (g_fence && g_fence->GetCompletedValue() < a_value) {
			g_fence->SetEventOnCompletion(a_value, g_fenceEvent);
			WaitForSingleObject(g_fenceEvent, a_ms);
		}
	}

	void WaitIdle()
	{
		std::scoped_lock l(g_gpuLock);
		if (g_queue && g_fence && g_fenceEvent) {
			const UINT64 v = ++g_fenceValue;
			if (SUCCEEDED(g_queue->Signal(g_fence, v))) {
				WaitFor(v, 2000);
			}
		}
	}

	// ---- render resources -----------------------------------------------------------------------------------------
	// THE BACK BUFFERS ARE NEVER HELD BETWEEN FRAMES (1.0.5, 2026-10-05). Each frame takes the buffer it draws into
	// (GetBuffer), and lets go of it once its commands are submitted. Holding them, as before, kept the game's swap
	// chain alive after the game released it: turning on FSR frame generation or changing the anti-aliasing makes the
	// game replace its swap chain on the same window, Windows allows one flip-model swap chain per window, the new one
	// was refused, and the game crashed on the swap chain it never got (witcher3.exe+0x1EC0228, the owner's crash).
	constexpr UINT kRtvSlots = 16;   // RTV descriptors: one per back buffer, room for a swap chain with more buffers

	// Sizes the per-frame list to a_count buffers (allocators made for new ones). False only when D3D12 refuses.
	bool EnsureFrames(UINT a_count)
	{
		if (a_count == 0 || a_count > kRtvSlots) {
			logger::error("swap chain has {} buffers - AMF draws for 1 to {}", a_count, kRtvSlots);
			return false;
		}
		const UINT step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		const std::size_t had = g_frames.size();
		g_frames.resize(std::max<std::size_t>(had, a_count));
		for (UINT i = 0; i < g_frames.size(); ++i) {
			auto& f = g_frames[i];
			f.rtv.ptr = h.ptr + static_cast<SIZE_T>(i) * step;
			if (!f.allocator && FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)))) {
				logger::error("command allocator {} failed", i);
				return false;
			}
		}
		if (g_frames.size() != had) {
			logger::debug("frames: {} -> {} per-frame allocators", had, g_frames.size());
		}
		return true;
	}

	LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wp, LPARAM a_lp);
	bool             WatchQueues(IDXGISwapChain* a_swapChain);   // with the detours, below

	bool Init(IDXGISwapChain* a_swapChain)
	{
		if (g_ready || g_failed) {
			return g_ready;
		}
		if (!g_queue && a_swapChain) {
			// The swap chain's creation was not seen (made through a path AMF's creation hooks never reached - a wrapper, a
			// trampoline captured before AMF hooked, or before AMF loaded), so no queue was captured with it. Asked first:
			// GetDevice(ID3D12CommandQueue) on the swap chain; then the last DIRECT queue this presenting thread submitted on,
			// if it is on the swap chain's device (QueueForSwapChain). The second needs ExecuteCommandLists watched, which
			// starts here and gives an answer from a following frame.
			const char* from = nullptr;
			if (ID3D12CommandQueue* q = presenthooks::QueueForSwapChain(a_swapChain, t_lastDirectQueue, &from)) {
				g_queue = q;   // keep the reference it gave us
				g_queueFrom = from;
				g_wantQueue = false;
				logger::info("queue taken from {}: {} on swap chain {} - its creation was not seen by AMF's hooks", from, static_cast<void*>(q),
					static_cast<void*>(a_swapChain));
			} else if (!g_wantQueue.exchange(true)) {
				if (!WatchQueues(a_swapChain)) {
					logger::error("swap chain {}: no command queue - its creation was not seen, GetDevice gives none and ExecuteCommandLists "
								  "could not be watched; the overlay stays off",
						static_cast<void*>(a_swapChain));
					g_failed = true;
					return false;
				}
				g_queueWaitFrames = 0;
				return false;   // the next frames tell which queue this thread presents from
			} else if (++g_queueWaitFrames < 600) {
				return false;
			}
		}
		if (!g_queue) {
			logger::error("no command queue was captured at swap-chain creation; the overlay stays off");
			g_failed = true;
			return false;
		}
		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(a_swapChain->GetDesc(&desc)) || FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&g_device)))) {
			logger::error("swap chain: GetDesc/GetDevice failed");
			g_failed = true;
			return false;
		}
		g_hwnd = desc.OutputWindow;
		g_format = desc.BufferDesc.Format;
		g_width = desc.BufferDesc.Width;
		g_height = desc.BufferDesc.Height;
		logger::info("swap chain {}: {}x{}, {} buffers, format {}, window {}", static_cast<void*>(a_swapChain),
			desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferCount, static_cast<int>(g_format),
			static_cast<void*>(g_hwnd));

		D3D12_DESCRIPTOR_HEAP_DESC rtv{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvSlots, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
		D3D12_DESCRIPTOR_HEAP_DESC srv{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
		if (FAILED(g_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtvHeap))) ||
			FAILED(g_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srvHeap)))) {
			logger::error("descriptor heaps failed");
			g_failed = true;
			return false;
		}
		g_srvStep = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		for (UINT i = kSrvCount - 1; i >= 1; --i) {
			g_freeSlots.push_back(i);
		}
		if (!EnsureFrames(desc.BufferCount)) {
			g_failed = true;
			return false;
		}
		if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr, IID_PPV_ARGS(&g_list))) ||
			FAILED(g_list->Close()) ||
			FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) {
			logger::error("command list / fence failed");
			g_failed = true;
			return false;
		}
		g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		g_swapChain = a_swapChain;
		g_ready = true;   // gfx:: is usable from here on - the renderer uploads its textures during OnDeviceReady

		if (!renderer::OnDeviceReady(g_hwnd, g_width, g_height)) {
			logger::error("the renderer did not come up; the overlay stays off");
			g_ready = false;
			g_failed = true;
			return false;
		}
		g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
		// Every OBSE plugin has loaded by the first Present, so TestBench (which sorts after us) is there to register with.
		devbenchtool::Init(true);
		logger::info("overlay ready (Dear ImGui {}, {} frames in flight, {} texture descriptors)", IMGUI_VERSION, g_frames.size(), kSrvCount - 1);
		ReadGameHdr();
		std::thread([] {
			while (true) {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				ReadGameHdr();
			}
		}).detach();
		return true;
	}

	// THE GAME REPLACED ITS SWAP CHAIN (frame generation on or off, an anti-aliasing / upscaler change, a display-mode
	// change): move the overlay onto the new one instead of drawing for the old one forever. Same device and queue; new
	// size, buffer count and, if it changed, format (the ImGui DX12 pipeline is built for one render-target format).
	bool Rebind(IDXGISwapChain* a_swapChain)
	{
		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(a_swapChain->GetDesc(&desc))) {
			logger::warn("swap chain {}: GetDesc failed - the overlay stays on {}", static_cast<void*>(a_swapChain), static_cast<void*>(g_swapChain));
			return false;
		}
		ID3D12Device* device = nullptr;
		if (FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&device))) || device != g_device) {
			logger::warn("swap chain {} is on another D3D12 device - the overlay does not follow it", static_cast<void*>(a_swapChain));
			Release(device);
			return false;
		}
		device->Release();
		WaitIdle();   // nothing of ours still in flight on the old buffers
		std::scoped_lock l(g_gpuLock);
		if (!EnsureFrames(desc.BufferCount)) {
			return false;
		}
		const DXGI_FORMAT oldFormat = g_format;
		logger::info("swap chain changed: {} -> {} ({}x{}, {} buffers, format {}{}) - the overlay follows it", static_cast<void*>(g_swapChain),
			static_cast<void*>(a_swapChain), desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferCount, static_cast<int>(desc.BufferDesc.Format),
			desc.OutputWindow != g_hwnd ? ", new window" : "");
		g_swapChain = a_swapChain;
		g_width = desc.BufferDesc.Width;
		g_height = desc.BufferDesc.Height;
		g_format = desc.BufferDesc.Format;
		if (desc.OutputWindow && desc.OutputWindow != g_hwnd) {
			if (g_hwnd && g_origWndProc) {
				SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
			}
			g_hwnd = desc.OutputWindow;
			g_origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WndProc)));
		}
		if (hdr::UiFormat(g_format) != hdr::UiFormat(oldFormat) && g_backendUp) {
			// the pipeline state names the render-target format: rebuild the backend for the new one (fonts follow at NewFrame)
			ImGui_ImplDX12_Shutdown();
			g_backendUp = ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frames.size()), hdr::UiFormat(g_format), g_srvHeap,
				g_srvHeap->GetCPUDescriptorHandleForHeapStart(), g_srvHeap->GetGPUDescriptorHandleForHeapStart());
			logger::info("swap chain format {} -> {}: ImGui's DX12 backend rebuilt ({})", static_cast<int>(oldFormat), static_cast<int>(g_format),
				g_backendUp ? "ok" : "FAILED");
		}
		return true;
	}

	// True when the frame belonged to the swap chain the overlay runs on (drawn, or nothing to draw this frame).
	bool Render(IDXGISwapChain* a_swapChain)
	{
		// Two presenting threads can meet here for a frame while frame generation is switched on or off; the second waits
		// for nothing and draws nothing (never two ImGui frames at once).
		std::unique_lock rl(g_renderLock, std::try_to_lock);
		if (!rl.owns_lock()) {
			return false;
		}
		if (!Init(a_swapChain)) {
			return false;
		}
		if (a_swapChain != g_swapChain && !Rebind(a_swapChain)) {
			return false;
		}

		// The whole frame: input, the framework window, every consumer window and HUD element, ImGui::Render.
		renderer::OnFrame();

		ImDrawData* dd = ImGui::GetDrawData();
		if (!dd || dd->CmdListsCount == 0) {
			// nothing on screen this frame (menu closed, no HUD element drew): nothing recorded - and nothing new holds what
			// waits to be freed, so it only waits for the frames already submitted
			std::scoped_lock dl(g_gpuLock);
			if (!g_deferred.empty()) RunDeferred(g_fenceValue.load());
			return true;
		}

		IDXGISwapChain3* sc3 = nullptr;
		if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc3)))) {
			return true;
		}
		const UINT idx = sc3->GetCurrentBackBufferIndex();
		sc3->Release();
		if (idx >= g_frames.size()) {
			return true;
		}
		ID3D12Resource* backBuffer = nullptr;   // this frame's only: released below, once the commands are submitted
		if (FAILED(a_swapChain->GetBuffer(idx, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
			static bool s_warned = false;
			if (!s_warned) {
				s_warned = true;
				logger::warn("GetBuffer({}) failed on swap chain {} - nothing drawn this frame", idx, static_cast<void*>(a_swapChain));
			}
			return true;
		}

		// HDR (HdrComposite.h): ImGui draws into an 8-bit intermediate, then one pass writes it to the back buffer in the
		// screen's own encoding. Only when the back buffer is not 8-bit; an 8-bit one is drawn straight into as before.
		const bool composite = hdr::NeedsComposite(g_format);
		hdr::Mode  hdrMode = hdr::Mode::kSdr;
		if (composite) {
			hdrMode = hdr::Decide(g_format, settings::Get().hdrMode, g_colorSpace.load(), g_gameHdr.load());
			if (static_cast<int>(hdrMode) != g_hdrLogged) {
				g_hdrLogged = static_cast<int>(hdrMode);
				logger::info("hdr: back buffer format {}, colour space {}, the game's HDR switch {}, uHdrMode {} -> the menu is written as {} "
							 "(paper white {:.0f} nits)",
					static_cast<int>(g_format), g_colorSpace.load(), g_gameHdr.load(), settings::Get().hdrMode, hdr::ModeName(hdrMode),
					g_paperWhite.load());
			}
			// the intermediate is the size of the buffer really drawn into (the 3D card run logged 2133x1200 while the game
			// ran at 3200x1800 - check, and say so once if the swap chain's description and the buffer ever disagree)
			const D3D12_RESOURCE_DESC bbd = backBuffer->GetDesc();
			const UINT                bbW = static_cast<UINT>(bbd.Width), bbH = bbd.Height;
			static bool s_sizeNoted = false;
			if ((bbW != g_width || bbH != g_height) && !s_sizeNoted) {
				s_sizeNoted = true;
				logger::warn("hdr: the back buffer is {}x{} while the swap chain reported {}x{} - the menu composite follows the buffer", bbW, bbH,
					g_width, g_height);
			}
			if (hdr::TargetSizeDiffers(bbW, bbH)) {
				WaitIdle();   // the old intermediate may still be read by a frame in flight
			}
			const bool ok = hdr::Init(g_device, g_format, [](D3D12_CPU_DESCRIPTOR_HANDLE& a_cpu, D3D12_GPU_DESCRIPTOR_HANDLE& a_gpu) {
				std::scoped_lock gl(g_gpuLock);
				if (g_freeSlots.empty()) return false;
				const UINT slot = g_freeSlots.back();
				g_freeSlots.pop_back();
				a_cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
				a_gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
				a_cpu.ptr += static_cast<SIZE_T>(slot) * g_srvStep;
				a_gpu.ptr += static_cast<UINT64>(slot) * g_srvStep;
				return true;
			}) && hdr::EnsureTarget(bbW, bbH);
			if (!ok) {
				if (!g_hdrFailed) {
					g_hdrFailed = true;
					logger::error("hdr: the menu composite could not be set up for back buffer format {} - nothing is drawn", static_cast<int>(g_format));
				}
				backBuffer->Release();
				return true;
			}
		}

		std::scoped_lock l(g_gpuLock);
		Frame& f = g_frames[idx];
		WaitFor(f.fence, 1000);
		f.allocator->Reset();
		g_list->Reset(f.allocator, nullptr);
		g_device->CreateRenderTargetView(backBuffer, nullptr, f.rtv);
		// a consumer's 3D preview (Item Explorer's item card): drawn into its own target first, read by ImGui below
		preview3d::Record(g_list);

		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = backBuffer;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (composite) {
			hdr::BeginUi(g_list);
			g_list->SetDescriptorHeaps(1, &g_srvHeap);
			ImGui_ImplDX12_RenderDrawData(dd, g_list);
			g_list->ResourceBarrier(1, &b);
			hdr::Composite(g_list, f.rtv, g_srvHeap, hdrMode, g_paperWhite.load());
		} else {
			g_list->ResourceBarrier(1, &b);
			g_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
			g_list->SetDescriptorHeaps(1, &g_srvHeap);
			ImGui_ImplDX12_RenderDrawData(dd, g_list);
		}
		std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
		g_list->ResourceBarrier(1, &b);
		g_list->Close();
		ID3D12CommandList* lists[] = { g_list };
		g_queue->ExecuteCommandLists(1, lists);
		f.fence = ++g_fenceValue;
		g_queue->Signal(g_fence, f.fence);
		// THE FIRST FRAMES DRAWN, in full (a Nexus report 2026-10-07: the menu opened and took input, the log said all was
		// well, and nothing showed - not even in Steam's screenshot). What was drawn, into which buffer of which swap chain,
		// on which queue and from which Present: enough to tell a draw that never lands from one something later covers.
		static int s_drawnLogged = 0;
		if (s_drawnLogged < 3) {
			++s_drawnLogged;
			const D3D12_RESOURCE_DESC   bd = backBuffer->GetDesc();
			const D3D12_COMMAND_QUEUE_DESC qd = g_queue->GetDesc();
			ID3D12Device* qdev = nullptr;
			g_queue->GetDevice(IID_PPV_ARGS(&qdev));
			logger::info("drawn frame {}: swap chain {} buffer {} ({}x{}, format {}), {} draw lists / {} vertices over {:.0f}x{:.0f} at "
						 "({:.0f},{:.0f}), {}; queue {} (type {}{}) on {}; thread {}, game-facing Present drew: {}, presenter of its own: {}",
				s_drawnLogged, static_cast<void*>(a_swapChain), idx, static_cast<UINT>(bd.Width), bd.Height, static_cast<int>(bd.Format),
				dd->CmdListsCount, dd->TotalVtxCount, dd->DisplaySize.x, dd->DisplaySize.y, dd->DisplayPos.x, dd->DisplayPos.y,
				composite ? std::string("HDR composite as ") + hdr::ModeName(hdrMode) : std::string("straight into the back buffer"),
				static_cast<void*>(g_queue), static_cast<int>(qd.Type), qdev == g_device ? "" : ", ANOTHER DEVICE than the swap chain's",
				ModuleOf(*reinterpret_cast<void**>(g_queue)), ::GetCurrentThreadId(), g_outerDraws.load() ? "yes" : "no",
				g_foreignPresenter.load() ? "yes" : "no");
			Release(qdev);
		}
		if (!g_deferred.empty()) RunDeferred(f.fence);   // what this frame may still show waits for this frame
		backBuffer->Release();   // the swap chain keeps it alive for the queued commands; we keep nothing
		return true;
	}

	// ---- detours ----------------------------------------------------------------------------------------------------
	// WHERE THE MENU IS DRAWN, AND WHY NEVER TWICE (1.0.6: every class of swap chain and factory is hooked, not only the first
	// one seen, and a start-up probe hooks dxgi's own Present body whatever path the game's swap chain was created on - so a
	// wrapper's Present and the real one it calls are now often BOTH hooked, nested on one thread).
	// Two thread-local guards decide it. t_inPresent: set by whichever hooked Present draws first in drawing path 0 (the
	// game-facing one, or the outermost hooked dxgi-level one); every hooked Present nested inside it only forwards.
	// t_inDxgiPresent: the same for uDrawPath=1, at dxgi level only (the game-facing Present never draws there). A dxgi-level
	// Present draws in path 0 only while the game-facing one has not (g_outerDraws false), or on frame generation's own
	// thread (ForeignPresenter: the inner swap chain presented outside the game-facing Present).
	// - No proxy (Streamline only): game -> Streamline's proxy slot (game-facing, draws into the inner dxgi swap chain, sets
	//   t_inPresent) -> Streamline -> dxgi's Present (inline hook; found by the creation hook and the probe: ONE entry, same
	//   target) -> t_inPresent: forwards. One draw. uDrawPath=1: the slot forwards, dxgi's Present draws. One draw.
	// - ReShade as bin\x64_dx12\dxgi.dll: Streamline loads System32's dxgi by full path, so unless ReShade also hooks that
	//   module's exports nothing changes from the case above. If it does, it wraps what Streamline gets - the next case.
	// - ReShade wrapping the swap chain returned to Streamline (dxgi.dll or d3d12.dll proxy, or injected): the chain is
	//   Streamline's proxy -> ReShade's DXGISwapChain::Present -> dxgi's Present. With the game-facing slot working, it draws
	//   (into whatever the dxgi-level creation hook recorded as the inner swap chain: dxgi's own, or ReShade's wrapper,
	//   which forwards GetBuffer) and both nested dxgi-level hooks forward on t_inPresent. Without it (the creation was not
	//   seen, so no slot was written): the OUTERMOST hooked dxgi-level Present draws (ReShade's class when the creation hook
	//   saw it, else dxgi's own via the probe), sets t_inPresent, the inner one forwards; with no creation to give the queue,
	//   Init finds it (QueueForSwapChain: GetDevice, else the presenting thread's DIRECT submissions). Always the same object,
	//   so Rebind never alternates. One draw.
	//   uDrawPath=1: the outermost hooked dxgi-level Present draws under t_inDxgiPresent, the inner one forwards. One draw.
	// - Frame generation on: its present thread goes through Streamline's proxy (game-facing hook runs on that thread, as in
	//   1.0.5). If it ever presents the inner dxgi swap chain itself, ForeignPresenter moves the drawing there and the
	//   game-facing Present stops; a wrapper class above it (not the inner swap chain itself) neither claims nor draws while
	//   g_outerDraws holds, so the inner dxgi hook draws once. uDrawPath=1 there: as above under t_inDxgiPresent.
	// - A game taking dxgi straight from dxgi.dll: the game-facing slots sit on dxgi's own vtable and forward to its body,
	//   which the dxgi-level hook MinHooks; a later probe or creation hook that reads our detour from that slot hooks what it
	//   forwards to instead (ResolveOwnDetour), never our own code. One draw.
	HRESULT OuterPresentBody(std::size_t, IDXGISwapChain*, UINT, UINT);
	HRESULT OuterPresent1Body(std::size_t, IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT PresentBody(std::size_t, IDXGISwapChain*, UINT, UINT);
	HRESULT Present1Body(std::size_t, IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT ResizeBuffersBody(std::size_t, IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	HRESULT SetColorSpace1Body(std::size_t, IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);
	HRESULT CreateSwapChainBody(std::size_t, IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	HRESULT CreateSwapChainForHwndBody(std::size_t, IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
	HRESULT OuterCreateSwapChainBody(std::size_t, IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
	HRESULT OuterCreateSwapChainForHwndBody(std::size_t, IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	// One detour per entry (D<I>::Fn is entry I's): each knows its own original, nothing is looked up per frame.
	template <std::size_t I>
	struct D_OuterPresent
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags) { return OuterPresentBody(I, a_this, a_sync, a_flags); }
	};
	template <std::size_t I>
	struct D_OuterPresent1
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
		{
			return OuterPresent1Body(I, a_this, a_sync, a_flags, a_params);
		}
	};
	template <std::size_t I>
	struct D_Present
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags) { return PresentBody(I, a_this, a_sync, a_flags); }
	};
	template <std::size_t I>
	struct D_Present1
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
		{
			return Present1Body(I, a_this, a_sync, a_flags, a_params);
		}
	};
	template <std::size_t I>
	struct D_ResizeBuffers
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain* a_this, UINT a_count, UINT a_w, UINT a_h, DXGI_FORMAT a_fmt, UINT a_flags)
		{
			return ResizeBuffersBody(I, a_this, a_count, a_w, a_h, a_fmt, a_flags);
		}
	};
	template <std::size_t I>
	struct D_SetColorSpace1
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain3* a_this, DXGI_COLOR_SPACE_TYPE a_space) { return SetColorSpace1Body(I, a_this, a_space); }
	};
	template <std::size_t I>
	struct D_CreateSwapChain
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
		{
			return CreateSwapChainBody(I, a_this, a_device, a_desc, a_out);
		}
	};
	template <std::size_t I>
	struct D_CreateSwapChainForHwnd
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
		{
			return CreateSwapChainForHwndBody(I, a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		}
	};
	template <std::size_t I>
	struct D_OuterCreateSwapChain
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
		{
			return OuterCreateSwapChainBody(I, a_this, a_device, a_desc, a_out);
		}
	};
	template <std::size_t I>
	struct D_OuterCreateSwapChainForHwnd
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
		{
			return OuterCreateSwapChainForHwndBody(I, a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		}
	};

	void ExecuteCommandListsBody(std::size_t, ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
	template <std::size_t I>
	struct D_ExecuteCommandLists
	{
		static void STDMETHODCALLTYPE Fn(ID3D12CommandQueue* a_this, UINT a_count, ID3D12CommandList* const* a_lists)
		{
			ExecuteCommandListsBody(I, a_this, a_count, a_lists);
		}
	};

	using presenthooks::Detours;
	using presenthooks::InlineSet;
	using presenthooks::SlotSet;
	// the queue fallback only: installed the first time a swap chain with no known queue reaches Render
	InlineSet<ExecuteCommandLists_t>    h_ExecuteCommandLists{ "ID3D12CommandQueue::ExecuteCommandLists", 10, Detours<D_ExecuteCommandLists>() };
	// dxgi-level: MinHook on the function bodies (one entry per distinct body - dxgi's own, a wrapper's, ...)
	InlineSet<Present_t>                h_Present{ "IDXGISwapChain::Present", 8, Detours<D_Present>() };
	InlineSet<Present1_t>               h_Present1{ "IDXGISwapChain1::Present1", 22, Detours<D_Present1>() };
	InlineSet<ResizeBuffers_t>          h_ResizeBuffers{ "IDXGISwapChain::ResizeBuffers", 13, Detours<D_ResizeBuffers>() };
	InlineSet<SetColorSpace1_t>         h_SetColorSpace1{ "IDXGISwapChain3::SetColorSpace1", 38, Detours<D_SetColorSpace1>() };
	InlineSet<CreateSwapChain_t>        h_CreateSwapChain{ "IDXGIFactory::CreateSwapChain", 10, Detours<D_CreateSwapChain>() };
	InlineSet<CreateSwapChainForHwnd_t> h_CreateSwapChainForHwnd{ "IDXGIFactory2::CreateSwapChainForHwnd", 15, Detours<D_CreateSwapChainForHwnd>() };
	// game-facing: vtable slot writes on the objects the game holds (one entry per distinct function the slot held)
	SlotSet<Present_t>                h_OuterPresent{ "IDXGISwapChain::Present", 8, Detours<D_OuterPresent>() };
	SlotSet<Present1_t>               h_OuterPresent1{ "IDXGISwapChain1::Present1", 22, Detours<D_OuterPresent1>() };
	SlotSet<CreateSwapChain_t>        h_OuterCreateSwapChain{ "IDXGIFactory::CreateSwapChain", 10, Detours<D_OuterCreateSwapChain>() };
	SlotSet<CreateSwapChainForHwnd_t> h_OuterCreateSwapChainForHwnd{ "IDXGIFactory2::CreateSwapChainForHwnd", 15,
		Detours<D_OuterCreateSwapChainForHwnd>() };

	// The paths a frame can be drawn from - the log, the no-Present warning and DevBench name them in these words.
	constexpr const char* kPathGameFacing = "game-facing slot";
	constexpr const char* kPathCreation = "dxgi Present via the creation hook";
	constexpr const char* kPathProbe = "dxgi Present via the start-up probe";
	constexpr const char* kPathDrawPath1 = "uDrawPath=1 at dxgi's Present";

	const char* DxgiPath(int a_seenBy) { return (a_seenBy & presenthooks::kCreation) ? kPathCreation : kPathProbe; }

	// The outermost hooked Present on a thread starts a fresh frame for t_drawnBy.
	struct PresentScope
	{
		PresentScope()
		{
			if (t_presentDepth++ == 0) {
				t_drawnBy = nullptr;
				t_drawnModule = nullptr;
			}
			if (!g_anyPresent.load(std::memory_order_relaxed)) {
				g_anyPresent = true;
			}
		}
		~PresentScope() { --t_presentDepth; }
		PresentScope(const PresentScope&) = delete;
		PresentScope& operator=(const PresentScope&) = delete;
	};

	// Render from one path; when the frame was the overlay's, remember the path (this frame on this thread, and for DevBench).
	bool DrawAt(IDXGISwapChain* a_swapChain, const char* a_path, const std::string* a_module)
	{
		if (!Render(a_swapChain)) {
			return false;
		}
		t_drawnBy = a_path;
		t_drawnModule = a_module;
		g_drawModule = a_module;
		if (g_drawPath.exchange(a_path) != a_path) {
			logger::info("draw path now: {} ({})", a_path, a_module ? *a_module : std::string("?"));
		}
		return true;
	}

	// ONE line at the first Present that reaches each hooked class: which path drew that frame, and whether input is up.
	void NoteFirstPresent(const char* a_what, std::size_t a_entry, const std::string& a_module, const char* a_foundBy, bool a_drewHere)
	{
		std::string drawn;
		if (t_drawnBy) {
			drawn = std::format("drawn this frame by: {} ({}){}", t_drawnBy, t_drawnModule ? *t_drawnModule : std::string("?"),
				a_drewHere ? ", here" : "");
		} else if (g_failed) {
			drawn = "nothing drawn: the overlay failed to start (see the errors above)";
		} else {
			drawn = std::format("nothing drawn here (inside another hooked Present: {}, the game-facing Present draws: {})",
				t_presentDepth > 1 ? "yes" : "no", g_outerDraws.load() ? "yes" : "no");
		}
		logger::info("first Present through {} class {} ({}, found by {}): {}; input (WndProc) set up: {}", a_what, a_entry + 1, a_module,
			a_foundBy, drawn, g_origWndProc ? "yes" : "no");
	}

	// Game-facing Present: draw into the back buffer first, then let the frame travel on through Streamline, dxgi and
	// whatever overlays hooked dxgi - so they all see the AMF window, Steam's screenshot included.
	bool DrawBeforePresent(IDXGISwapChain* a_this, const char* a_which, const std::string* a_module)
	{
		if (g_foreignPresenter.load()) {
			return false;   // frame generation presents the inner swap chain on its own thread: AMF draws there, not here
		}
		if (settings::Get().drawPath == 1) {
			// [Display] uDrawPath=1 (troubleshooting, a Nexus report 2026-10-07): draw at dxgi's own Present instead, after
			// Streamline and whatever runs inside it - g_outerDraws stays false, so the dxgi-level Presents draw
			static std::atomic_bool s_said{ false };
			if (!s_said.exchange(true)) {
				logger::info("uDrawPath=1: the menu is drawn at dxgi's Present, after NVIDIA Streamline and anything inside it, not at the "
							 "game-facing {}", a_which);
			}
			return false;
		}
		IDXGISwapChain* target = (a_this == g_outerSwapChain.load()) ? g_innerSwapChain.load() : nullptr;
		if (!target) {
			static std::atomic_bool s_warned{ false };
			if (!s_warned.exchange(true)) {
				logger::warn("game-facing {} on swap chain {}, which was not seen being created; AMF draws at dxgi's Present instead",
					a_which, static_cast<void*>(a_this));
			}
			return false;
		}
		const bool drew = DrawAt(target, kPathGameFacing, a_module);
		if (drew && !g_outerDraws.exchange(true)) {
			logger::info("the game presents through {} of swap chain {} (the call lands in {}); AMF draws there, before dxgi's "
						 "Present and every overlay hooked into it, so Steam's F12 screenshot sees the AMF window",
				a_which, static_cast<void*>(a_this), a_module ? *a_module : std::string("?"));
		}
		return drew;
	}

	template <class Set, class Call>
	HRESULT GameFacingPresent(Set& a_set, std::size_t a_i, IDXGISwapChain* a_this, UINT a_flags, const char* a_which, Call&& a_call)
	{
		auto&        e = a_set.At(a_i);
		const bool   first = e.calls.fetch_add(1, std::memory_order_relaxed) == 0;
		PresentScope scope;
		bool         drew = false;
		HRESULT      hr;
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
			t_inPresent = true;
			drew = DrawBeforePresent(a_this, a_which, &e.module);
			hr = a_call(e.original);
			t_inPresent = false;
		} else {
			hr = a_call(e.original);
		}
		if (first) {
			NoteFirstPresent(a_set.What(), a_i, e.module, "the game-facing slot", drew);
		}
		return hr;
	}

	HRESULT OuterPresentBody(std::size_t a_i, IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
	{
		return GameFacingPresent(h_OuterPresent, a_i, a_this, a_flags, "Present", [&](Present_t a_o) { return a_o(a_this, a_sync, a_flags); });
	}

	HRESULT OuterPresent1Body(std::size_t a_i, IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
	{
		return GameFacingPresent(h_OuterPresent1, a_i, a_this, a_flags, "Present1",
			[&](Present1_t a_o) { return a_o(a_this, a_sync, a_flags, a_params); });
	}

	// dxgi-level Present: reached after the game-facing one (same thread) or from Streamline's own present thread.
	// It draws only while the game-facing path has not, so a frame is never drawn twice.
	void NoteDxgiPresent(const char* a_which)
	{
		static std::atomic_bool s_logged{ false };
		if (!s_logged.exchange(true)) {
			logger::info("dxgi's {} reached (inside the game-facing Present: {}; AMF draws at the game-facing one: {})", a_which,
				t_inPresent ? "yes" : "no", g_outerDraws.load() ? "yes" : "not yet");
		}
	}

	// True when the inner swap chain is presented from outside the game-facing Present (frame generation's own thread).
	bool ForeignPresenter(IDXGISwapChain* a_this)
	{
		if (t_inPresent || a_this != g_innerSwapChain.load()) {
			return false;
		}
		if (!g_foreignPresenter.exchange(true)) {
			logger::info("dxgi swap chain {} is presented on its own thread ({}), not from inside the game's Present - frame "
						 "generation: AMF draws there, on that thread and its queue, over every frame it presents",
				static_cast<void*>(a_this), ::GetCurrentThreadId());
		}
		return true;
	}

	// uDrawPath=1: dxgi's Present draws even when it is reached inside the game-facing Present. That one holds t_inPresent,
	// which made hk_Present / hk_Present1 skip the frame, so nothing drew at all (diagnosed and patched by a player, 美猫少佐 (Bineko Major),
	// on a Nexus report of 2026-10-11). Recursion is stopped by a guard of its own instead.
	template <class Original>
	HRESULT PresentAtDxgi(IDXGISwapChain* a_this, UINT a_flags, const char* a_which, const std::string* a_module, bool& a_drew, Original&& a_original)
	{
		if (t_inDxgiPresent || (a_flags & DXGI_PRESENT_TEST)) {
			return a_original();
		}
		t_inDxgiPresent = true;
		ForeignPresenter(a_this);   // false at once inside the game-facing Present; notes frame generation's own thread
		static std::atomic_bool s_said{ false };
		if (!s_said.exchange(true)) {
			logger::info("uDrawPath=1: dxgi's {} draws the menu (inside the game-facing Present: {})", a_which, t_inPresent ? "yes" : "no");
		}
		a_drew = DrawAt(a_this, kPathDrawPath1, a_module);
		const HRESULT hr = a_original();
		t_inDxgiPresent = false;
		return hr;
	}

	template <class Set, class Call>
	HRESULT DxgiPresent(Set& a_set, std::size_t a_i, IDXGISwapChain* a_this, UINT a_flags, const char* a_which, Call&& a_call)
	{
		auto&        e = a_set.At(a_i);
		const bool   first = e.calls.fetch_add(1, std::memory_order_relaxed) == 0;
		PresentScope scope;
		NoteDxgiPresent(a_which);
		bool    drew = false;
		HRESULT hr;
		if (settings::Get().drawPath == 1) {
			hr = PresentAtDxgi(a_this, a_flags, a_which, &e.module, drew, [&] { return a_call(e.original); });
		} else if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST) && (ForeignPresenter(a_this) || !g_outerDraws.load())) {
			t_inPresent = true;
			drew = DrawAt(a_this, DxgiPath(e.seenBy.load()), &e.module);
			hr = a_call(e.original);
			t_inPresent = false;
		} else {
			hr = a_call(e.original);
		}
		if (first) {
			NoteFirstPresent(a_set.What(), a_i, e.module, presenthooks::SourceName(e.seenBy.load()), drew);
		}
		return hr;
	}

	HRESULT PresentBody(std::size_t a_i, IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
	{
		return DxgiPresent(h_Present, a_i, a_this, a_flags, "Present", [&](Present_t a_o) { return a_o(a_this, a_sync, a_flags); });
	}

	HRESULT Present1Body(std::size_t a_i, IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
	{
		return DxgiPresent(h_Present1, a_i, a_this, a_flags, "Present1", [&](Present1_t a_o) { return a_o(a_this, a_sync, a_flags, a_params); });
	}

	HRESULT ResizeBuffersBody(std::size_t a_i, IDXGISwapChain* a_this, UINT a_count, UINT a_w, UINT a_h, DXGI_FORMAT a_fmt, UINT a_flags)
	{
		const auto original = h_ResizeBuffers.Original(a_i);
		const bool ours = g_ready && a_this == g_swapChain;
		if (ours) {
			WaitIdle();   // our commands on the old buffers are done; AMF holds no buffer, so the resize is never refused
		}
		const HRESULT hr = original(a_this, a_count, a_w, a_h, a_fmt, a_flags);
		if (ours && SUCCEEDED(hr)) {
			DXGI_SWAP_CHAIN_DESC desc{};
			a_this->GetDesc(&desc);
			std::scoped_lock l(g_gpuLock);
			EnsureFrames(desc.BufferCount);
			g_width = desc.BufferDesc.Width;
			g_height = desc.BufferDesc.Height;
			logger::info("resized to {}x{} ({} buffers)", desc.BufferDesc.Width, desc.BufferDesc.Height, desc.BufferCount);
		} else if (ours) {
			logger::warn("ResizeBuffers failed (0x{:08X})", static_cast<unsigned>(hr));
		}
		return hr;
	}

	// While a queue is wanted, each thread's last DIRECT submission is noted (the presenting thread submits its frame on the
	// queue it presents from just before Present); afterwards this is one atomic read and the call.
	void ExecuteCommandListsBody(std::size_t a_i, ID3D12CommandQueue* a_this, UINT a_count, ID3D12CommandList* const* a_lists)
	{
		if (g_wantQueue.load(std::memory_order_relaxed) && a_this && a_this->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
			t_lastDirectQueue = a_this;
		}
		h_ExecuteCommandLists.Original(a_i)(a_this, a_count, a_lists);
	}

	// Hooks ExecuteCommandLists through a DIRECT queue of our own on the swap chain's device (released at once).
	bool WatchQueues(IDXGISwapChain* a_swapChain)
	{
		ID3D12Device*       device = nullptr;
		ID3D12CommandQueue* temp = nullptr;
		D3D12_COMMAND_QUEUE_DESC qd{};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (SUCCEEDED(a_swapChain->GetDevice(IID_PPV_ARGS(&device))) && device) {
			device->CreateCommandQueue(&qd, IID_PPV_ARGS(&temp));
		}
		const int entry = temp ? h_ExecuteCommandLists.Hook(temp, presenthooks::kQueueHook) : -1;
		Release(temp);
		Release(device);
		if (entry >= 0) {
			logger::info("no queue for swap chain {} yet (its creation was not seen; GetDevice gives the device, not the queue): watching "
						 "ID3D12CommandQueue::ExecuteCommandLists - the overlay starts once the presenting thread has submitted on its queue",
				static_cast<void*>(a_swapChain));
		}
		return entry >= 0;
	}

	// The game's colour space for its swap chain: HDR10 (G2084 P2020), scRGB (G10 P709) or SDR (G22 P709) - the ground
	// truth for how the menu's colours must be written (HdrComposite.h).
	HRESULT SetColorSpace1Body(std::size_t a_i, IDXGISwapChain3* a_this, DXGI_COLOR_SPACE_TYPE a_space)
	{
		const HRESULT hr = h_SetColorSpace1.Original(a_i)(a_this, a_space);
		if (SUCCEEDED(hr) && !t_probe) {
			const int was = g_colorSpace.exchange(static_cast<int>(a_space));
			if (was != static_cast<int>(a_space)) {
				logger::info("hdr: the game set its swap chain's colour space to {}", static_cast<int>(a_space));
			}
		}
		return hr;
	}

	// [Visuals] HdrEnabled and HdrPaperWhite from the game's settings file (the fallback when the colour space was not seen)
	void ReadGameHdr()
	{
		PWSTR docs = nullptr;
		std::filesystem::path file;
		if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
			file = std::filesystem::path(docs) / L"The Witcher 3" / L"dx12user.settings";
		}
		if (docs) {
			::CoTaskMemFree(docs);
		}
		std::ifstream in(file);
		std::string   line;
		bool          visuals = false;
		int           enabled = -1;
		while (std::getline(in, line)) {
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (!line.empty() && line.front() == '[') {
				visuals = line == "[Visuals]";
				continue;
			}
			if (!visuals) continue;
			if (line.rfind("HdrEnabled=", 0) == 0) {
				enabled = (line.substr(11) == "true" || line.substr(11) == "1") ? 1 : 0;
			} else if (line.rfind("HdrPaperWhite=", 0) == 0) {
				const float pw = std::strtof(line.c_str() + 14, nullptr);
				if (pw >= 50.0f && pw <= 1000.0f) g_paperWhite = pw;
			}
		}
		g_gameHdr = enabled;
	}

	void CaptureQueue(IUnknown* a_device, const char* a_via)
	{
		ID3D12CommandQueue* q = nullptr;
		if (a_device && SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&q)))) {
			if (g_queue) {
				g_queue->Release();
			}
			g_queue = q;   // keep our reference
			g_queueFrom = a_via;
			logger::info("captured the presenting command queue {} via {}", static_cast<void*>(q), a_via);
		}
	}

	void HookSwapChain(IUnknown* a_swapChain, int a_source)
	{
		IDXGISwapChain* sc = nullptr;
		if (!a_swapChain || FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc))) || !sc) {
			return;
		}
		h_Present.Hook(sc, a_source);
		h_ResizeBuffers.Hook(sc, a_source);
		IDXGISwapChain3* sc3 = nullptr;
		if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3) {
			h_SetColorSpace1.Hook(sc3, a_source);
			sc3->Release();
		}
		IDXGISwapChain1* sc1 = nullptr;
		if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1))) && sc1) {
			h_Present1.Hook(sc1, a_source);
			sc1->Release();
		}
		sc->Release();
	}

	void NoteCreation()
	{
		ULONGLONG none = 0;
		g_firstCreateTick.compare_exchange_strong(none, GetTickCount64());
	}

	// A dxgi-level creation hook saw a swap chain made (the probe's own is passed over before this).
	void SwapChainCreated(IUnknown* a_device, IDXGISwapChain* a_swapChain, const char* a_via)
	{
		NoteCreation();
		CaptureQueue(a_device, a_via);
		HookSwapChain(a_swapChain, presenthooks::kCreation);
		t_created = a_swapChain;
		// The game-facing Present may now go through an object AMF has not hooked (frame generation's own swap chain):
		// draw at dxgi's Present again until the game-facing path draws for this swap chain.
		if (g_outerDraws.exchange(false)) {
			logger::info("new dxgi swap chain {} - drawing at dxgi's Present until the game-facing Present takes over again",
				static_cast<void*>(a_swapChain));
		}
		g_foreignPresenter = false;   // who presents the new one is seen at its first Present
	}

	HRESULT CreateSwapChainBody(std::size_t a_i, IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
	{
		const HRESULT hr = h_CreateSwapChain.Original(a_i)(a_this, a_device, a_desc, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out && *a_out) {
			SwapChainCreated(a_device, *a_out, "CreateSwapChain");
		}
		return hr;
	}

	HRESULT CreateSwapChainForHwndBody(std::size_t a_i, IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
	{
		const HRESULT hr = h_CreateSwapChainForHwnd.Original(a_i)(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out && *a_out) {
			SwapChainCreated(a_device, *a_out, "CreateSwapChainForHwnd");
		}
		return hr;
	}

	// The swap chain the game was handed. Streamline creates the real dxgi one INSIDE this call (the dxgi-level hook
	// above records it in t_created), so the pair is known here: the game presents the outer one, AMF draws into the inner.
	void AdoptGameSwapChain(IUnknown* a_game, const char* a_via)
	{
		IDXGISwapChain* inner = t_created;
		t_created = nullptr;
		IDXGISwapChain* sc = nullptr;
		if (!a_game || FAILED(a_game->QueryInterface(IID_PPV_ARGS(&sc))) || !sc) {
			return;
		}
		if (!inner) {
			logger::warn("game-facing {}: no dxgi swap chain was created inside it; AMF keeps drawing at dxgi's Present", a_via);
			sc->Release();
			return;
		}
		g_innerSwapChain = inner;
		g_outerSwapChain = sc;
		logger::info("game-facing {}: the game holds swap chain {} (Present in {}), wrapping dxgi swap chain {}{}", a_via,
			static_cast<void*>(sc), ModuleOf(SlotOf(sc, 8)), static_cast<void*>(inner), sc == inner ? " (the same object - no proxy)" : "");
		h_OuterPresent.Write(sc);
		IDXGISwapChain1* sc1 = nullptr;
		if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1))) && sc1) {
			h_OuterPresent1.Write(sc1);
			sc1->Release();
		}
		sc->Release();   // the game keeps its own reference; ours is only the pointer to compare against
	}

	HRESULT OuterCreateSwapChainBody(std::size_t a_i, IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
	{
		const auto original = h_OuterCreateSwapChain.Original(a_i);
		if (t_probe) {
			return original(a_this, a_device, a_desc, a_out);
		}
		t_created = nullptr;
		const HRESULT hr = original(a_this, a_device, a_desc, a_out);
		if (SUCCEEDED(hr) && a_out && *a_out) {
			NoteCreation();
			AdoptGameSwapChain(*a_out, "CreateSwapChain");
		}
		t_created = nullptr;
		return hr;
	}

	HRESULT OuterCreateSwapChainForHwndBody(std::size_t a_i, IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
	{
		const auto original = h_OuterCreateSwapChainForHwnd.Original(a_i);
		if (t_probe) {
			return original(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		}
		t_created = nullptr;
		const HRESULT hr = original(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		if (SUCCEEDED(hr) && a_out && *a_out) {
			NoteCreation();
			AdoptGameSwapChain(*a_out, "CreateSwapChainForHwnd");
		}
		t_created = nullptr;
		return hr;
	}

	void HookFactoryOuter(void* a_factory)
	{
		if (!a_factory) {
			return;
		}
		auto* unk = static_cast<IUnknown*>(a_factory);
		IDXGIFactory* f = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f))) && f) {
			h_OuterCreateSwapChain.Write(f);
			f->Release();
		}
		IDXGIFactory2* f2 = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f2))) && f2) {
			h_OuterCreateSwapChainForHwnd.Write(f2);
			f2->Release();
		}
	}

	void HookFactory(void* a_factory, int a_source)
	{
		if (!a_factory) {
			return;
		}
		auto* unk = static_cast<IUnknown*>(a_factory);
		IDXGIFactory* f = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f))) && f) {
			h_CreateSwapChain.Hook(f, a_source);
			f->Release();
		}
		IDXGIFactory2* f2 = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f2))) && f2) {
			h_CreateSwapChainForHwnd.Hook(f2, a_source);
			f2->Release();
		}
	}

	// The factory exports: the probe's own factory (and any d3d12 makes inside the probe's device creation) passes untouched.
	void FactoryMade(HRESULT a_hr, void** a_out)
	{
		if (t_probe || FAILED(a_hr) || !a_out) {
			return;
		}
		HookFactory(*a_out, presenthooks::kCreation);
		if (g_gameUsesDxgi) {
			HookFactoryOuter(*a_out);
		}
	}

	HRESULT WINAPI hk_CreateDXGIFactory(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory(a_riid, a_out);
		FactoryMade(hr, a_out);
		return hr;
	}

	HRESULT WINAPI hk_CreateDXGIFactory1(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory1(a_riid, a_out);
		FactoryMade(hr, a_out);
		return hr;
	}

	HRESULT WINAPI hk_CreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory2(a_flags, a_riid, a_out);
		FactoryMade(hr, a_out);
		return hr;
	}

	// The factory the game itself receives - from sl.interposer.dll on Witcher 3 5.0. Its exports are hooked; the
	// objects they return get the slot writes.
	HRESULT WINAPI hk_OuterCreateDXGIFactory(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_OuterCreateDXGIFactory(a_riid, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out) {
			HookFactoryOuter(*a_out);
		}
		return hr;
	}

	HRESULT WINAPI hk_OuterCreateDXGIFactory1(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_OuterCreateDXGIFactory1(a_riid, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out) {
			HookFactoryOuter(*a_out);
		}
		return hr;
	}

	HRESULT WINAPI hk_OuterCreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_OuterCreateDXGIFactory2(a_flags, a_riid, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out) {
			HookFactoryOuter(*a_out);
		}
		return hr;
	}

	// ---- the start-up thread: graphics modules, the probe, and the no-Present warning --------------------------------
	void ProbeOnFactory(IUnknown* a_factory) { HookFactory(a_factory, presenthooks::kProbe); }
	void ProbeOnSwapChain(IUnknown* a_swapChain) { HookSwapChain(a_swapChain, presenthooks::kProbe); }

	template <class Set>
	void ListInline(Set& a_set)
	{
		for (std::size_t i = 0; i < a_set.Count(); ++i) {
			auto& e = a_set.At(i);
			logger::warn("  hooked: {} class {} at {} in {} (found by {}), reached {} times", a_set.What(), i + 1, e.target, e.module,
				presenthooks::SourceName(e.seenBy.load()), e.calls.load());
		}
	}

	template <class Set>
	void ListSlots(Set& a_set)
	{
		for (std::size_t i = 0; i < a_set.Count(); ++i) {
			auto& e = a_set.At(i);
			logger::warn("  hooked: game-facing {} class {} (was {} in {}), reached {} times", a_set.What(), i + 1,
				reinterpret_cast<void*>(e.original), e.module, e.calls.load());
		}
	}

	void WarnNoPresent(bool a_created)
	{
		logger::warn("no Present has reached any AMF hook 15 s after {} - the menu cannot be drawn and F1 is not read until one does. "
					 "Every hook installed:",
			a_created ? "the first swap-chain creation AMF saw" : "start-up (AMF saw no swap chain being created at all)");
		for (const auto& e : g_exportHooks) {
			logger::warn("  hooked: {}", e);
		}
		ListInline(h_CreateSwapChain);
		ListInline(h_CreateSwapChainForHwnd);
		ListInline(h_Present);
		ListInline(h_Present1);
		ListInline(h_ResizeBuffers);
		ListInline(h_SetColorSpace1);
		ListInline(h_ExecuteCommandLists);
		ListSlots(h_OuterCreateSwapChain);
		ListSlots(h_OuterCreateSwapChainForHwnd);
		ListSlots(h_OuterPresent);
		ListSlots(h_OuterPresent1);
		{
			std::scoped_lock l(g_probeLock);
			logger::warn("  start-up probe: {}", g_probeSummary);
		}
		presenthooks::LogGraphicsModules("no Present after 15 s");
	}

	// Never under the loader lock: Install runs inside the .asi's DllMain (the ASI loader LoadLibrary's it at the game's entry
	// point, and DllMain itself always holds the lock). This thread starts its work once DllMain has returned.
	DWORD WINAPI StartupThread(LPVOID)
	{
		presenthooks::LogGraphicsModules("start-up");
		t_probe = true;
		const presenthooks::ProbeResult r = presenthooks::RunProbe(o_CreateDXGIFactory2, &ProbeOnFactory, &ProbeOnSwapChain);
		t_probe = false;
		{
			std::scoped_lock l(g_probeLock);
			g_probeSummary = r.summary;
		}
		while (!g_anyPresent.load()) {
			::Sleep(1000);
			const ULONGLONG created = g_firstCreateTick.load();
			const ULONGLONG from = std::max(g_installTick.load(), created);
			if (::GetTickCount64() >= from + 15000) {
				if (!g_anyPresent.load()) {
					WarnNoPresent(created != 0);
				}
				break;
			}
		}
		return 0;
	}

	// The module the exe imports CreateDXGIFactory* from, read from its import table (logic library: read the exe's
	// imports before choosing a hook). sl.interposer.dll on Witcher 3 5.0; dxgi.dll on a build without Streamline.
	HMODULE GameDxgiSource(std::string& a_name)
	{
		auto* base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
		auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
		if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return nullptr;
		}
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
		if (!dir.VirtualAddress) {
			return nullptr;
		}
		for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
			const auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
			for (; names->u1.AddressOfData; ++names) {
				if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
					continue;
				}
				const auto* byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
				if (std::strncmp(byName->Name, "CreateDXGIFactory", 17) == 0) {
					a_name = reinterpret_cast<const char*>(base + d->Name);
					return GetModuleHandleA(a_name.c_str());
				}
			}
		}
		return nullptr;
	}

	// ---- input ------------------------------------------------------------------------------------------------------
	// Every message goes to the input module first; it decides what the framework takes (the toggle key, and
	// everything while the menu is up) and what the game still gets (see Input.cpp).
	LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wp, LPARAM a_lp)
	{
		if (g_ready && input::OnWindowMessage(a_hwnd, a_msg, a_wp, a_lp)) {
			if (a_msg == WM_INPUT) {
				return DefWindowProcW(a_hwnd, a_msg, a_wp, a_lp);   // the raw-input buffer must still be released
			}
			return 0;
		}
		return CallWindowProcW(g_origWndProc, a_hwnd, a_msg, a_wp, a_lp);
	}
}

namespace Overlay
{
	bool Install()
	{
		g_installTick = ::GetTickCount64();
		if (const auto st = MH_Initialize(); st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			logger::error("MinHook failed to initialise ({})", static_cast<int>(st));
			return false;
		}
		// Witcher 3 5.0 does not import dxgi itself: NVIDIA Streamline's sl.interposer.dll hands the game
		// CreateDXGIFactory*, and it loads the SYSTEM dxgi.dll by full path. So the module to hook is System32's, by full
		// path too - a dxgi.dll proxy in bin\x64_dx12 (ReShade, OptiScaler) is a different module the game's own calls never
		// reach (RESEARCH-web 1.1). Loading it here makes Streamline's later LoadLibrary return this same module.
		wchar_t sys[MAX_PATH]{};
		const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
		const std::wstring dxgiPath = std::wstring(sys, n) + L"\\dxgi.dll";
		HMODULE dxgi = (n > 0 && n < MAX_PATH) ? LoadLibraryW(dxgiPath.c_str()) : nullptr;
		if (!dxgi) {
			logger::error("the system dxgi.dll did not load ({})", GetLastError());
			return false;
		}
		// 1.0.6: d3d12.dll is delay-loaded (xmake.lua) and System32's copy is loaded here by full path, so AMF's own d3d12 calls
		// bind to it and AMF never loads a d3d12.dll proxy from bin\x64_dx12 itself (a ReShade d3d12.dll was loaded by AMF's
		// static import up to 1.0.5).
		{
			const std::wstring d3d12Path = std::wstring(sys, n) + L"\\d3d12.dll";
			const HMODULE before = GetModuleHandleW(L"d3d12.dll");
			const HMODULE d3d12 = LoadLibraryW(d3d12Path.c_str());
			if (!d3d12) {
				logger::warn("the system d3d12.dll did not load by full path ({}); AMF's d3d12 calls bind to whatever d3d12.dll loads first", GetLastError());
			} else if (before && before != d3d12) {
				logger::warn("a d3d12.dll other than System32's was already loaded at {} before AMF ({}); AMF's own calls may bind to it",
					static_cast<void*>(before), ModuleOf(reinterpret_cast<const void*>(before)));
			} else {
				logger::info("System32's d3d12.dll at {} (AMF's d3d12 calls bind to it)", static_cast<void*>(d3d12));
			}
		}
		logger::info("hooking the system dxgi.dll at {} (already loaded before AMF: {})", static_cast<void*>(dxgi),
			GetModuleHandleW(L"sl.interposer.dll") ? "Streamline yes" : "Streamline not yet");
		struct Export
		{
			const char* name;
			void*       detour;
			void**      original;
		};
		const Export exports[] = {
			{ "CreateDXGIFactory", reinterpret_cast<void*>(&hk_CreateDXGIFactory), reinterpret_cast<void**>(&o_CreateDXGIFactory) },
			{ "CreateDXGIFactory1", reinterpret_cast<void*>(&hk_CreateDXGIFactory1), reinterpret_cast<void**>(&o_CreateDXGIFactory1) },
			{ "CreateDXGIFactory2", reinterpret_cast<void*>(&hk_CreateDXGIFactory2), reinterpret_cast<void**>(&o_CreateDXGIFactory2) },
		};
		int ok = 0;
		for (const auto& e : exports) {
			void* target = reinterpret_cast<void*>(GetProcAddress(dxgi, e.name));
			if (target && MH_CreateHook(target, e.detour, e.original) == MH_OK && MH_EnableHook(target) == MH_OK) {
				logger::info("hooked dxgi!{}", e.name);
				g_exportHooks.push_back(std::format("System32 dxgi!{} at {}", e.name, target));
				++ok;
			} else {
				logger::warn("could not hook dxgi!{}", e.name);
			}
		}

		// The game-facing level: whatever module the exe takes its factory from. AMF draws on the objects that one
		// returns, so the window is in the back buffer before Steam's hook on dxgi's Present takes its screenshot.
		std::string sourceName;
		HMODULE     source = GameDxgiSource(sourceName);
		if (!source) {
			logger::warn("the exe imports no CreateDXGIFactory (or its module is not loaded); AMF draws at dxgi's Present only, "
						 "where an overlay hooked after it (Steam) may screenshot before it draws");
		} else if (source == dxgi || _stricmp(sourceName.c_str(), "dxgi.dll") == 0) {
			g_gameUsesDxgi = true;
			logger::info("the exe takes CreateDXGIFactory* from dxgi.dll itself; the game-facing slots go on dxgi's own objects");
		} else {
			const Export outer[] = {
				{ "CreateDXGIFactory", reinterpret_cast<void*>(&hk_OuterCreateDXGIFactory), reinterpret_cast<void**>(&o_OuterCreateDXGIFactory) },
				{ "CreateDXGIFactory1", reinterpret_cast<void*>(&hk_OuterCreateDXGIFactory1), reinterpret_cast<void**>(&o_OuterCreateDXGIFactory1) },
				{ "CreateDXGIFactory2", reinterpret_cast<void*>(&hk_OuterCreateDXGIFactory2), reinterpret_cast<void**>(&o_OuterCreateDXGIFactory2) },
			};
			for (const auto& e : outer) {
				void* target = reinterpret_cast<void*>(GetProcAddress(source, e.name));
				if (target && MH_CreateHook(target, e.detour, e.original) == MH_OK && MH_EnableHook(target) == MH_OK) {
					logger::info("hooked {}!{} (the factory the game itself receives)", sourceName, e.name);
					g_exportHooks.push_back(std::format("{}!{} at {} (game-facing)", sourceName, e.name, target));
				} else {
					logger::warn("could not hook {}!{}", sourceName, e.name);
				}
			}
		}
		// The graphics-module report, the start-up probe and the no-Present warning run on a thread of their own: this is
		// DllMain, under the loader lock, where creating a D3D12 device (driver threads, driver DLLs) could deadlock. The
		// thread's work starts once DllMain has returned. Failing to start it leaves the hooks above as they are.
		if (const HANDLE t = ::CreateThread(nullptr, 0, &StartupThread, nullptr, 0, nullptr)) {
			::CloseHandle(t);
		} else {
			logger::warn("the start-up thread (graphics modules, probe, no-Present warning) could not start ({}); AMF relies on its "
						 "creation hooks alone", ::GetLastError());
		}
		return ok > 0;
	}

	void* GameWindow() { return g_hwnd; }

	std::string PresentStateJson()
	{
		auto esc = [](std::string a_s) {
			std::string out;
			for (const char c : a_s) {
				if (c == '"' || c == '\\') out += '\\';
				if (static_cast<unsigned char>(c) >= 0x20) out += c;
			}
			return out;
		};
		const char*        path = g_drawPath.load();
		const std::string* module = g_drawModule.load();
		std::string        probe;
		{
			std::scoped_lock l(g_probeLock);
			probe = g_probeSummary;
		}
		return std::format(R"({{"path":"{}","module":"{}","classesHooked":{{"dxgiPresent":{},"dxgiPresent1":{},"gameFacingPresent":{},)"
						   R"("gameFacingPresent1":{},"createSwapChain":{},"createSwapChainForHwnd":{},"executeCommandLists":{}}},"probe":"{}","queueFrom":"{}",)"
						   R"("anyPresent":{},"inputSetUp":{}}})",
			path ? path : "none", esc(module ? *module : std::string()), h_Present.Count(), h_Present1.Count(), h_OuterPresent.Count(),
			h_OuterPresent1.Count(), h_CreateSwapChain.Count() + h_OuterCreateSwapChain.Count(),
			h_CreateSwapChainForHwnd.Count() + h_OuterCreateSwapChainForHwnd.Count(), h_ExecuteCommandLists.Count(), esc(probe), esc(g_queueFrom.load()),
			g_anyPresent.load() ? "true" : "false", g_origWndProc ? "true" : "false");
	}
}

namespace gfx
{
	bool Ready() { return g_ready.load() && g_device && g_queue && g_srvHeap; }

	bool InitImGuiBackend()
	{
		if (!g_device || !g_srvHeap) {
			return false;
		}
		g_backendUp = ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frames.size()), hdr::UiFormat(g_format), g_srvHeap,
			g_srvHeap->GetCPUDescriptorHandleForHeapStart(), g_srvHeap->GetGPUDescriptorHandleForHeapStart());
		return g_backendUp;
	}

	void NewFrame()
	{
		if (g_backendUp) {
			ImGui_ImplDX12_NewFrame();
		}
	}

	void InvalidateDeviceObjects()
	{
		if (!g_backendUp) {
			return;
		}
		// The font texture may still be read by a frame in flight; releasing it under the GPU is a device removal.
		WaitIdle();
		ImGui_ImplDX12_InvalidateDeviceObjects();
	}

	// The 3D preview's view of the overlay (Preview3D.h): the device, the shared descriptor heap and its free slots, a
	// synchronous upload on the presenting queue, and a wait for the frames in flight. Made ready on first use.
	bool EnsurePreview()
	{
		if (preview3d::Ready()) {
			return true;
		}
		if (!Ready()) {
			return false;
		}
		preview3d::Host h;
		h.device = g_device;
		h.srvHeap = g_srvHeap;
		h.allocSrv = [](D3D12_CPU_DESCRIPTOR_HANDLE& a_cpu, D3D12_GPU_DESCRIPTOR_HANDLE& a_gpu) {
			std::scoped_lock l(g_gpuLock);
			if (g_freeSlots.empty()) {
				return false;
			}
			const UINT slot = g_freeSlots.back();
			g_freeSlots.pop_back();
			a_cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
			a_gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
			a_cpu.ptr += static_cast<SIZE_T>(slot) * g_srvStep;
			a_gpu.ptr += static_cast<UINT64>(slot) * g_srvStep;
			return true;
		};
		h.freeSrv = [](std::uint64_t a_gpuPtr) {
			std::scoped_lock l(g_gpuLock);
			const UINT64 base = g_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr;
			if (a_gpuPtr > base && g_srvStep) {
				g_freeSlots.push_back(static_cast<UINT>((a_gpuPtr - base) / g_srvStep));
			}
		};
		h.upload = [](const std::function<void(ID3D12GraphicsCommandList*)>& a_record) {
			std::scoped_lock l(g_gpuLock);
			if (!g_uploadAllocator &&
				(FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAllocator))) ||
					FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAllocator, nullptr, IID_PPV_ARGS(&g_uploadList))) ||
					FAILED(g_uploadList->Close()))) {
				return false;
			}
			g_uploadAllocator->Reset();
			g_uploadList->Reset(g_uploadAllocator, nullptr);
			a_record(g_uploadList);
			if (FAILED(g_uploadList->Close())) {
				return false;
			}
			ID3D12CommandList* lists[] = { g_uploadList };
			g_queue->ExecuteCommandLists(1, lists);
			const UINT64 v = ++g_fenceValue;
			g_queue->Signal(g_fence, v);
			WaitFor(v, 5000);
			return true;
		};
		h.waitIdle = [] { WaitIdle(); };
		h.defer = [](std::function<void()> a_free) {
			std::scoped_lock l(g_gpuLock);
			g_deferred.push_back({ 0, std::move(a_free) });
		};
		return preview3d::Init(h);
	}

	namespace
	{
		// One level of a texture, tightly packed RGBA8.
		struct Level
		{
			const std::uint8_t* rgba = nullptr;
			int                 w = 0, h = 0;
		};

		// Uploads every level as one subresource each (level 0 = the full size) and returns the ImTextureID, or null (logged).
		// g_gpuLock is taken here.
		void* UploadLevels(const std::vector<Level>& a_levels)
		{
			if (!Ready() || a_levels.empty() || !a_levels[0].rgba || a_levels[0].w <= 0 || a_levels[0].h <= 0) {
				return nullptr;
			}
			const int  width = a_levels[0].w, height = a_levels[0].h;
			const UINT mips = static_cast<UINT>(a_levels.size());
			std::scoped_lock l(g_gpuLock);
			if (g_freeSlots.empty()) {
				logger::warn("texture: all {} descriptors are in use; nothing uploaded", kSrvCount - 1);
				return nullptr;
			}

			D3D12_HEAP_PROPERTIES heap{};
			heap.Type = D3D12_HEAP_TYPE_DEFAULT;
			D3D12_RESOURCE_DESC rd{};
			rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			rd.Width = static_cast<UINT64>(width);
			rd.Height = static_cast<UINT>(height);
			rd.DepthOrArraySize = 1;
			rd.MipLevels = static_cast<UINT16>(mips);
			rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			rd.SampleDesc.Count = 1;
			rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			ID3D12Resource* tex = nullptr;
			if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
				logger::warn("texture: CreateCommittedResource {}x{} ({} levels) failed", width, height, mips);
				return nullptr;
			}

			// Where each level sits in one upload buffer (row pitch and offset alignment are the device's to say).
			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(mips);
			std::vector<UINT>   rows(mips);
			std::vector<UINT64> rowBytes(mips);
			UINT64              total = 0;
			g_device->GetCopyableFootprints(&rd, 0, mips, 0, layouts.data(), rows.data(), rowBytes.data(), &total);

			D3D12_HEAP_PROPERTIES up{};
			up.Type = D3D12_HEAP_TYPE_UPLOAD;
			D3D12_RESOURCE_DESC bd{};
			bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			bd.Width = total;
			bd.Height = 1;
			bd.DepthOrArraySize = 1;
			bd.MipLevels = 1;
			bd.SampleDesc.Count = 1;
			bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			ID3D12Resource* upload = nullptr;
			if (FAILED(g_device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
				logger::warn("texture: upload buffer of {} bytes failed", total);
				tex->Release();
				return nullptr;
			}
			void*       mapped = nullptr;
			D3D12_RANGE none{ 0, 0 };
			if (FAILED(upload->Map(0, &none, &mapped)) || !mapped) {
				upload->Release();
				tex->Release();
				return nullptr;
			}
			for (UINT m = 0; m < mips; ++m) {
				const Level& lv = a_levels[m];
				auto* dst = static_cast<std::uint8_t*>(mapped) + layouts[m].Offset;
				for (int y = 0; y < lv.h; ++y) {
					std::memcpy(dst + static_cast<std::size_t>(y) * layouts[m].Footprint.RowPitch,
						lv.rgba + static_cast<std::size_t>(y) * lv.w * 4, static_cast<std::size_t>(lv.w) * 4);
				}
			}
			upload->Unmap(0, nullptr);

			if (!g_uploadAllocator &&
				(FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_uploadAllocator))) ||
					FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_uploadAllocator, nullptr, IID_PPV_ARGS(&g_uploadList))) ||
					FAILED(g_uploadList->Close()))) {
				logger::warn("texture: the upload command list could not be created");
				Release(g_uploadAllocator);
				upload->Release();
				tex->Release();
				return nullptr;
			}
			g_uploadAllocator->Reset();
			g_uploadList->Reset(g_uploadAllocator, nullptr);
			for (UINT m = 0; m < mips; ++m) {
				D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
				src.pResource = upload;
				src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
				src.PlacedFootprint = layouts[m];
				dst.pResource = tex;
				dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
				dst.SubresourceIndex = m;
				g_uploadList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = tex;
			b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
			g_uploadList->ResourceBarrier(1, &b);
			g_uploadList->Close();
			ID3D12CommandList* lists[] = { g_uploadList };
			g_queue->ExecuteCommandLists(1, lists);
			const UINT64 v = ++g_fenceValue;
			g_queue->Signal(g_fence, v);
			WaitFor(v, 2000);
			upload->Release();

			const UINT slot = g_freeSlots.back();
			g_freeSlots.pop_back();
			D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
			sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			sv.Texture2D.MipLevels = mips;
			D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
			D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
			cpu.ptr += static_cast<SIZE_T>(slot) * g_srvStep;
			gpu.ptr += static_cast<UINT64>(slot) * g_srvStep;
			g_device->CreateShaderResourceView(tex, &sv, cpu);
			g_textures[gpu.ptr] = Texture{ tex, slot };
			logger::debug("texture: {}x{} ({} level(s)) uploaded to descriptor {}", width, height, mips, slot);
			return reinterpret_cast<void*>(gpu.ptr);
		}
	}

	void* CreateTextureRGBA(const void* a_rgba, int a_width, int a_height)
	{
		if (!a_rgba || a_width <= 0 || a_height <= 0) {
			return nullptr;
		}
		return UploadLevels({ Level{ static_cast<const std::uint8_t*>(a_rgba), a_width, a_height } });
	}

	// THE MIP CHAIN, BUILT ON THE CPU (1.0.5, the art library - Skyrim 2.1.6 asked D3D11's GenerateMips; D3D12 has none).
	// Each level is the one above halved (never below 1) by a 2x2 box filter. Colour is averaged WEIGHTED BY ALPHA, so the
	// transparent pixels round a frame's ornament (black with no alpha) do not darken its edges as the art shrinks; alpha is
	// the plain average. An odd size folds its last row / column into the box beside it (the sample is clamped).
	void* CreateTextureRGBAMips(const void* a_rgba, int a_width, int a_height, int* a_outMips)
	{
		if (a_outMips) {
			*a_outMips = 0;
		}
		if (!a_rgba || a_width <= 0 || a_height <= 0) {
			return nullptr;
		}
		std::vector<std::vector<std::uint8_t>> chain;
		std::vector<Level> levels{ Level{ static_cast<const std::uint8_t*>(a_rgba), a_width, a_height } };
		chain.reserve(16);
		int w = a_width, h = a_height;
		while (w > 1 || h > 1) {
			const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
			const std::uint8_t* src = levels.back().rgba;
			std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
			for (int y = 0; y < nh; ++y) {
				for (int x = 0; x < nw; ++x) {
					double r = 0.0, g = 0.0, bl = 0.0, a = 0.0;
					int    n = 0;
					for (int dy = 0; dy < 2; ++dy) {
						for (int dx = 0; dx < 2; ++dx) {
							const int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
							const std::uint8_t* p = src + (static_cast<std::size_t>(sy) * w + sx) * 4;
							const double pa = p[3];
							r += p[0] * pa;
							g += p[1] * pa;
							bl += p[2] * pa;
							a += pa;
							++n;
						}
					}
					std::uint8_t* q = dst.data() + (static_cast<std::size_t>(y) * nw + x) * 4;
					q[0] = static_cast<std::uint8_t>(a > 0.0 ? std::lround(r / a) : 0);
					q[1] = static_cast<std::uint8_t>(a > 0.0 ? std::lround(g / a) : 0);
					q[2] = static_cast<std::uint8_t>(a > 0.0 ? std::lround(bl / a) : 0);
					q[3] = static_cast<std::uint8_t>(std::lround(a / n));
				}
			}
			chain.push_back(std::move(dst));
			levels.push_back(Level{ chain.back().data(), nw, nh });
			w = nw;
			h = nh;
		}
		void* id = UploadLevels(levels);
		if (id && a_outMips) {
			*a_outMips = static_cast<int>(levels.size());
		}
		return id;
	}

	void ReleaseTexture(void* a_textureId)
	{
		if (!a_textureId) {
			return;
		}
		// not at once: this frame's draw list may still show it (deferred release, above)
		std::scoped_lock l(g_gpuLock);
		if (const auto it = g_textures.find(reinterpret_cast<std::uint64_t>(a_textureId)); it != g_textures.end()) {
			ID3D12Resource* res = it->second.resource;
			const UINT      slot = it->second.slot;
			g_textures.erase(it);
			g_deferred.push_back({ 0, [res, slot] {
				res->Release();
				g_freeSlots.push_back(slot);
			} });
		}
	}

	bool DecodeImageFile(const std::wstring& a_path, std::vector<std::uint8_t>& a_rgba, int& a_width, int& a_height)
	{
		const HRESULT          co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);   // WIC needs COM on this thread
		IWICImagingFactory*    factory = nullptr;
		IWICBitmapDecoder*     decoder = nullptr;
		IWICBitmapFrameDecode* frame = nullptr;
		IWICFormatConverter*   converter = nullptr;
		bool                   ok = false;
		UINT                   w = 0, h = 0;
		if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
			SUCCEEDED(factory->CreateDecoderFromFilename(a_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
			SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&w, &h)) && w > 0 && h > 0 &&
			SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
			SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
			a_rgba.resize(static_cast<std::size_t>(w) * h * 4);
			ok = SUCCEEDED(converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(a_rgba.size()), a_rgba.data()));
		}
		Release(converter);
		Release(frame);
		Release(decoder);
		Release(factory);
		if (SUCCEEDED(co)) {
			CoUninitialize();
		}
		a_width = static_cast<int>(w);
		a_height = static_cast<int>(h);
		return ok;
	}

	void GetBackBufferSize(unsigned& a_width, unsigned& a_height)
	{
		a_width = g_width;
		a_height = g_height;
	}
}
