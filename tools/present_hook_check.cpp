// The Present hooks outside the game (1.0.6): `xmake build present_hook_check` then `xmake run present_hook_check`.
// Runs src/PresentHooks.cpp - the multi-class hook tables and the start-up probe the .asi uses - on a D3D12 device and real
// dxgi swap chains of its own, in hidden windows, and checks that a Present is caught:
//   (b) on a swap chain made BEFORE any hook existed (a path the creation hooks never saw), presented through a hand-written
//       COM proxy whose Present forwards to the real one (the shape of ReShade's and Streamline's wrappers) - caught at
//       dxgi's own Present, hooked by the probe alone; its queue is found without its creation (QueueForSwapChain:
//       GetDevice, else the presenting thread's DIRECT ExecuteCommandLists, watched through a hook made on demand);
//   (a) on a plain dxgi swap chain made through dxgi's CreateDXGIFactory2 after AMF-style hooks - the creation hook sees it;
//   (c) on a second and a third class appearing after the first (inline hooks and game-facing slot writes alike), with the
//       nested-Present guard drawing each frame exactly once;
// and that the probe's own swap chain is never recorded as the game's (no queue captured, no creation counted).
// The detour BODIES here stand in for Overlay.cpp's (they count instead of drawing); the tables, the probe, the slot
// writes and the guard pattern are the shipped ones. Exit code 0 = every check passed.

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include "PresentHooks.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace
{
	using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	using SetColorSpace1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);
	using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
	using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);
	using D3D12CreateDevice_t = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

	int g_failures = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
		if (!a_ok) ++g_failures;
	}

	// ---- the stand-in overlay: what Overlay.cpp's detours decide, counted instead of drawn ----------------------------
	thread_local bool t_probe = false;
	thread_local bool t_inPresent = false;
	int               g_draws = 0;           // "Render" calls this frame
	void*             g_drawnInto = nullptr;  // the swap chain the last draw went to
	const char*       g_drawnBy = nullptr;
	int               g_created = 0;          // swap chains the creation hook recorded
	IUnknown*         g_capturedQueue = nullptr;

	void Draw(void* a_swapChain, const char* a_by)
	{
		++g_draws;
		g_drawnInto = a_swapChain;
		g_drawnBy = a_by;
	}

	HRESULT PresentBody(std::size_t, IDXGISwapChain*, UINT, UINT);
	HRESULT Present1Body(std::size_t, IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
	HRESULT OuterPresentBody(std::size_t, IDXGISwapChain*, UINT, UINT);
	HRESULT ResizeBuffersBody(std::size_t, IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	HRESULT SetColorSpace1Body(std::size_t, IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);
	HRESULT CreateSwapChainForHwndBody(std::size_t, IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);

	template <std::size_t I>
	struct D_Present
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags) { return PresentBody(I, a_this, a_sync, a_flags); }
	};
	template <std::size_t I>
	struct D_Present1
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_p)
		{
			return Present1Body(I, a_this, a_sync, a_flags, a_p);
		}
	};
	template <std::size_t I>
	struct D_OuterPresent
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain* a_this, UINT a_sync, UINT a_flags) { return OuterPresentBody(I, a_this, a_sync, a_flags); }
	};
	template <std::size_t I>
	struct D_ResizeBuffers
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain* a_this, UINT a_n, UINT a_w, UINT a_h, DXGI_FORMAT a_f, UINT a_fl)
		{
			return ResizeBuffersBody(I, a_this, a_n, a_w, a_h, a_f, a_fl);
		}
	};
	template <std::size_t I>
	struct D_SetColorSpace1
	{
		static HRESULT STDMETHODCALLTYPE Fn(IDXGISwapChain3* a_this, DXGI_COLOR_SPACE_TYPE a_s) { return SetColorSpace1Body(I, a_this, a_s); }
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

	using presenthooks::Detours;
	presenthooks::InlineSet<Present_t>                h_Present{ "IDXGISwapChain::Present", 8, Detours<D_Present>() };
	presenthooks::InlineSet<Present1_t>               h_Present1{ "IDXGISwapChain1::Present1", 22, Detours<D_Present1>() };
	presenthooks::InlineSet<ResizeBuffers_t>          h_ResizeBuffers{ "IDXGISwapChain::ResizeBuffers", 13, Detours<D_ResizeBuffers>() };
	presenthooks::InlineSet<SetColorSpace1_t>         h_SetColorSpace1{ "IDXGISwapChain3::SetColorSpace1", 38, Detours<D_SetColorSpace1>() };
	presenthooks::InlineSet<CreateSwapChainForHwnd_t> h_CreateSwapChainForHwnd{ "IDXGIFactory2::CreateSwapChainForHwnd", 15,
		Detours<D_CreateSwapChainForHwnd>() };
	presenthooks::SlotSet<Present_t>                  h_OuterPresent{ "IDXGISwapChain::Present", 8, Detours<D_OuterPresent>() };

	// the same guard as Overlay.cpp's drawing path 0: the first hooked Present on the thread draws, nested ones forward
	HRESULT PresentBody(std::size_t a_i, IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
	{
		auto& e = h_Present.At(a_i);
		e.calls.fetch_add(1);
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
			t_inPresent = true;
			Draw(a_this, "dxgi-level Present");
			const HRESULT hr = e.original(a_this, a_sync, a_flags);
			t_inPresent = false;
			return hr;
		}
		return e.original(a_this, a_sync, a_flags);
	}

	HRESULT Present1Body(std::size_t a_i, IDXGISwapChain1* a_this, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_p)
	{
		auto& e = h_Present1.At(a_i);
		e.calls.fetch_add(1);
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
			t_inPresent = true;
			Draw(a_this, "dxgi-level Present1");
			const HRESULT hr = e.original(a_this, a_sync, a_flags, a_p);
			t_inPresent = false;
			return hr;
		}
		return e.original(a_this, a_sync, a_flags, a_p);
	}

	HRESULT OuterPresentBody(std::size_t a_i, IDXGISwapChain* a_this, UINT a_sync, UINT a_flags)
	{
		auto& e = h_OuterPresent.At(a_i);
		e.calls.fetch_add(1);
		if (!t_inPresent && !(a_flags & DXGI_PRESENT_TEST)) {
			t_inPresent = true;
			Draw(a_this, "game-facing slot");
			const HRESULT hr = e.original(a_this, a_sync, a_flags);
			t_inPresent = false;
			return hr;
		}
		return e.original(a_this, a_sync, a_flags);
	}

	HRESULT ResizeBuffersBody(std::size_t a_i, IDXGISwapChain* a_this, UINT a_n, UINT a_w, UINT a_h, DXGI_FORMAT a_f, UINT a_fl)
	{
		h_ResizeBuffers.At(a_i).calls.fetch_add(1);
		return h_ResizeBuffers.Original(a_i)(a_this, a_n, a_w, a_h, a_f, a_fl);
	}

	HRESULT SetColorSpace1Body(std::size_t a_i, IDXGISwapChain3* a_this, DXGI_COLOR_SPACE_TYPE a_s)
	{
		h_SetColorSpace1.At(a_i).calls.fetch_add(1);
		return h_SetColorSpace1.Original(a_i)(a_this, a_s);
	}

	using ExecuteCommandLists_t = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
	thread_local ID3D12CommandQueue* t_lastDirectQueue = nullptr;
	std::atomic_bool                 g_wantQueue{ false };
	void ExecuteCommandListsBody(std::size_t, ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
	template <std::size_t I>
	struct D_ExecuteCommandLists
	{
		static void STDMETHODCALLTYPE Fn(ID3D12CommandQueue* a_this, UINT a_n, ID3D12CommandList* const* a_l) { ExecuteCommandListsBody(I, a_this, a_n, a_l); }
	};
	presenthooks::InlineSet<ExecuteCommandLists_t> h_ExecuteCommandLists{ "ID3D12CommandQueue::ExecuteCommandLists", 10,
		Detours<D_ExecuteCommandLists>() };
	// as Overlay.cpp's
	void ExecuteCommandListsBody(std::size_t a_i, ID3D12CommandQueue* a_this, UINT a_n, ID3D12CommandList* const* a_l)
	{
		if (g_wantQueue.load(std::memory_order_relaxed) && a_this && a_this->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
			t_lastDirectQueue = a_this;
		}
		h_ExecuteCommandLists.Original(a_i)(a_this, a_n, a_l);
	}
	// as Overlay.cpp's WatchQueues
	bool WatchQueues(IDXGISwapChain* a_swapChain)
	{
		ID3D12Device*       device = nullptr;
		ID3D12CommandQueue* temp = nullptr;
		D3D12_COMMAND_QUEUE_DESC qd{};
		qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (SUCCEEDED(a_swapChain->GetDevice(IID_PPV_ARGS(&device))) && device) device->CreateCommandQueue(&qd, IID_PPV_ARGS(&temp));
		const int entry = temp ? h_ExecuteCommandLists.Hook(temp, presenthooks::kQueueHook) : -1;
		if (temp) temp->Release();
		if (device) device->Release();
		return entry >= 0;
	}
	// one empty, closed command list of the queue's type, executed on it
	void Submit(ID3D12CommandQueue* a_queue)
	{
		ID3D12Device* device = nullptr;
		if (!a_queue || FAILED(a_queue->GetDevice(IID_PPV_ARGS(&device))) || !device) return;
		const D3D12_COMMAND_LIST_TYPE type = a_queue->GetDesc().Type;
		ID3D12CommandAllocator*    alloc = nullptr;
		ID3D12GraphicsCommandList* list = nullptr;
		if (SUCCEEDED(device->CreateCommandAllocator(type, IID_PPV_ARGS(&alloc))) &&
			SUCCEEDED(device->CreateCommandList(0, type, alloc, nullptr, IID_PPV_ARGS(&list))) && SUCCEEDED(list->Close())) {
			ID3D12CommandList* lists[] = { list };
			a_queue->ExecuteCommandLists(1, lists);
			ID3D12Fence* fence = nullptr;
			if (SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
				a_queue->Signal(fence, 1);
				const HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
				fence->SetEventOnCompletion(1, ev);
				WaitForSingleObject(ev, 2000);
				CloseHandle(ev);
				fence->Release();
			}
		}
		if (list) list->Release();
		if (alloc) alloc->Release();
		device->Release();
	}

	void HookSwapChain(IUnknown* a_swapChain, int a_source)
	{
		IDXGISwapChain* sc = nullptr;
		if (!a_swapChain || FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&sc))) || !sc) return;
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

	HRESULT CreateSwapChainForHwndBody(std::size_t a_i, IDXGIFactory2* a_this, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc,
		const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fs, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
	{
		const HRESULT hr = h_CreateSwapChainForHwnd.Original(a_i)(a_this, a_device, a_hwnd, a_desc, a_fs, a_output, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out && *a_out) {
			++g_created;
			ID3D12CommandQueue* q = nullptr;
			if (a_device && SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&q))) && q) {
				if (g_capturedQueue) g_capturedQueue->Release();
				g_capturedQueue = q;
			}
			HookSwapChain(*a_out, presenthooks::kCreation);
		}
		return hr;
	}

	void HookFactory(IUnknown* a_factory, int a_source)
	{
		IDXGIFactory2* f2 = nullptr;
		if (a_factory && SUCCEEDED(a_factory->QueryInterface(IID_PPV_ARGS(&f2))) && f2) {
			h_CreateSwapChainForHwnd.Hook(f2, a_source);
			f2->Release();
		}
	}

	CreateFactory2_t o_CreateDXGIFactory2 = nullptr;
	HRESULT WINAPI   hk_CreateDXGIFactory2(UINT a_flags, REFIID a_riid, void** a_out)
	{
		const HRESULT hr = o_CreateDXGIFactory2(a_flags, a_riid, a_out);
		if (!t_probe && SUCCEEDED(hr) && a_out) HookFactory(static_cast<IUnknown*>(*a_out), presenthooks::kCreation);
		return hr;
	}

	void ProbeOnFactory(IUnknown* a_factory) { HookFactory(a_factory, presenthooks::kProbe); }
	void ProbeOnSwapChain(IUnknown* a_swapChain) { HookSwapChain(a_swapChain, presenthooks::kProbe); }

	// ---- a hand-written COM proxy: every call forwards to the real swap chain (ReShade's / Streamline's shape) ----------
	int g_proxyPresents[2] = {};

	template <int Tag>
	class ProxySwapChain final : public IDXGISwapChain3
	{
	public:
		explicit ProxySwapChain(IDXGISwapChain3* a_real) : _real(a_real) { _real->AddRef(); }

		STDMETHODIMP QueryInterface(REFIID a_riid, void** a_out) override
		{
			if (!a_out) return E_POINTER;
			if (a_riid == __uuidof(IUnknown) || a_riid == __uuidof(IDXGIObject) || a_riid == __uuidof(IDXGIDeviceSubObject) ||
				a_riid == __uuidof(IDXGISwapChain) || a_riid == __uuidof(IDXGISwapChain1) || a_riid == __uuidof(IDXGISwapChain2) ||
				a_riid == __uuidof(IDXGISwapChain3)) {
				*a_out = static_cast<IDXGISwapChain3*>(this);
				AddRef();
				return S_OK;
			}
			return _real->QueryInterface(a_riid, a_out);
		}
		STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&_refs)); }
		STDMETHODIMP_(ULONG) Release() override
		{
			const LONG n = InterlockedDecrement(&_refs);
			if (n == 0) {
				_real->Release();
				delete this;
			}
			return static_cast<ULONG>(n);
		}
		STDMETHODIMP SetPrivateData(REFGUID a_g, UINT a_n, const void* a_d) override { return _real->SetPrivateData(a_g, a_n, a_d); }
		STDMETHODIMP SetPrivateDataInterface(REFGUID a_g, const IUnknown* a_u) override { return _real->SetPrivateDataInterface(a_g, a_u); }
		STDMETHODIMP GetPrivateData(REFGUID a_g, UINT* a_n, void* a_d) override { return _real->GetPrivateData(a_g, a_n, a_d); }
		STDMETHODIMP GetParent(REFIID a_r, void** a_p) override { return _real->GetParent(a_r, a_p); }
		STDMETHODIMP GetDevice(REFIID a_r, void** a_d) override { return _real->GetDevice(a_r, a_d); }
		STDMETHODIMP Present(UINT a_sync, UINT a_flags) override
		{
			++g_proxyPresents[Tag];   // also keeps each Tag's Present a distinct function (no identical-code folding)
			return _real->Present(a_sync, a_flags);
		}
		STDMETHODIMP GetBuffer(UINT a_i, REFIID a_r, void** a_s) override { return _real->GetBuffer(a_i, a_r, a_s); }
		STDMETHODIMP SetFullscreenState(BOOL a_f, IDXGIOutput* a_t) override { return _real->SetFullscreenState(a_f, a_t); }
		STDMETHODIMP GetFullscreenState(BOOL* a_f, IDXGIOutput** a_t) override { return _real->GetFullscreenState(a_f, a_t); }
		STDMETHODIMP GetDesc(DXGI_SWAP_CHAIN_DESC* a_d) override { return _real->GetDesc(a_d); }
		STDMETHODIMP ResizeBuffers(UINT a_n, UINT a_w, UINT a_h, DXGI_FORMAT a_f, UINT a_fl) override { return _real->ResizeBuffers(a_n, a_w, a_h, a_f, a_fl); }
		STDMETHODIMP ResizeTarget(const DXGI_MODE_DESC* a_d) override { return _real->ResizeTarget(a_d); }
		STDMETHODIMP GetContainingOutput(IDXGIOutput** a_o) override { return _real->GetContainingOutput(a_o); }
		STDMETHODIMP GetFrameStatistics(DXGI_FRAME_STATISTICS* a_s) override { return _real->GetFrameStatistics(a_s); }
		STDMETHODIMP GetLastPresentCount(UINT* a_c) override { return _real->GetLastPresentCount(a_c); }
		STDMETHODIMP GetDesc1(DXGI_SWAP_CHAIN_DESC1* a_d) override { return _real->GetDesc1(a_d); }
		STDMETHODIMP GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_d) override { return _real->GetFullscreenDesc(a_d); }
		STDMETHODIMP GetHwnd(HWND* a_h) override { return _real->GetHwnd(a_h); }
		STDMETHODIMP GetCoreWindow(REFIID a_r, void** a_u) override { return _real->GetCoreWindow(a_r, a_u); }
		STDMETHODIMP Present1(UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_p) override
		{
			++g_proxyPresents[Tag];
			return _real->Present1(a_sync, a_flags, a_p);
		}
		STDMETHODIMP_(BOOL) IsTemporaryMonoSupported() override { return _real->IsTemporaryMonoSupported(); }
		STDMETHODIMP GetRestrictToOutput(IDXGIOutput** a_o) override { return _real->GetRestrictToOutput(a_o); }
		STDMETHODIMP SetBackgroundColor(const DXGI_RGBA* a_c) override { return _real->SetBackgroundColor(a_c); }
		STDMETHODIMP GetBackgroundColor(DXGI_RGBA* a_c) override { return _real->GetBackgroundColor(a_c); }
		STDMETHODIMP SetRotation(DXGI_MODE_ROTATION a_r) override { return _real->SetRotation(a_r); }
		STDMETHODIMP GetRotation(DXGI_MODE_ROTATION* a_r) override { return _real->GetRotation(a_r); }
		STDMETHODIMP SetSourceSize(UINT a_w, UINT a_h) override { return _real->SetSourceSize(a_w, a_h); }
		STDMETHODIMP GetSourceSize(UINT* a_w, UINT* a_h) override { return _real->GetSourceSize(a_w, a_h); }
		STDMETHODIMP SetMaximumFrameLatency(UINT a_l) override { return _real->SetMaximumFrameLatency(a_l); }
		STDMETHODIMP GetMaximumFrameLatency(UINT* a_l) override { return _real->GetMaximumFrameLatency(a_l); }
		STDMETHODIMP_(HANDLE) GetFrameLatencyWaitableObject() override { return _real->GetFrameLatencyWaitableObject(); }
		STDMETHODIMP SetMatrixTransform(const DXGI_MATRIX_3X2_F* a_m) override { return _real->SetMatrixTransform(a_m); }
		STDMETHODIMP GetMatrixTransform(DXGI_MATRIX_3X2_F* a_m) override { return _real->GetMatrixTransform(a_m); }
		STDMETHODIMP_(UINT) GetCurrentBackBufferIndex() override { return _real->GetCurrentBackBufferIndex(); }
		STDMETHODIMP CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE a_c, UINT* a_s) override { return _real->CheckColorSpaceSupport(a_c, a_s); }
		STDMETHODIMP SetColorSpace1(DXGI_COLOR_SPACE_TYPE a_c) override { return _real->SetColorSpace1(a_c); }
		STDMETHODIMP ResizeBuffers1(UINT a_n, UINT a_w, UINT a_h, DXGI_FORMAT a_f, UINT a_fl, const UINT* a_mask, IUnknown* const* a_q) override
		{
			return _real->ResizeBuffers1(a_n, a_w, a_h, a_f, a_fl, a_mask, a_q);
		}

	private:
		~ProxySwapChain() = default;
		IDXGISwapChain3* _real;
		LONG             _refs = 1;
	};

	HWND MakeWindow(const wchar_t* a_title)
	{
		return CreateWindowExW(WS_EX_TOOLWINDOW, L"AMF_PresentHookCheck", a_title, WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
			GetModuleHandleW(nullptr), nullptr);
	}

	IDXGISwapChain3* MakeSwapChain(IDXGIFactory2* a_factory, ID3D12CommandQueue* a_queue, HWND a_window)
	{
		DXGI_SWAP_CHAIN_DESC1 sd{};
		sd.Width = 64;
		sd.Height = 64;
		sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		IDXGISwapChain1* sc1 = nullptr;
		IDXGISwapChain3* sc3 = nullptr;
		if (SUCCEEDED(a_factory->CreateSwapChainForHwnd(a_queue, a_window, &sd, nullptr, nullptr, &sc1)) && sc1) {
			sc1->QueryInterface(IID_PPV_ARGS(&sc3));
			sc1->Release();
		}
		return sc3;
	}

	bool SameObject(IUnknown* a_a, IUnknown* a_b)
	{
		if (!a_a || !a_b) return false;
		IUnknown* ia = nullptr;
		IUnknown* ib = nullptr;
		a_a->QueryInterface(IID_PPV_ARGS(&ia));
		a_b->QueryInterface(IID_PPV_ARGS(&ib));
		const bool same = ia && ia == ib;
		if (ia) ia->Release();
		if (ib) ib->Release();
		return same;
	}

	// Presents a_frames frames through a_through; true when every frame drew exactly once, into a_expectInto.
	bool Frames(IDXGISwapChain* a_through, int a_frames, void* a_expectInto, const char* a_expectBy, std::string& a_note)
	{
		bool ok = true;
		for (int f = 0; f < a_frames; ++f) {
			g_draws = 0;
			g_drawnInto = nullptr;
			g_drawnBy = nullptr;
			const HRESULT hr = a_through->Present(0, 0);
			const bool    frameOk = SUCCEEDED(hr) && g_draws == 1 && g_drawnInto == a_expectInto &&
			                     (!a_expectBy || (g_drawnBy && std::string(g_drawnBy) == a_expectBy));
			if (!frameOk) {
				ok = false;
				a_note = std::format("frame {}: hr 0x{:08X}, {} draw(s), into {} by {}", f, static_cast<unsigned>(hr), g_draws, g_drawnInto,
					g_drawnBy ? g_drawnBy : "nothing");
			}
		}
		if (ok) {
			a_note = std::format("{} frames, one draw each, by {}", a_frames, g_drawnBy ? g_drawnBy : "?");
		}
		return ok;
	}
}

