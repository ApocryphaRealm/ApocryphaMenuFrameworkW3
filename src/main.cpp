// Apocrypha Menu Framework - The Witcher 3: Wild Hunt Remastered (patch 5.0, DX12). Copyright (C) 2026 ApocryphaRealm.
// GPL-3.0-or-later.
//
// The public contract (include/AMF/API.h) is the Skyrim AMF's, unchanged: a native mod written against it registers its
// pages here the same way. There is no script extender for Witcher 3 5.0: AMF is an .asi loaded by an ASI loader
// (bin\x64_dx12\dinput8.dll - the Ultimate ASI Loader or AMFLoader) and starts from DllMain (bottom of this file).

#include "ModMenus.h"
#include "Paths.h"
#include "Red3.h"
#include "AMF/API.h"
#include "Bindings.h"
#include "Input.h"
#include "Keyboard.h"
#include "Overlay.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Probe.h"
#include "SystemRow.h"
#include "Tick.h"

#include <imgui.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <cstdio>
#include <cstring>

namespace
{
	// The keys the framework consumes while its menu is open, as DirectInput scan codes (the numbering the whole
	// core uses): Tab, Escape, the arrows, Enter. The MENU key is not in this list - SMF_GetReservedKeyCodes puts the
	// LIVE menu key first at every call - the keyboard key of Controls' "Open and close the menu", the key the input
	// hook really opens on (bindings::ToggleKeyboardCode) - so a key the player moved AMF away from is free again.
	constexpr std::array<std::int32_t, 7> kNavigationKeys{ 0x0F, 0x01, 0xC8, 0xD0, 0xCB, 0xCD, 0x1C };
}

AMF_API std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity)
{
	std::array<std::int32_t, kNavigationKeys.size() + 1> reserved{};
	std::uint32_t count = 0;
	// The binding, not settings::Get().toggleKey: the two are kept equal (settings::Load / Save), but the binding is the
	// key that opens the menu, and a Controls-page rebind changes it the moment the key is pressed.
	const std::int32_t live = bindings::ToggleKeyboardCode();
	if (live > 0) {
		reserved[count++] = live;
	}
	for (const std::int32_t nav : kNavigationKeys) {
		if (nav != live) {
			reserved[count++] = nav;
		}
	}
	if (!a_buffer) {
		return count;   // null buffer = "how big a buffer do I need"
	}
	const std::uint32_t written = a_capacity < count ? a_capacity : count;
	for (std::uint32_t i = 0; i < written; ++i) {
		a_buffer[i] = reserved[i];
	}
	logger::debug("SMF_GetReservedKeyCodes: reported {} reserved key(s) to a caller (menu key 0x{:X} first)", written, live);
	return written;
}

AMF_API const char* AMF_GetVersionString()
{
	return AMF_VERSION;
}

AMF_API std::uint32_t AMF_GetAPIVersion()
{
	return 1;
}

AMF_API bool AMF_RegisterPage(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render)
{
	return registry::Register(a_modName, a_pageName, a_render);
}

AMF_API const char* AMF_GetLanguage()
{
	// A process-lifetime buffer: consumers may keep the pointer and compare each frame.
	static char s_buffer[64] = "english";
	const std::string& lang = strings::Language();
	if (std::strncmp(s_buffer, lang.c_str(), sizeof(s_buffer)) != 0) {
		std::snprintf(s_buffer, sizeof(s_buffer), "%s", lang.c_str());
	}
	return s_buffer;
}

AMF_API bool AMF_OpenMenu(const char* a_modName)
{
	// A consumer's own settings key opens the framework ON its page; an unknown or empty name opens the menu where
	// it last was. Returns whether the named mod was found.
	renderer::SetMenuVisible(true, false);
	if (!a_modName || !*a_modName) {
		return true;
	}
	const std::vector<registry::Entry> entries = registry::Snapshot();
	for (std::size_t i = 0; i < entries.size(); ++i) {
		if (entries[i].modName == a_modName) {
			renderer::SetSelectedNode("mod:" + std::to_string(i));
			logger::info("AMF_OpenMenu: opened on '{}' (registry index {})", a_modName, i);
			return true;
		}
	}
	logger::info("AMF_OpenMenu: '{}' is not registered; menu opened where it was", a_modName);
	return false;
}

