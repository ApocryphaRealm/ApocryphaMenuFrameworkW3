#include "Probe.h"

#include <MinHook.h>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

namespace probe
{
	namespace
	{
		using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
		using RegisterRawInputDevices_t = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
		using XInputGetState_t = DWORD(WINAPI*)(DWORD, void*);
		using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8W*, REFGUID, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);

		DirectInput8Create_t      o_DirectInput8Create = nullptr;
		RegisterRawInputDevices_t o_RegisterRawInputDevices = nullptr;
		XInputGetState_t          o_XInputGetState = nullptr;
		CreateDevice_t            o_CreateDevice = nullptr;
		std::atomic<int>          g_xinputCalls{ 0 };
		std::atomic_bool          g_xinputConnectedLogged{ false };

		// The exe's import slot for a_dll!a_func (by name; the DLL name compared case-insensitively). Null when absent.
		void** FindImport(const char* a_dll, const char* a_func)
		{
			auto* base = reinterpret_cast<std::uint8_t*>(::GetModuleHandleW(nullptr));
			auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
			auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (!dir.VirtualAddress) { return nullptr; }
			for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
				if (::_stricmp(reinterpret_cast<const char*>(base + imp->Name), a_dll) != 0) { continue; }
				auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
				for (; names->u1.AddressOfData; ++names, ++slots) {
					if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) { continue; }
					const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
					if (std::strcmp(byName->Name, a_func) == 0) { return reinterpret_cast<void**>(&slots->u1.Function); }
				}
			}
			return nullptr;
		}

		template <class T>
		bool Patch(const char* a_dll, const char* a_func, T a_detour, T& a_original)
		{
			void** slot = FindImport(a_dll, a_func);
			if (!slot) {
				logger::info("probe: the exe does not import {}!{}", a_dll, a_func);
				return false;
			}
			DWORD old = 0;
			if (!::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { return false; }
			a_original = reinterpret_cast<T>(*slot);
			*slot = reinterpret_cast<void*>(a_detour);
			::VirtualProtect(slot, sizeof(void*), old, &old);
			logger::info("probe: watching the exe's {}!{} import (was {})", a_dll, a_func, reinterpret_cast<void*>(a_original));
			return true;
		}

		const char* DeviceName(REFGUID a_guid)
		{
			if (a_guid == GUID_SysKeyboard) { return "the system KEYBOARD"; }
			if (a_guid == GUID_SysMouse) { return "the system MOUSE"; }
			return "another device (joystick/pad)";
		}

		HRESULT STDMETHODCALLTYPE hk_CreateDevice(IDirectInput8W* a_self, REFGUID a_guid, LPDIRECTINPUTDEVICE8W* a_out, LPUNKNOWN a_outer)
		{
			const HRESULT hr = o_CreateDevice(a_self, a_guid, a_out, a_outer);
			logger::info("probe: DirectInput8 CreateDevice({}) -> 0x{:08X} - the game reads this through DirectInput", DeviceName(a_guid),
				static_cast<std::uint32_t>(hr));
			return hr;
		}

		HRESULT WINAPI hk_DirectInput8Create(HINSTANCE a_inst, DWORD a_ver, REFIID a_iid, LPVOID* a_out, LPUNKNOWN a_outer)
		{
			const HRESULT hr = o_DirectInput8Create(a_inst, a_ver, a_iid, a_out, a_outer);
			logger::info("probe: the game called DirectInput8Create (version 0x{:X}) -> 0x{:08X}", a_ver, static_cast<std::uint32_t>(hr));
			if (SUCCEEDED(hr) && a_out && *a_out && !o_CreateDevice) {
				void** vtable = *reinterpret_cast<void***>(*a_out);
				void* target = vtable[3];   // IDirectInput8W::CreateDevice
				if (MH_CreateHook(target, reinterpret_cast<void*>(&hk_CreateDevice), reinterpret_cast<void**>(&o_CreateDevice)) == MH_OK &&
					MH_EnableHook(target) == MH_OK) {
					logger::info("probe: watching IDirectInput8::CreateDevice");
				}
			}
			return hr;
		}

		BOOL WINAPI hk_RegisterRawInputDevices(PCRAWINPUTDEVICE a_devices, UINT a_count, UINT a_size)
		{
			for (UINT i = 0; a_devices && i < a_count; ++i) {
				const auto& d = a_devices[i];
				const char* what = d.usUsagePage == 1 && d.usUsage == 6 ? "KEYBOARD" : d.usUsagePage == 1 && d.usUsage == 2 ? "MOUSE" :
				                   d.usUsagePage == 1 && (d.usUsage == 4 || d.usUsage == 5) ? "joystick/pad" : "other";
				logger::info("probe: the game registered RawInput usage page {} usage {} ({}), flags 0x{:X}, window {}", d.usUsagePage, d.usUsage,
					what, d.dwFlags, static_cast<void*>(d.hwndTarget));
			}
			return o_RegisterRawInputDevices(a_devices, a_count, a_size);
		}

		DWORD WINAPI hk_XInputGetState(DWORD a_user, void* a_state)
		{
			const DWORD rc = o_XInputGetState(a_user, a_state);
			if (g_xinputCalls.fetch_add(1, std::memory_order_relaxed) == 0) {
				logger::info("probe: the game polls XInputGetState (first call: user {}, result {})", a_user, rc);
			}
			if (rc == ERROR_SUCCESS && !g_xinputConnectedLogged.exchange(true)) {
				logger::info("probe: XInputGetState reports a CONNECTED pad on user {} - the pad reaches the game through XInput", a_user);
			}
			return rc;
		}
	}

	void Install()
	{
		if (const auto st = MH_Initialize(); st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) {
			logger::warn("probe: MinHook did not initialise ({})", static_cast<int>(st));
		}
		Patch("DINPUT8.dll", "DirectInput8Create", &hk_DirectInput8Create, o_DirectInput8Create);
		Patch("USER32.dll", "RegisterRawInputDevices", &hk_RegisterRawInputDevices, o_RegisterRawInputDevices);
		Patch("XINPUT9_1_0.dll", "XInputGetState", &hk_XInputGetState, o_XInputGetState);
	}

	void Poll()
	{
		static bool s_gameInput = false, s_hid = false, s_streamline = false;
		if (!s_gameInput && (::GetModuleHandleW(L"GameInput.dll") || ::GetModuleHandleW(L"GameInputRedist.dll"))) {
			s_gameInput = true;
			logger::info("probe: GameInput.dll is loaded in the game - the GameInput pad path may be live");
		}
		if (!s_hid && ::GetModuleHandleW(L"hid.dll")) {
			s_hid = true;
			logger::info("probe: hid.dll is loaded (the Sony HID pad path is available)");
		}
		if (!s_streamline && ::GetModuleHandleW(L"sl.interposer.dll")) {
			s_streamline = true;
			logger::info("probe: NVIDIA Streamline (sl.interposer.dll) is loaded");
		}
	}
}
