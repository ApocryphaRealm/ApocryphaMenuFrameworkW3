#include "Overlay.h"

#include "DevBenchTool.h"
#include "Gfx.h"
#include "Input.h"
#include "Renderer.h"

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <wincodec.h>

#include <cstring>

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

	CreateFactory_t          o_CreateDXGIFactory = nullptr;
	CreateFactory_t          o_CreateDXGIFactory1 = nullptr;
	CreateFactory2_t         o_CreateDXGIFactory2 = nullptr;
	CreateSwapChain_t        o_CreateSwapChain = nullptr;
	CreateSwapChainForHwnd_t o_CreateSwapChainForHwnd = nullptr;
	Present_t                o_Present = nullptr;
	Present1_t               o_Present1 = nullptr;
	ResizeBuffers_t          o_ResizeBuffers = nullptr;

	// game-facing originals: what the slot held before we wrote it
	CreateFactory_t          o_OuterCreateDXGIFactory = nullptr;
	CreateFactory_t          o_OuterCreateDXGIFactory1 = nullptr;
	CreateFactory2_t         o_OuterCreateDXGIFactory2 = nullptr;
	CreateSwapChain_t        o_OuterCreateSwapChain = nullptr;
	CreateSwapChainForHwnd_t o_OuterCreateSwapChainForHwnd = nullptr;
	Present_t                o_OuterPresent = nullptr;
	Present1_t               o_OuterPresent1 = nullptr;

	template <class T>
	void Release(T*& a_p)
	{
		if (a_p) {
			a_p->Release();
			a_p = nullptr;
		}
	}

	// ---- vtable hooks (MinHook on the function the vtable slot points at; shared by every instance) --------------
	template <class T>
	bool HookSlot(void* a_object, std::size_t a_index, void* a_detour, T& a_original, const char* a_what)
	{
		if (a_original) {
			return true;
		}
		if (!a_object) {
			return false;
		}
		auto** vtbl = *reinterpret_cast<void***>(a_object);
		void*  target = vtbl ? vtbl[a_index] : nullptr;
		if (!target) {
			logger::error("hook {}: vtable slot {} is empty", a_what, a_index);
			return false;
		}
		if (MH_CreateHook(target, a_detour, reinterpret_cast<void**>(&a_original)) != MH_OK ||
			MH_EnableHook(target) != MH_OK) {
			logger::error("hook {}: MinHook failed at {}", a_what, target);
			a_original = nullptr;
			return false;
		}
		logger::info("hooked {} at {}", a_what, target);
		return true;
	}

	// The file name of the module a code address lives in - which Present the game really calls is read from this.
	std::string ModuleOf(const void* a_address)
	{
		HMODULE m = nullptr;
		wchar_t file[MAX_PATH]{};
		if (a_address &&
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				static_cast<LPCWSTR>(a_address), &m) &&
			GetModuleFileNameW(m, file, MAX_PATH)) {
			return std::filesystem::path(file).filename().string();
		}
		return "?";
	}

	void* SlotOf(void* a_object, std::size_t a_index)
	{
		auto** vtbl = a_object ? *reinterpret_cast<void***>(a_object) : nullptr;
		return vtbl ? vtbl[a_index] : nullptr;
	}

	// ---- game-facing hooks (a direct write of the vtable SLOT; every object of that class goes through it first) -------
	template <class T>
	bool WriteSlot(void* a_object, std::size_t a_index, void* a_detour, T& a_original, const char* a_what)
	{
		auto** vtbl = a_object ? *reinterpret_cast<void***>(a_object) : nullptr;
		if (!vtbl) {
			return false;
		}
		void* current = vtbl[a_index];
		if (current == a_detour) {
			return true;   // this class is already ours
		}
		if (!current) {
			logger::error("game-facing {}: vtable slot {} is empty", a_what, a_index);
			return false;
		}
		if (a_original && reinterpret_cast<void*>(a_original) != current) {
			logger::warn("game-facing {}: a second class ({} in {}) - only the first is redirected", a_what, current, ModuleOf(current));
			return false;
		}
		DWORD old = 0;
		if (!VirtualProtect(&vtbl[a_index], sizeof(void*), PAGE_READWRITE, &old)) {
			logger::error("game-facing {}: VirtualProtect failed ({})", a_what, GetLastError());
			return false;
		}
		a_original = reinterpret_cast<T>(current);   // set before the slot is live, so the detour always has it
		InterlockedExchangePointer(&vtbl[a_index], a_detour);
		VirtualProtect(&vtbl[a_index], sizeof(void*), old, &old);
		logger::info("game-facing {}: vtable slot {} redirected (was {} in {})", a_what, a_index, current, ModuleOf(current));
		return true;
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

	bool Init(IDXGISwapChain* a_swapChain)
	{
		if (g_ready || g_failed) {
			return g_ready;
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
		if (g_format != oldFormat && g_backendUp) {
			// the pipeline state names the render-target format: rebuild the backend for the new one (fonts follow at NewFrame)
			ImGui_ImplDX12_Shutdown();
			g_backendUp = ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frames.size()), g_format, g_srvHeap,
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
			return true;   // nothing on screen this frame (menu closed, no HUD element drew): nothing recorded
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

		std::scoped_lock l(g_gpuLock);
		Frame& f = g_frames[idx];
		WaitFor(f.fence, 1000);
		f.allocator->Reset();
		g_list->Reset(f.allocator, nullptr);
		g_device->CreateRenderTargetView(backBuffer, nullptr, f.rtv);

		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = backBuffer;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		g_list->ResourceBarrier(1, &b);
		g_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
		g_list->SetDescriptorHeaps(1, &g_srvHeap);
		ImGui_ImplDX12_RenderDrawData(dd, g_list);
		std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
		g_list->ResourceBarrier(1, &b);
		g_list->Close();
		ID3D12CommandList* lists[] = { g_list };
		g_queue->ExecuteCommandLists(1, lists);
		f.fence = ++g_fenceValue;
		g_queue->Signal(g_fence, f.fence);
		backBuffer->Release();   // the swap chain keeps it alive for the queued commands; we keep nothing
		return true;
	}

	// ---- detours ----------------------------------------------------------------------------------------------------
	// Game-facing Present: draw into the back buffer first, then let the frame travel on through Streamline, dxgi and
	// whatever overlays hooked dxgi - so they all see the AMF window, Steam's screenshot included.
	void DrawBeforePresent(IDXGISwapChain* a_this, const char* a_which, const void* a_original)
	{
		if (g_foreignPresenter.load()) {
			return;   // frame generation presents the inner swap chain on its own thread: AMF draws there, not here
		}
		IDXGISwapChain* target = (a_this == g_outerSwapChain.load()) ? g_innerSwapChain.load() : nullptr;
		if (!target) {
			static std::atomic_bool s_warned{ false };
			if (!s_warned.exchange(true)) {
				logger::warn("game-facing {} on swap chain {}, which was not seen being created; AMF draws at dxgi's Present instead",
					a_which, static_cast<void*>(a_this));
			}
			return;
		}
		if (Render(target) && !g_outerDraws.exchange(true)) {
			logger::info("the game presents through {} of swap chain {} (the call lands in {}); AMF draws there, before dxgi's "
						 "Present and every overlay hooked into it, so Steam's F12 screenshot sees the AMF window",
				a_which, static_cast<void*>(a_this), ModuleOf(a_original));
		}
	}

	HRESULT STDMETHODCALLTYPE hk_OuterPresent(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
	{
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
			t_inPresent = true;
			DrawBeforePresent(a_this, "Present", reinterpret_cast<const void*>(o_OuterPresent));
			const HRESULT hr = o_OuterPresent(a_this, a_sync, a_flags);
			t_inPresent = false;
			return hr;
		}
		return o_OuterPresent(a_this, a_sync, a_flags);
	}

	HRESULT STDMETHODCALLTYPE hk_OuterPresent1(IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
	{
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
			t_inPresent = true;
			DrawBeforePresent(a_this, "Present1", reinterpret_cast<const void*>(o_OuterPresent1));
			const HRESULT hr = o_OuterPresent1(a_this, a_sync, a_flags, a_params);
			t_inPresent = false;
			return hr;
		}
		return o_OuterPresent1(a_this, a_sync, a_flags, a_params);
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

	HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
	{
		NoteDxgiPresent("Present");
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST) && (ForeignPresenter(a_this) || !g_outerDraws.load())) {
			t_inPresent = true;
			Render(a_this);
			const HRESULT hr = o_Present(a_this, a_sync, a_flags);
			t_inPresent = false;
			return hr;
		}
		return o_Present(a_this, a_sync, a_flags);
	}

	HRESULT STDMETHODCALLTYPE hk_Present1(IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
	{
		NoteDxgiPresent("Present1");
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST) && (ForeignPresenter(a_this) || !g_outerDraws.load())) {
			t_inPresent = true;
			Render(a_this);
			const HRESULT hr = o_Present1(a_this, a_sync, a_flags, a_params);
			t_inPresent = false;
			return hr;
		}
		return o_Present1(a_this, a_sync, a_flags, a_params);
	}

	HRESULT STDMETHODCALLTYPE hk_ResizeBuffers(IDXGISwapChain* a_this, UINT a_count, UINT a_w, UINT a_h, DXGI_FORMAT a_fmt, UINT a_flags)
	{
		const bool ours = g_ready && a_this == g_swapChain;
		if (ours) {
			WaitIdle();   // our commands on the old buffers are done; AMF holds no buffer, so the resize is never refused
		}
		const HRESULT hr = o_ResizeBuffers(a_this, a_count, a_w, a_h, a_fmt, a_flags);
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

	void CaptureQueue(IUnknown* a_device, const char* a_via)
	{
		ID3D12CommandQueue* q = nullptr;
		if (a_device && SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&q)))) {
			if (g_queue) {
				g_queue->Release();
			}
			g_queue = q;   // keep our reference
			logger::info("captured the presenting command queue {} via {}", static_cast<void*>(q), a_via);
		}
	}

	void HookSwapChain(IUnknown* a_swapChain)
	{
		IDXGISwapChain* sc = nullptr;
		if (!a_swapChain || FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc)))) {
			return;
		}
		HookSlot(sc, 8, reinterpret_cast<void*>(&hk_Present), o_Present, "IDXGISwapChain::Present");
		HookSlot(sc, 13, reinterpret_cast<void*>(&hk_ResizeBuffers), o_ResizeBuffers, "IDXGISwapChain::ResizeBuffers");
		IDXGISwapChain1* sc1 = nullptr;
		if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1)))) {
			HookSlot(sc1, 22, reinterpret_cast<void*>(&hk_Present1), o_Present1, "IDXGISwapChain1::Present1");
			sc1->Release();
		}
		sc->Release();
	}

	HRESULT STDMETHODCALLTYPE hk_CreateSwapChain(IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
	{
		const HRESULT hr = o_CreateSwapChain(a_this, a_device, a_desc, a_out);
		if (SUCCEEDED(hr) && a_out && *a_out) {
			CaptureQueue(a_device, "CreateSwapChain");
			HookSwapChain(*a_out);
			t_created = *a_out;
			// The game-facing Present may now go through an object AMF has not hooked (frame generation's own swap chain):
			// draw at dxgi's Present again until the game-facing path draws for this swap chain.
			if (g_outerDraws.exchange(false)) {
				logger::info("new dxgi swap chain {} - drawing at dxgi's Present until the game-facing Present takes over again",
					static_cast<void*>(*a_out));
			}
			g_foreignPresenter = false;   // who presents the new one is seen at its first Present
		}
		return hr;
	}

	HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForHwnd(IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
	{
		const HRESULT hr = o_CreateSwapChainForHwnd(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		if (SUCCEEDED(hr) && a_out && *a_out) {
			CaptureQueue(a_device, "CreateSwapChainForHwnd");
			HookSwapChain(*a_out);
			t_created = *a_out;
			// The game-facing Present may now go through an object AMF has not hooked (frame generation's own swap chain):
			// draw at dxgi's Present again until the game-facing path draws for this swap chain.
			if (g_outerDraws.exchange(false)) {
				logger::info("new dxgi swap chain {} - drawing at dxgi's Present until the game-facing Present takes over again",
					static_cast<void*>(*a_out));
			}
			g_foreignPresenter = false;   // who presents the new one is seen at its first Present
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
		if (!a_game || FAILED(a_game->QueryInterface(IID_PPV_ARGS(&sc)))) {
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
		WriteSlot(sc, 8, reinterpret_cast<void*>(&hk_OuterPresent), o_OuterPresent, "IDXGISwapChain::Present");
		IDXGISwapChain1* sc1 = nullptr;
		if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1)))) {
			WriteSlot(sc1, 22, reinterpret_cast<void*>(&hk_OuterPresent1), o_OuterPresent1, "IDXGISwapChain1::Present1");
			sc1->Release();
		}
		sc->Release();   // the game keeps its own reference; ours is only the pointer to compare against
	}

	HRESULT STDMETHODCALLTYPE hk_OuterCreateSwapChain(IDXGIFactory* a_this, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
	{
		t_created = nullptr;
		const HRESULT hr = o_OuterCreateSwapChain(a_this, a_device, a_desc, a_out);
		if (SUCCEEDED(hr) && a_out && *a_out) {
			AdoptGameSwapChain(*a_out, "CreateSwapChain");
		}
		t_created = nullptr;
		return hr;
	}

	HRESULT STDMETHODCALLTYPE hk_OuterCreateSwapChainForHwnd(IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
	{
		t_created = nullptr;
		const HRESULT hr = o_OuterCreateSwapChainForHwnd(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		if (SUCCEEDED(hr) && a_out && *a_out) {
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
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f)))) {
			WriteSlot(f, 10, reinterpret_cast<void*>(&hk_OuterCreateSwapChain), o_OuterCreateSwapChain, "IDXGIFactory::CreateSwapChain");
			f->Release();
		}
		IDXGIFactory2* f2 = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f2)))) {
			WriteSlot(f2, 15, reinterpret_cast<void*>(&hk_OuterCreateSwapChainForHwnd), o_OuterCreateSwapChainForHwnd,
				"IDXGIFactory2::CreateSwapChainForHwnd");
			f2->Release();
		}
	}

	void HookFactory(void* a_factory)
	{
		if (!a_factory) {
			return;
		}
		auto* unk = static_cast<IUnknown*>(a_factory);
		IDXGIFactory* f = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f)))) {
			HookSlot(f, 10, reinterpret_cast<void*>(&hk_CreateSwapChain), o_CreateSwapChain, "IDXGIFactory::CreateSwapChain");
			f->Release();
		}
		IDXGIFactory2* f2 = nullptr;
		if (SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&f2)))) {
			HookSlot(f2, 15, reinterpret_cast<void*>(&hk_CreateSwapChainForHwnd), o_CreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
			f2->Release();
		}
	}

	HRESULT WINAPI hk_CreateDXGIFactory(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory(a_riid, a_out);
		if (SUCCEEDED(hr) && a_out) {
			HookFactory(*a_out);
			if (g_gameUsesDxgi) {
				HookFactoryOuter(*a_out);
			}
		}
		return hr;
	}

	HRESULT WINAPI hk_CreateDXGIFactory1(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory1(a_riid, a_out);
		if (SUCCEEDED(hr) && a_out) {
			HookFactory(*a_out);
			if (g_gameUsesDxgi) {
				HookFactoryOuter(*a_out);
			}
		}
		return hr;
	}

	HRESULT WINAPI hk_CreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory2(a_flags, a_riid, a_out);
		if (SUCCEEDED(hr) && a_out) {
			HookFactory(*a_out);
			if (g_gameUsesDxgi) {
				HookFactoryOuter(*a_out);
			}
		}
		return hr;
	}

	// The factory the game itself receives - from sl.interposer.dll on Witcher 3 5.0. Its exports are hooked; the
	// objects they return get the slot writes.
	HRESULT WINAPI hk_OuterCreateDXGIFactory(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_OuterCreateDXGIFactory(a_riid, a_out);
		if (SUCCEEDED(hr) && a_out) {
			HookFactoryOuter(*a_out);
		}
		return hr;
	}

	HRESULT WINAPI hk_OuterCreateDXGIFactory1(REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_OuterCreateDXGIFactory1(a_riid, a_out);
		if (SUCCEEDED(hr) && a_out) {
			HookFactoryOuter(*a_out);
		}
		return hr;
	}

	HRESULT WINAPI hk_OuterCreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_OuterCreateDXGIFactory2(a_flags, a_riid, a_out);
		if (SUCCEEDED(hr) && a_out) {
			HookFactoryOuter(*a_out);
		}
		return hr;
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
				} else {
					logger::warn("could not hook {}!{}", sourceName, e.name);
				}
			}
		}
		return ok > 0;
	}

	void* GameWindow() { return g_hwnd; }
}