AMF_API void AMF_CloseMenu()
{
	renderer::SetMenuVisible(false, false);
}

// 1.0.5: whether the framework's window is up - a mod that reads the controller itself (before the pad gate empties
// the game's reads) stands down on it while the menu is open.
AMF_API bool AMF_IsMenuOpen()
{
	return renderer::IsMainWindowVisible();
}

AMF_API bool AMF_DrawThemeFrame(void* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1)
{
	return renderer::DrawThemeFrameAround(static_cast<ImDrawList*>(a_drawList), a_x0, a_y0, a_x1, a_y1);
}

AMF_API void AMF_ShowKeyboard()
{
	keyboard::Show();
}

AMF_API void AMF_HideKeyboard()
{
	keyboard::Hide();
}

AMF_API void AMF_SetSticksCaptured(bool a_captured)
{
	input::SetSticksCaptured(a_captured);
}

AMF_API bool AMF_GetStick(int a_which, float* a_x, float* a_y, bool* a_clicked, bool* a_live)
{
	float x = 0.0f, y = 0.0f;
	bool  clicked = false, live = false;
	input::GetStick(a_which, x, y, clicked, live);
	if (a_x) { *a_x = x; }
	if (a_y) { *a_y = y; }
	if (a_clicked) { *a_clicked = clicked; }
	if (a_live) { *a_live = live; }
	return true;
}

// A consumer mod's bind button (2026-09-28, Ultimate Combat's rebuild): the next press on that side is recorded
// and swallowed, so the menu's own navigation does not act on it (Bindings.h, the consumer capture).
AMF_API void AMF_BeginKeyCapture(bool a_gamepadSide, std::int32_t a_timeoutMs)
{
	bindings::BeginConsumerCapture(a_gamepadSide, a_timeoutMs);
}

AMF_API void AMF_CancelKeyCapture()
{
	bindings::CancelConsumerCapture();
}

AMF_API std::int32_t AMF_PollKeyCapture(std::int32_t* a_kind, std::int32_t* a_code)
{
	return static_cast<std::int32_t>(bindings::PollConsumerCapture(a_kind, a_code));
}

AMF_API bool AMF_SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible)
{
	return registry::SetPageVisible(a_modName, a_pageName, a_visible);
}

AMF_API int AMF_DeclareInnerTabs(int a_count, int a_current)
{
	return renderer::DeclareInnerTabs(a_count, a_current);
}

AMF_API std::uint32_t AMF_GetInputMode()
{
	return input::UsingController() ? 1u : 0u;
}

// ---- Sharing the framework's Dear ImGui with a C++ consumer (Oblivion Remastered 0.1.0) ----------------------------
// A page draws inside the framework's frame, so it must use the framework's context. A consumer compiled against the
// same Dear ImGui (1.90.8 docking, obsolete functions kept) calls ImGui::SetCurrentContext / SetAllocatorFunctions
// with these and then uses the ordinary C++ API; AMF.h does it for them after AMF_CheckImGuiABI agrees. The cimgui
// ig* exports remain for C consumers and for anything built against a different ImGui.

AMF_API void* AMF_GetImGuiContext()
{
	return ImGui::GetCurrentContext();   // null until the renderer is up (first Present)
}

AMF_API bool AMF_GetImGuiAllocatorFunctions(void** a_alloc, void** a_free, void** a_userData)
{
	ImGuiMemAllocFunc alloc = nullptr;
	ImGuiMemFreeFunc  free = nullptr;
	void*             user = nullptr;
	ImGui::GetAllocatorFunctions(&alloc, &free, &user);
	if (a_alloc) { *a_alloc = reinterpret_cast<void*>(alloc); }
	if (a_free) { *a_free = reinterpret_cast<void*>(free); }
	if (a_userData) { *a_userData = user; }
	return alloc && free;
}

