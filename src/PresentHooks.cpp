// The hook tables and the start-up probe - see PresentHooks.h.
#include "PresentHooks.h"

#include "Logger.h"

#include <TlHelp32.h>

#include <cwchar>
#include <filesystem>
#include <vector>

namespace presenthooks
{
	namespace
	{
		// constant-initialised: SlotSet globals in other files register here during their dynamic initialisation
		void* (*g_resolvers[16])(void*) = {};
		std::atomic<std::size_t> g_resolverCount{ 0 };

		std::wstring SystemDir()
		{
			wchar_t sys[MAX_PATH]{};
			const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
			return (n > 0 && n < MAX_PATH) ? std::wstring(sys, n) : std::wstring();
		}

		std::wstring ModulePath(HMODULE a_module)
		{
			wchar_t file[MAX_PATH]{};
			const DWORD n = a_module ? GetModuleFileNameW(a_module, file, MAX_PATH) : 0;
			return (n > 0 && n < MAX_PATH) ? std::wstring(file, n) : std::wstring();
		}

		HMODULE ModuleAt(const void* a_address)
		{
			HMODULE m = nullptr;
			if (a_address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								 static_cast<LPCWSTR>(a_address), &m)) {
				return m;
			}
			return nullptr;
		}

		bool SameFolder(const std::filesystem::path& a_file, const std::wstring& a_folder)
		{
			return !a_folder.empty() && _wcsicmp(a_file.parent_path().wstring().c_str(), a_folder.c_str()) == 0;
		}

