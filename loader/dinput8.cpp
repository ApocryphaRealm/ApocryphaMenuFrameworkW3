// AMFLoader - a minimal ASI loader for The Witcher 3 Remastered, as bin\x64_dx12\dinput8.dll. Copyright (C) 2026
// ApocryphaRealm. GPL-3.0-or-later.
//
// witcher3.exe 5.0 statically imports DINPUT8.dll (DirectInput8Create) and dinput8 is not a KnownDLL, so this file beside
// the exe is loaded at process start. It does two things:
//   1. forwards every dinput8 export to the SYSTEM dinput8.dll (loaded by full path on first use);
//   2. at the game's entry point - after the Windows loader has finished and released the loader lock - loads every
//      *.asi in its own folder, then runs the game's real entry point.
// That is the Ultimate ASI Loader's contract (MIT, ThirteenAG), written from its documented behaviour, so either loader
// runs Apocrypha Menu Framework; a player who already has the Ultimate ASI Loader from another mod keeps it.

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <Unknwn.h>

#include <share.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
	HMODULE g_self = nullptr;
	HMODULE g_real = nullptr;
	std::FILE* g_log = nullptr;

	void Log(const char* a_fmt, const char* a_arg = "")
	{
		if (!g_log) { return; }
		SYSTEMTIME t{};
		::GetLocalTime(&t);
		std::fprintf(g_log, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
		std::fprintf(g_log, a_fmt, a_arg);
		std::fputc('\n', g_log);
		std::fflush(g_log);
	}

	std::wstring SelfFolder()
	{
		wchar_t path[MAX_PATH]{};
		const DWORD n = ::GetModuleFileNameW(g_self, path, MAX_PATH);
		std::wstring s(path, n);
		const auto slash = s.find_last_of(L"\\/");
		return slash == std::wstring::npos ? L"." : s.substr(0, slash);
	}

	HMODULE Real()
	{
		if (!g_real) {
			wchar_t sys[MAX_PATH]{};
			const UINT n = ::GetSystemDirectoryW(sys, MAX_PATH);
			g_real = ::LoadLibraryW((std::wstring(sys, n) + L"\\dinput8.dll").c_str());
		}
		return g_real;
	}

	template <class T>
	T RealProc(const char* a_name)
	{
		const HMODULE m = Real();
		return m ? reinterpret_cast<T>(::GetProcAddress(m, a_name)) : nullptr;
	}

	void LoadAsis()
	{
		const std::wstring folder = SelfFolder();
		WIN32_FIND_DATAW fd{};
		const HANDLE h = ::FindFirstFileW((folder + L"\\*.asi").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) {
			Log("no .asi files beside the loader");
			return;
		}
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { continue; }
			const std::wstring path = folder + L"\\" + fd.cFileName;
			char narrow[MAX_PATH]{};
			::WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, narrow, MAX_PATH, nullptr, nullptr);
			const HMODULE m = ::LoadLibraryW(path.c_str());
			Log(m ? "loaded %s" : "FAILED to load %s", narrow);
		} while (::FindNextFileW(h, &fd));
		::FindClose(h);
	}

	// ---- the entry-point detour: 14-byte absolute jump written over the exe's entry, restored before it runs ----------
	std::uint8_t g_saved[14]{};
	std::uint8_t* g_entry = nullptr;

	void Restore()
	{
		DWORD old = 0;
		::VirtualProtect(g_entry, sizeof(g_saved), PAGE_EXECUTE_READWRITE, &old);
		std::memcpy(g_entry, g_saved, sizeof(g_saved));
		::VirtualProtect(g_entry, sizeof(g_saved), old, &old);
		::FlushInstructionCache(::GetCurrentProcess(), g_entry, sizeof(g_saved));
	}

	// the entry point is called with one argument (the PEB pointer) - passed through untouched
	int EntryDetour(void* a_arg)
	{
		Restore();
		Log("game entry point reached - loading .asi plugins");
		LoadAsis();
		using Entry_t = int (*)(void*);
		return reinterpret_cast<Entry_t>(g_entry)(a_arg);
	}

	bool HookEntry()
	{
		auto* base = reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));
		auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
		g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
		std::memcpy(g_saved, g_entry, sizeof(g_saved));
		std::uint8_t jump[14] = { 0xFF, 0x25, 0, 0, 0, 0 };   // jmp [rip+0] ; then the 8-byte target
		const auto target = reinterpret_cast<std::uintptr_t>(&EntryDetour);
		std::memcpy(jump + 6, &target, sizeof(target));
		DWORD old = 0;
		if (!::VirtualProtect(g_entry, sizeof(jump), PAGE_EXECUTE_READWRITE, &old)) { return false; }
		std::memcpy(g_entry, jump, sizeof(jump));
		::VirtualProtect(g_entry, sizeof(jump), old, &old);
		::FlushInstructionCache(::GetCurrentProcess(), g_entry, sizeof(jump));
		return true;
	}
}

extern "C"
{
	HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE a, DWORD b, REFIID c, LPVOID* d, LPUNKNOWN e)
	{
		using F = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
		const auto f = RealProc<F>("DirectInput8Create");
		return f ? f(a, b, c, d, e) : E_FAIL;
	}
	HRESULT WINAPI Proxy_DllCanUnloadNow()
	{
		using F = HRESULT(WINAPI*)();
		const auto f = RealProc<F>("DllCanUnloadNow");
		return f ? f() : S_FALSE;
	}
	HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID a, REFIID b, LPVOID* c)
	{
		using F = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
		const auto f = RealProc<F>("DllGetClassObject");
		return f ? f(a, b, c) : CLASS_E_CLASSNOTAVAILABLE;
	}
	HRESULT WINAPI Proxy_DllRegisterServer()
	{
		using F = HRESULT(WINAPI*)();
		const auto f = RealProc<F>("DllRegisterServer");
		return f ? f() : E_FAIL;
	}
	HRESULT WINAPI Proxy_DllUnregisterServer()
	{
		using F = HRESULT(WINAPI*)();
		const auto f = RealProc<F>("DllUnregisterServer");
		return f ? f() : E_FAIL;
	}
	const void* WINAPI Proxy_GetdfDIJoystick()
	{
		using F = const void*(WINAPI*)();
		const auto f = RealProc<F>("GetdfDIJoystick");
		return f ? f() : nullptr;
	}
}

BOOL APIENTRY DllMain(HMODULE a_module, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH) {
		g_self = a_module;
		::DisableThreadLibraryCalls(a_module);
		const std::wstring logPath = SelfFolder() + L"\\AMFLoader.log";
		g_log = ::_wfsopen(logPath.c_str(), L"w", _SH_DENYWR);
		Log("AMFLoader (dinput8.dll proxy) attached");
		if (!HookEntry()) {
			Log("could not hook the entry point - loading .asi plugins now instead");
			LoadAsis();
		}
	}
	return TRUE;
}