// The same layout test ImGui's own IMGUI_CHECKVERSION makes, answered for the framework's build: a consumer passes its
// IMGUI_VERSION and sizeof values; any difference means sharing the context would corrupt memory, so it must not.
AMF_API bool AMF_CheckImGuiABI(const char* a_version, std::size_t a_io, std::size_t a_style, std::size_t a_vec2,
	std::size_t a_vec4, std::size_t a_drawVert, std::size_t a_drawIdx)
{
	const bool ok = a_version && std::strcmp(a_version, IMGUI_VERSION) == 0 && a_io == sizeof(ImGuiIO) &&
	                a_style == sizeof(ImGuiStyle) && a_vec2 == sizeof(ImVec2) && a_vec4 == sizeof(ImVec4) &&
	                a_drawVert == sizeof(ImDrawVert) && a_drawIdx == sizeof(ImDrawIdx);
	if (!ok) {
		logger::warn("AMF_CheckImGuiABI: a consumer built against Dear ImGui \"{}\" (io {}, style {}) asked to share the "
					 "framework's {} (io {}, style {}) - refused; it must use the ig* exports instead",
			a_version ? a_version : "(null)", a_io, a_style, IMGUI_VERSION, sizeof(ImGuiIO), sizeof(ImGuiStyle));
	}
	return ok;
}

namespace
{
	// the game thread, every frame (Tick.cpp): the System-menu row (not yet wired on Witcher 3 - M3)
	void OnFrame()
	{
		systemrow::Tick();
		red3::Pump();   // engine calls only here, on the game's main thread
		static ULONGLONG s_next = 0;   // the probes' module checks, about once a second
		if (const ULONGLONG now = ::GetTickCount64(); now >= s_next) {
			s_next = now + 1000;
			probe::Poll();
		}
	}

	// Documents\The Witcher 3\AMF\ApocryphaMenuFramework.log - beside the game's own saves and settings, outside the game
	// folder, so it is a real file under Mod Organizer 2 and survives a game update.
	void OpenLog()
	{
		std::filesystem::path dir = "AMF";
		PWSTR docs = nullptr;
		if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)) && docs) {
			dir = std::filesystem::path(docs) / "The Witcher 3" / "AMF";
		}
		if (docs) { ::CoTaskMemFree(docs); }
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((dir / "ApocryphaMenuFramework.log").string(), true);
		auto log = std::make_shared<spdlog::logger>("AMF", std::move(sink));
		log->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);
		spdlog::set_default_logger(std::move(log));
	}

	std::atomic_bool g_started{ false };

	void Start()
	{
		if (g_started.exchange(true)) { return; }
		OpenLog();
		wchar_t exe[MAX_PATH]{};
		::GetModuleFileNameW(nullptr, exe, MAX_PATH);
		logger::info("Apocrypha Menu Framework {} loading (The Witcher 3 Remastered, DX12; ASI)", AMF_VERSION);
		logger::info("Original framework embedding Dear ImGui (MIT); the same core and API as the Skyrim AMF");
		logger::info("host {} - working folder {}", std::filesystem::path(exe).string(), std::filesystem::current_path().string());
		logger::info("AMF data folder {} (beside the exe, through the virtual folder under Mod Organizer 2)", paths::Data().string());

		// Settings first: the log level and the menu key are read before anything else logs or binds.
		settings::Load();
		strings::Load();
		modmenus::Start();   // the mods' own settings menus, read on a background thread
		// What the game reads its input through - the M1 probes (PLAN open questions 3 and 4), each logged once.
		probe::Install();
		input::Install();
		tick::Install(&OnFrame);

		// The ASI loader runs this before the game creates its renderer: hook the system DXGI factory exports now and
		// pick up the swap chain and its command queue when the game creates them.
		if (!Overlay::Install()) {
			logger::error("the overlay hooks could not be installed; AMF will not draw");
		}
		logger::info("Successfully loaded!");
	}
}

// An ASI loader (the Ultimate ASI Loader, or AMFLoader) LoadLibrary's this file before the game's WinMain, so the renderer
// does not exist yet. There is no plugin interface to answer: the work starts here.
BOOL APIENTRY DllMain(HMODULE a_module, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH) {
		::DisableThreadLibraryCalls(a_module);
		Start();
	}
	return TRUE;
}