		std::string Narrow(const std::wstring& a_text)
		{
			if (a_text.empty()) {
				return {};
			}
			const int n = WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
			if (n > 0) {
				WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), out.data(), n, nullptr, nullptr);
			}
			return out;
		}

		// version.dll's readers, from System32's copy by full path: no import of ours binds to a version.dll proxy beside the
		// exe (an ASI loader is often named version.dll), and the .asi gains no load-time dependency for a log line.
		struct VersionApi
		{
			using Size_t = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
			using Info_t = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
			using Query_t = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
			Size_t  size = nullptr;
			Info_t  info = nullptr;
			Query_t query = nullptr;
		};

		const VersionApi& Version()
		{
			static const VersionApi api = [] {
				VersionApi a;
				const std::wstring sys = SystemDir();
				if (const HMODULE m = sys.empty() ? nullptr : LoadLibraryW((sys + L"\\version.dll").c_str())) {
					a.size = reinterpret_cast<VersionApi::Size_t>(GetProcAddress(m, "GetFileVersionInfoSizeW"));
					a.info = reinterpret_cast<VersionApi::Info_t>(GetProcAddress(m, "GetFileVersionInfoW"));
					a.query = reinterpret_cast<VersionApi::Query_t>(GetProcAddress(m, "VerQueryValueW"));
				}
				return a;
			}();
			return api;
		}

		// "ProductName FileVersion" from a file's version resource ("ReShade 6.3.3.1"), or "no version resource".
		std::string VersionOf(const std::wstring& a_file)
		{
			const VersionApi& v = Version();
			if (!v.size || !v.info || !v.query) {
				return "version resource not readable (System32's version.dll did not load)";
			}
			DWORD      handle = 0;
			const DWORD size = v.size(a_file.c_str(), &handle);
			if (size == 0) {
				return "no version resource";
			}
			std::vector<std::uint8_t> data(size);
			if (!v.info(a_file.c_str(), 0, size, data.data())) {
				return "no version resource";
			}
			struct Lang
			{
				WORD language, codePage;
			};
			Lang* langs = nullptr;
			UINT  langBytes = 0;
			std::wstring prefix = L"\\StringFileInfo\\040904b0\\";
			if (v.query(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &langBytes) && langs &&
				langBytes >= sizeof(Lang)) {
				wchar_t buf[64]{};
				swprintf_s(buf, L"\\StringFileInfo\\%04x%04x\\", langs[0].language, langs[0].codePage);
				prefix = buf;
			}
			auto field = [&](const wchar_t* a_name) {
				wchar_t* value = nullptr;
				UINT     len = 0;
				const std::wstring key = prefix + a_name;
				if (v.query(data.data(), key.c_str(), reinterpret_cast<void**>(&value), &len) && value && len > 0) {
					return Narrow(std::wstring(value, wcsnlen(value, len)));
				}
				return std::string();
			};
			std::string product = field(L"ProductName");
			std::string version = field(L"FileVersion");
			if (product.empty() && version.empty()) {
				return "no product name or version";
			}
			return (product.empty() ? std::string("(no product name)") : product) + " " + (version.empty() ? std::string("(no version)") : version);
		}

		bool GraphicsName(std::wstring a_name)
		{
			for (auto& c : a_name) {
				c = static_cast<wchar_t>(towlower(c));
			}
			return a_name.find(L"dxgi") != std::wstring::npos || a_name.find(L"d3d12") != std::wstring::npos ||
			       a_name.find(L"d3d11") != std::wstring::npos || a_name.find(L"reshade") != std::wstring::npos;
		}
	}

	const char* SourceName(int a_bits)
	{
		if (a_bits & kCreation) return "the creation hook";
		if (a_bits & kProbe) return "the start-up probe";
		if (a_bits & kGameFacing) return "the game-facing slot";
		if (a_bits & kQueueHook) return "the queue fallback";
		return "?";
	}

	ID3D12CommandQueue* QueueForSwapChain(IDXGISwapChain* a_swapChain, ID3D12CommandQueue* a_candidate, const char** a_from)
	{
		if (!a_swapChain) {
			return nullptr;
		}
		ID3D12CommandQueue* q = nullptr;
		if (SUCCEEDED(a_swapChain->GetDevice(IID_PPV_ARGS(&q))) && q) {
			if (a_from) *a_from = "the swap chain (GetDevice)";
			return q;
		}
		if (!a_candidate) {
			return nullptr;
		}
		ID3D12Device* scDevice = nullptr;
		ID3D12Device* qDevice = nullptr;
		IUnknown*     a = nullptr;
		IUnknown*     b = nullptr;
		bool          ok = false;
		if (SUCCEEDED(a_swapChain->GetDevice(IID_PPV_ARGS(&scDevice))) && scDevice && SUCCEEDED(a_candidate->GetDevice(IID_PPV_ARGS(&qDevice))) &&
			qDevice && SUCCEEDED(scDevice->QueryInterface(IID_PPV_ARGS(&a))) && SUCCEEDED(qDevice->QueryInterface(IID_PPV_ARGS(&b)))) {
			ok = a == b && a_candidate->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT;
		}
		for (IUnknown* p : { static_cast<IUnknown*>(scDevice), static_cast<IUnknown*>(qDevice), a, b }) {
			if (p) p->Release();
		}
		if (!ok) {
			return nullptr;
		}
		a_candidate->AddRef();
		if (a_from) *a_from = "the presenting thread's last DIRECT ExecuteCommandLists";
		return a_candidate;
	}

	std::string ModuleOf(const void* a_address)
	{
		const std::wstring path = ModulePath(ModuleAt(a_address));
		return path.empty() ? std::string("?") : Narrow(std::filesystem::path(path).filename().wstring());
	}

	bool InSystemDir(const void* a_address)
	{
		const std::wstring path = ModulePath(ModuleAt(a_address));
		return !path.empty() && SameFolder(path, SystemDir());
	}

	void RegisterResolver(void* (*a_resolve)(void*))
	{
		const std::size_t n = g_resolverCount.load();
		if (n < std::size(g_resolvers)) {
			g_resolvers[n] = a_resolve;
			g_resolverCount.store(n + 1, std::memory_order_release);
		}
	}

	void* ResolveOwnDetour(void* a_function)
	{
		const std::size_t n = g_resolverCount.load(std::memory_order_acquire);
		for (std::size_t i = 0; i < n; ++i) {
			if (void* p = g_resolvers[i](a_function)) {
				return p;
			}
		}
		return nullptr;
	}

	void LogHooked(const char* a_what, std::size_t a_entry, void* a_target, const std::string& a_module, int a_source, bool a_slot)
	{
		if (a_slot) {
			logger::info("game-facing {}: vtable slot redirected to class {} (was {} in {})", a_what, a_entry + 1, a_target, a_module);
		} else {
			logger::info("hooked {} class {} at {} in {} (found by {}){}", a_what, a_entry + 1, a_target, a_module, SourceName(a_source),
				a_entry > 0 ? " - a further class: frames through it are caught too" : "");
		}
	}

	void LogHookFailed(const char* a_what, void* a_target, const char* a_why)
	{
		logger::error("hook {}{}{}: {}", a_what, a_target ? " at " : "", a_target ? std::format("{}", a_target) : std::string(), a_why);
	}

	// ---- the start-up probe ----------------------------------------------------------------------------------------------
	namespace
	{
		using D3D12CreateDevice_t = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);

		struct ProbeCtx
		{
			CreateFactory2_t createFactory2 = nullptr;
			void (*onFactory)(IUnknown*) = nullptr;
			void (*onSwapChain)(IUnknown*) = nullptr;
			ProbeResult* result = nullptr;
			// released by the caller of the guarded body, so a fault half-way still frees what was made
			IDXGIFactory2*      factory = nullptr;
			IDXGIAdapter1*      adapter = nullptr;
			ID3D12Device*       device = nullptr;
			ID3D12CommandQueue* queue = nullptr;
			IDXGISwapChain1*    swapChain = nullptr;
			HWND                window = nullptr;
			ATOM                windowClass = 0;
		};

		constexpr wchar_t kWindowClass[] = L"AMF_PresentProbe";

		void* Slot(void* a_object, std::size_t a_index)
		{
			auto** vtbl = a_object ? *reinterpret_cast<void***>(a_object) : nullptr;
			return vtbl ? vtbl[a_index] : nullptr;
		}

		void ProbeBody(ProbeCtx* a_ctx)
		{
			ProbeResult& r = *a_ctx->result;
			const std::wstring sys = SystemDir();
			if (sys.empty()) {
				r.summary = "no system folder";
				return;
			}
			// System32's modules by full path - never a proxy beside the exe, never AMF's own (delay-loaded) imports
			const HMODULE d3d12 = LoadLibraryW((sys + L"\\d3d12.dll").c_str());
			const HMODULE dxgi = LoadLibraryW((sys + L"\\dxgi.dll").c_str());
			if (!d3d12 || !dxgi) {
				r.summary = std::format("System32's {} did not load ({})", d3d12 ? "dxgi.dll" : "d3d12.dll", GetLastError());
				return;
			}
			const auto createDevice = reinterpret_cast<D3D12CreateDevice_t>(GetProcAddress(d3d12, "D3D12CreateDevice"));
			CreateFactory2_t createFactory = a_ctx->createFactory2;
			if (!createFactory) {
				createFactory = reinterpret_cast<CreateFactory2_t>(GetProcAddress(dxgi, "CreateDXGIFactory2"));
			}
			if (!createDevice || !createFactory) {
				r.summary = "D3D12CreateDevice or CreateDXGIFactory2 is missing from System32's modules";
				return;
			}
			HRESULT hr = createFactory(0, IID_PPV_ARGS(&a_ctx->factory));
			if (FAILED(hr) || !a_ctx->factory) {
				r.summary = std::format("CreateDXGIFactory2 failed (0x{:08X})", static_cast<unsigned>(hr));
				return;
			}
			// the first hardware adapter (the swap chain's class is dxgi's whatever the adapter; no software device)
			for (UINT i = 0; a_ctx->factory->EnumAdapters1(i, &a_ctx->adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
				DXGI_ADAPTER_DESC1 ad{};
				if (a_ctx->adapter && SUCCEEDED(a_ctx->adapter->GetDesc1(&ad)) && !(ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
					break;
				}
				if (a_ctx->adapter) {
					a_ctx->adapter->Release();
					a_ctx->adapter = nullptr;
				}
			}
			hr = createDevice(a_ctx->adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&a_ctx->device));
			if (FAILED(hr) || !a_ctx->device) {
				r.summary = std::format("D3D12CreateDevice failed (0x{:08X})", static_cast<unsigned>(hr));
				return;
			}
			D3D12_COMMAND_QUEUE_DESC qd{};
			qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			hr = a_ctx->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&a_ctx->queue));
			if (FAILED(hr) || !a_ctx->queue) {
				r.summary = std::format("CreateCommandQueue failed (0x{:08X})", static_cast<unsigned>(hr));
				return;
			}
			WNDCLASSEXW wc{};
			wc.cbSize = sizeof(wc);
			wc.lpfnWndProc = DefWindowProcW;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.lpszClassName = kWindowClass;
			a_ctx->windowClass = RegisterClassExW(&wc);
			// never shown (no WS_VISIBLE), never in the taskbar, destroyed before the probe returns
			a_ctx->window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kWindowClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
				wc.hInstance, nullptr);
			if (!a_ctx->window) {
				r.summary = std::format("the hidden window could not be made ({})", GetLastError());
				return;
			}
			if (a_ctx->onFactory) {
				a_ctx->onFactory(a_ctx->factory);
			}
			DXGI_SWAP_CHAIN_DESC1 sd{};
			sd.Width = 1;
			sd.Height = 1;
			sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.SampleDesc.Count = 1;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.BufferCount = 2;
			sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			hr = a_ctx->factory->CreateSwapChainForHwnd(a_ctx->queue, a_ctx->window, &sd, nullptr, nullptr, &a_ctx->swapChain);
			if (FAILED(hr) || !a_ctx->swapChain) {
				r.summary = std::format("CreateSwapChainForHwnd failed (0x{:08X})", static_cast<unsigned>(hr));
				return;
			}
			r.present = Slot(a_ctx->swapChain, 8);
			r.present1 = Slot(a_ctx->swapChain, 22);
			r.resizeBuffers = Slot(a_ctx->swapChain, 13);
			IDXGISwapChain3* sc3 = nullptr;
			if (SUCCEEDED(a_ctx->swapChain->QueryInterface(IID_PPV_ARGS(&sc3))) && sc3) {
				r.setColorSpace1 = Slot(sc3, 38);
				sc3->Release();
			}
			// dxgi.dll means the real body; anything else names whatever stands in the way (a wrapper, or a slot redirect)
			const auto where = [](void* a_fn) {
				return !a_fn ? std::string("(empty)") : std::format("{} in {}{}", a_fn, ModuleOf(a_fn), InSystemDir(a_fn) ? "" : " - NOT System32's");
			};
			logger::info("start-up probe: a real dxgi swap chain's Present is {}, Present1 {}, ResizeBuffers {}, SetColorSpace1 {}",
				where(r.present), where(r.present1), where(r.resizeBuffers), where(r.setColorSpace1));
			if (a_ctx->onSwapChain) {
				a_ctx->onSwapChain(a_ctx->swapChain);
			}
			r.ok = r.present != nullptr;
			r.summary = r.ok ? std::format("ok: Present in {}", ModuleOf(r.present)) : std::string("the swap chain's Present slot is empty");
		}

		// No C++ object lives in this frame, so it may hold the structured-exception guard: a driver fault inside the
		// throwaway device's creation is logged and AMF carries on with the hooks it already has.
		bool GuardedProbe(ProbeCtx* a_ctx, DWORD* a_code)
		{
			__try {
				ProbeBody(a_ctx);
				return true;
			} __except (*a_code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		template <class T>
		void Drop(T*& a_p)
		{
			if (a_p) {
				a_p->Release();
				a_p = nullptr;
			}
		}
	}

	ProbeResult RunProbe(CreateFactory2_t a_createFactory2, void (*a_onFactory)(IUnknown*), void (*a_onSwapChain)(IUnknown*))
	{
		ProbeResult r;
		ProbeCtx    ctx;
		ctx.createFactory2 = a_createFactory2;
		ctx.onFactory = a_onFactory;
		ctx.onSwapChain = a_onSwapChain;
		ctx.result = &r;
		const ULONGLONG t0 = GetTickCount64();
		DWORD           code = 0;
		if (!GuardedProbe(&ctx, &code)) {
			r.ok = false;
			r.summary = std::format("faulted (exception 0x{:08X}) - the hooks already installed stay", static_cast<unsigned>(code));
		}
		// swap chain before its window, the device last
		Drop(ctx.swapChain);
		Drop(ctx.queue);
		Drop(ctx.device);
		Drop(ctx.adapter);
		Drop(ctx.factory);
		if (ctx.window) {
			DestroyWindow(ctx.window);
		}
		if (ctx.windowClass) {
			UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
		}
		if (r.ok) {
			logger::info("start-up probe {} ({} ms; its device, queue, swap chain and window are released)", r.summary, GetTickCount64() - t0);
		} else {
			logger::warn("start-up probe did not finish: {} - AMF relies on its creation hooks alone, as before 1.0.6", r.summary);
		}
		return r;
	}

	void LogGraphicsModules(const char* a_when)
	{
		const std::wstring sys = SystemDir();
		wchar_t exeBuf[MAX_PATH]{};
		GetModuleFileNameW(nullptr, exeBuf, MAX_PATH);
		const std::wstring exeDir = std::filesystem::path(exeBuf).parent_path().wstring();

		int          outside = 0;
		const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
		if (snap != INVALID_HANDLE_VALUE) {
			MODULEENTRY32W me{};
			me.dwSize = sizeof(me);
			for (BOOL more = Module32FirstW(snap, &me); more; more = Module32NextW(snap, &me)) {
				if (!GraphicsName(me.szModule)) {
					continue;
				}
				const std::filesystem::path file(me.szExePath);
				if (SameFolder(file, sys)) {
					continue;
				}
				std::wstring lower = me.szModule;
				for (auto& c : lower) {
					c = static_cast<wchar_t>(towlower(c));
				}
				const char* where = SameFolder(file, exeDir) ? "in the game's exe folder" : "elsewhere";
				if (lower.starts_with(L"d3d12core") || lower.starts_with(L"d3d12sdklayers")) {
					// the D3D12 Agility SDK runtime a game ships in its own folder: expected, not a proxy
					logger::info("{}: D3D12 runtime from outside System32: {} ({}; {})", a_when, Narrow(file.filename().wstring()), where,
						VersionOf(file.wstring()));
					continue;
				}
				++outside;
				logger::warn("{}: graphics module from outside System32: {} ({}; {}) - it can stand between the game and dxgi", a_when,
					Narrow(file.filename().wstring()), where, VersionOf(file.wstring()));
			}
			CloseHandle(snap);
		} else {
			logger::warn("{}: the module list could not be read ({})", a_when, GetLastError());
		}
		if (outside == 0) {
			logger::info("{}: no dxgi / d3d12 / d3d11 / ReShade module is loaded from outside System32 (no proxy loaded)", a_when);
		}
		for (const wchar_t* name : { L"dxgi.dll", L"d3d12.dll" }) {
			const std::filesystem::path file = std::filesystem::path(exeDir) / name;
			std::error_code ec;
			if (std::filesystem::exists(file, ec)) {
				logger::warn("{}: a {} file sits in the game's exe folder ({}){}", a_when, Narrow(name), VersionOf(file.wstring()),
					GetModuleHandleW(file.wstring().c_str()) ? ", loaded" : ", not loaded yet");
			} else {
				logger::info("{}: no {} in the game's exe folder", a_when, Narrow(name));
			}
		}
	}
}