namespace gfx
{
	bool Ready() { return g_ready.load() && g_device && g_queue && g_srvHeap; }

	bool InitImGuiBackend()
	{
		if (!g_device || !g_srvHeap) {
			return false;
		}
		g_backendUp = ImGui_ImplDX12_Init(g_device, static_cast<int>(g_frames.size()), g_format, g_srvHeap,
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

	void* CreateTextureRGBA(const void* a_rgba, int a_width, int a_height)
	{
		if (!Ready() || !a_rgba || a_width <= 0 || a_height <= 0) {
			return nullptr;
		}
		std::scoped_lock l(g_gpuLock);
		if (g_freeSlots.empty()) {
			logger::warn("texture: all {} descriptors are in use; nothing uploaded", kSrvCount - 1);
			return nullptr;
		}

		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC rd{};
		rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rd.Width = static_cast<UINT64>(a_width);
		rd.Height = static_cast<UINT>(a_height);
		rd.DepthOrArraySize = 1;
		rd.MipLevels = 1;
		rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		rd.SampleDesc.Count = 1;
		rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		ID3D12Resource* tex = nullptr;
		if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
			logger::warn("texture: CreateCommittedResource {}x{} failed", a_width, a_height);
			return nullptr;
		}

		const UINT pitch = (static_cast<UINT>(a_width) * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
		const UINT size = pitch * static_cast<UINT>(a_height);
		D3D12_HEAP_PROPERTIES up{};
		up.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bd{};
		bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bd.Width = size;
		bd.Height = 1;
		bd.DepthOrArraySize = 1;
		bd.MipLevels = 1;
		bd.SampleDesc.Count = 1;
		bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ID3D12Resource* upload = nullptr;
		if (FAILED(g_device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
			logger::warn("texture: upload buffer of {} bytes failed", size);
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
		for (int y = 0; y < a_height; ++y) {
			std::memcpy(static_cast<std::uint8_t*>(mapped) + static_cast<std::size_t>(y) * pitch,
				static_cast<const std::uint8_t*>(a_rgba) + static_cast<std::size_t>(y) * a_width * 4, static_cast<std::size_t>(a_width) * 4);
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
		D3D12_TEXTURE_COPY_LOCATION src{}, dst{};
		src.pResource = upload;
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, static_cast<UINT>(a_width), static_cast<UINT>(a_height), 1, pitch };
		dst.pResource = tex;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = 0;
		g_uploadList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
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
		sv.Texture2D.MipLevels = 1;
		D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
		cpu.ptr += static_cast<SIZE_T>(slot) * g_srvStep;
		gpu.ptr += static_cast<UINT64>(slot) * g_srvStep;
		g_device->CreateShaderResourceView(tex, &sv, cpu);
		g_textures[gpu.ptr] = Texture{ tex, slot };
		logger::debug("texture: {}x{} uploaded to descriptor {}", a_width, a_height, slot);
		return reinterpret_cast<void*>(gpu.ptr);
	}

	void ReleaseTexture(void* a_textureId)
	{
		if (!a_textureId) {
			return;
		}
		WaitIdle();
		std::scoped_lock l(g_gpuLock);
		if (const auto it = g_textures.find(reinterpret_cast<std::uint64_t>(a_textureId)); it != g_textures.end()) {
			it->second.resource->Release();
			g_freeSlots.push_back(it->second.slot);
			g_textures.erase(it);
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