int main()
{
	auto log = spdlog::stdout_logger_mt("present_hook_check");
	log->set_pattern("  [log] %v");
	spdlog::set_default_logger(log);

	if (MH_Initialize() != MH_OK) {
		std::printf("FAIL: MinHook did not initialise\n");
		return 1;
	}
	wchar_t sys[MAX_PATH]{};
	const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
	const std::wstring sysDir(sys, n);
	const HMODULE d3d12 = LoadLibraryW((sysDir + L"\\d3d12.dll").c_str());
	const HMODULE dxgi = LoadLibraryW((sysDir + L"\\dxgi.dll").c_str());
	const auto    createDevice = d3d12 ? reinterpret_cast<D3D12CreateDevice_t>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
	const auto    createFactory2 = dxgi ? reinterpret_cast<CreateFactory2_t>(GetProcAddress(dxgi, "CreateDXGIFactory2")) : nullptr;
	if (!createDevice || !createFactory2) {
		std::printf("FAIL: System32's d3d12.dll / dxgi.dll did not load\n");
		return 1;
	}

	presenthooks::LogGraphicsModules("check start-up");

	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWindowProcW;
	wc.hInstance = GetModuleHandleW(nullptr);
	wc.lpszClassName = L"AMF_PresentHookCheck";
	RegisterClassExW(&wc);
	const HWND windowA = MakeWindow(L"A");
	const HWND windowB = MakeWindow(L"B");

	// the "game": its device and queue, made before any hook (as Streamline's are, from System32 by full path)
	IDXGIFactory2*      factory0 = nullptr;
	ID3D12Device*       device = nullptr;
	ID3D12CommandQueue* queue = nullptr;
	createFactory2(0, IID_PPV_ARGS(&factory0));
	createDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
	D3D12_COMMAND_QUEUE_DESC qd{};
	qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (device) device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
	ID3D12CommandQueue*      copyQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC cd{};
	cd.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	if (device) device->CreateCommandQueue(&cd, IID_PPV_ARGS(&copyQueue));
	if (!factory0 || !device || !queue || !copyQueue || !windowA || !windowB) {
		std::printf("FAIL: no D3D12 device / queue / window to test with\n");
		return 1;
	}

	// (b) setup: a swap chain made BEFORE any hook - no creation hook can have seen it - and a proxy wrapped round it
	IDXGISwapChain3* s0 = MakeSwapChain(factory0, queue, windowA);
	Check(s0 != nullptr, "a swap chain is made before any hook exists");
	if (!s0) return 1;
	auto* w0 = new ProxySwapChain<0>(s0);

	// AMF-style install: dxgi's CreateDXGIFactory2 export hooked, then the start-up probe (here on the main thread, as no
	// loader lock is held in a test)
	const bool exportHooked = MH_CreateHook(reinterpret_cast<void*>(createFactory2), reinterpret_cast<void*>(&hk_CreateDXGIFactory2),
								  reinterpret_cast<void**>(&o_CreateDXGIFactory2)) == MH_OK &&
	                          MH_EnableHook(reinterpret_cast<void*>(createFactory2)) == MH_OK;
	Check(exportHooked, "dxgi!CreateDXGIFactory2 is hooked");
	t_probe = true;
	const presenthooks::ProbeResult probe = presenthooks::RunProbe(o_CreateDXGIFactory2, &ProbeOnFactory, &ProbeOnSwapChain);
	t_probe = false;
	Check(probe.ok, "the start-up probe ran: " + probe.summary);
	Check(h_Present.Count() == 1 && presenthooks::InSystemDir(h_Present.At(0).target) && h_Present.At(0).module == "dxgi.dll",
		std::format("the probe hooked dxgi's own Present body ({} class(es), in {})", h_Present.Count(), h_Present.Count() ? h_Present.At(0).module : "-"));
	Check(h_Present1.Count() == 1 && h_ResizeBuffers.Count() == 1 && h_SetColorSpace1.Count() == 1,
		"the probe hooked Present1, ResizeBuffers and SetColorSpace1 too");
	Check(h_CreateSwapChainForHwnd.Count() == 1, "the probe hooked dxgi's factory CreateSwapChainForHwnd (later creations are seen)");
	Check(g_created == 0 && g_capturedQueue == nullptr, "the probe's own swap chain was NOT recorded as the game's (no creation, no queue)");

	// (b) frames through the proxy: Streamline / ReShade style wrapper -> real Present, caught at dxgi's body by the probe
	std::string note;
	bool        fok = false;
	fok = Frames(w0, 3, s0, "dxgi-level Present", note);
	Check(fok && g_proxyPresents[0] == 3,
		"(b) a swap chain no creation hook saw, presented through a proxy, is caught at dxgi's Present: " + note);
	{
		// the queue for a swap chain whose creation was not seen: GetDevice first, then the presenting thread's DIRECT submissions
		const char*         from = nullptr;
		ID3D12CommandQueue* direct = presenthooks::QueueForSwapChain(w0, nullptr, &from);
		std::printf("INFO: GetDevice(ID3D12CommandQueue) on a D3D12 swap chain %s on this Windows\n",
			direct ? "gives the queue" : "gives NO queue (E_NOINTERFACE: dxgi hands back the device)");
		if (direct) {
			Check(SameObject(direct, queue), "(b) the queue GetDevice gave is the one the swap chain presents on");
			direct->Release();
		}
		Check(WatchQueues(w0), "(b) ExecuteCommandLists is watched through a queue of our own on the swap chain's device");
		g_wantQueue = true;
		Submit(copyQueue);   // a COPY queue's submission is not a candidate
		Check(t_lastDirectQueue == nullptr, "(b) a COPY queue's submission is not taken");
		Submit(queue);       // the frame's own submission on the presenting queue, on this (the presenting) thread
		ID3D12CommandQueue* found = presenthooks::QueueForSwapChain(w0, t_lastDirectQueue, &from);
		Check(found && SameObject(found, queue),
			std::string("(b) the queue is found from the presenting thread's DIRECT submission: ") + (found ? from : "none"));
		if (found) found->Release();
		g_wantQueue = false;
	}
	{
		g_draws = 0;
		DXGI_PRESENT_PARAMETERS pp{};
		const HRESULT hr = w0->Present1(0, 0, &pp);
		Check(SUCCEEDED(hr) && g_draws == 1 && h_Present1.At(0).calls.load() >= 1, "(b) Present1 through the proxy is caught at dxgi's Present1");
	}

	// (a) a plain dxgi swap chain made through the hooked CreateDXGIFactory2: the creation hook sees it
	IDXGIFactory2* factory1 = nullptr;
	createFactory2(0, IID_PPV_ARGS(&factory1));   // the export, now hooked
	IDXGISwapChain3* s1 = factory1 ? MakeSwapChain(factory1, queue, windowB) : nullptr;
	Check(s1 && g_created == 1 && SameObject(g_capturedQueue, queue), "(a) the creation hook saw the swap chain and captured its queue");
	if (!s1) return 1;
	Check(h_Present.Count() == 1 && (h_Present.At(0).seenBy.load() & presenthooks::kCreation) && (h_Present.At(0).seenBy.load() & presenthooks::kProbe),
		"(a) the same dxgi Present body is ONE entry, found by the probe and the creation hook");
	const auto callsBefore = h_Present.At(0).calls.load();
	fok = Frames(s1, 3, s1, "dxgi-level Present", note);
	Check(fok && h_Present.At(0).calls.load() == callsBefore + 3,
		"(a) a plain dxgi swap chain's Present is caught: " + note);
	Check(SUCCEEDED(s1->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0)) && h_ResizeBuffers.At(0).calls.load() >= 1, "(a) ResizeBuffers is caught");
	Check(SUCCEEDED(s1->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)) && h_SetColorSpace1.At(0).calls.load() >= 1,
		"(a) SetColorSpace1 is caught");

	// (c) a second class appearing after the first: the proxy class, as a creation hook would meet ReShade's wrapper
	HookSwapChain(w0, presenthooks::kCreation);
	Check(h_Present.Count() == 2 && h_Present1.Count() == 2, std::format("(c) a second class is hooked, not refused ({} Present classes: {}, {})",
																 h_Present.Count(), h_Present.At(0).module, h_Present.Count() > 1 ? h_Present.At(1).module : "-"));
	const auto innerBefore = h_Present.At(0).calls.load();
	fok = Frames(w0, 3, w0, "dxgi-level Present", note);
	Check(fok && h_Present.At(1).calls.load() == 3 && h_Present.At(0).calls.load() == innerBefore + 3,
		"(c) wrapper and real Present both hooked, nested: drawn once per frame, at the outer (wrapper) one: " + note);
	auto* w2 = new ProxySwapChain<1>(s1);
	HookSwapChain(w2, presenthooks::kCreation);
	Check(h_Present.Count() == 3, "(c) a third class is hooked too");
	fok = Frames(w2, 3, w2, "dxgi-level Present", note);
	Check(fok && g_proxyPresents[1] == 3, "(c) the third class's frames are caught: " + note);

	// game-facing slot writes, more than one class (1.0.5 redirected only the first)
	const int slotA = h_OuterPresent.Write(w0);
	const int slotB = h_OuterPresent.Write(w2);
	const int slotA2 = h_OuterPresent.Write(w0);
	Check(slotA == 0 && slotB == 1 && slotA2 == 0 && h_OuterPresent.Count() == 2,
		std::format("game-facing: two classes redirected, a repeat write reuses its entry ({}, {}, {})", slotA, slotB, slotA2));
	const int proxy0Before = g_proxyPresents[0];
	fok = Frames(w0, 3, w0, "game-facing slot", note);
	Check(fok && g_proxyPresents[0] == proxy0Before + 3,
		"game-facing class 1: drawn once per frame at the slot, forwarded through the proxy to dxgi: " + note);
	fok = Frames(w2, 3, w2, "game-facing slot", note);
	Check(fok, "game-facing class 2: " + note);

	// a game that takes dxgi straight from dxgi.dll: its game-facing slot is on dxgi's own vtable; a later dxgi-level hook
	// reads our detour from that slot and must hook what it forwards to, never our code
	const int slotDxgi = h_OuterPresent.Write(s1);
	const std::size_t classesBefore = h_Present.Count();
	HookSwapChain(s1, presenthooks::kCreation);
	Check(slotDxgi == 2 && h_Present.Count() == classesBefore,
		"a dxgi-level hook that reads our game-facing detour from the slot resolves it to dxgi's body (no new class, no hook on our own code)");
	fok = Frames(s1, 3, s1, "game-facing slot", note);
	Check(fok, "dxgi's own class with a game-facing slot: " + note);

	MH_DisableHook(MH_ALL_HOOKS);
	w2->Release();
	w0->Release();
	s1->Release();
	s0->Release();
	if (factory1) factory1->Release();
	if (g_capturedQueue) g_capturedQueue->Release();
	copyQueue->Release();
	queue->Release();
	device->Release();
	factory0->Release();
	DestroyWindow(windowA);
	DestroyWindow(windowB);
	MH_Uninitialize();
	std::printf("%s - %d check(s) failed\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
	return g_failures ? 1 : 0;
}
