#pragma once

// ============================================================================================
// CONSUMER SURFACE - the half of the SMF-compatible API that is NOT a settings page.
//
// AddSectionItem covers a mod that wants a page inside the framework's own menu. The stock
// consumer header exposes a second, larger surface for mods that want to draw their own thing:
// standalone windows, HUD elements drawn over the game, named fonts, and image textures. AMF
// exported none of it, and the silence is the problem - the header's wrappers are
//
//     static auto func = GetFunction<...>("AddWindow");
//     if (func) { ... }
//
// with no else, so a missing export is a NO-OP, not an error. Measured 2026-09-04: with the
// module-name alias in place, five third-party mods resolved AMF correctly and still registered
// nothing, because what they actually call is AddWindow, not AddSectionItem.
//
// Everything here is owned by the render thread except the registries, which are guarded because
// registration happens on whatever thread a consumer's SKSEPlugin_Load or message handler runs on.
// ============================================================================================

#include <cstdint>
// For WindowStates()'s diagnostic snapshot below - it returns a vector of copied structs, one of
// which holds the window's view name.
#include <string>
#include <vector>

struct ImVec2;
struct ImFont;

namespace consumer
{
	using RenderFunction = void (*)();
	using HudCallback = void (*)();

	// The object a consumer is handed by AddWindow/AddWindowWithView. Its LAYOUT is the contract
	// (two atomics, in this order, nothing before them) because the consumer writes IsOpen
	// directly - the same arrangement GetMainWindow already ships. Allocated once and never
	// freed: a consumer keeps the pointer for the life of the process.
	struct WindowInterface
	{
		std::atomic<bool> IsOpen{ false };
		std::atomic<bool> BlockUserInput{ true };
	};

	// a_view is the optional "view name" of AddWindowWithView - kept with the entry so a window
	// can be addressed by name later; nullptr for a plain AddWindow.
	WindowInterface* AddWindow(RenderFunction a_render, const char* a_view);

	std::int64_t RegisterHudElement(HudCallback a_callback);
	void UnregisterHudElement(std::int64_t a_id);

	// Render thread, inside a frame. Windows self-gate on IsOpen; HUD elements always draw,
	// which is the whole point of being a HUD element rather than a page.
	void DrawWindows();
	void DrawHudElements();

	// True when a consumer window is up AND taking input - folded into the framework's own
	// IsAnyBlockingWindowOpened answer so a launcher gets one truthful answer for the process.
	bool AnyBlockingWindowOpen();

	// True when a consumer window should be HANDED THE PLAYER'S INPUT (Skyrim 2.0.4): open, BlockUserInput,
	// AND at least one top-level ImGui window its render function submitted on the last drawn frame
	// accepts the mouse (no ImGuiWindowFlags_NoMouseInputs). The flags alone are not enough - the
	// stock header's AddWindow(render, doesWindowPauseGame = true) sets BlockUserInput on every window
	// it creates, so a passive always-on overlay (StepUpOnto SKSE's NPC perf overlay) reads as blocking
	// and took all of the game's input during play. Render thread; one frame behind DrawWindows.
	bool AnyWindowOwnsInput();
	// The ImGui name of the first window that makes AnyWindowOwnsInput() true, for the transition log.
	std::string InputOwnerName();

	// ---- fonts ----------------------------------------------------------------------------
	// PushFont(name) and the three family pushes are always balanced by Pop(): an unknown name
	// pushes the CURRENT font rather than nothing, because a consumer that pushed and popped
	// symmetrically must not be able to unbalance ImGui's stack through us.
	// Skyrim 2.0.4: the Font Awesome names ("fa-solid-900", "fa-regular-400", "fa-brands-400", and the
	// family pushes PushSolid / PushRegular / PushBrands) push an ICON FACE - the framework's text
	// face with that Font Awesome style merged in - once the renderer has built it.
	void PushNamedFont(const char* a_name);

	// The icon faces. Built on demand: the first push of a face marks it wanted and asks the renderer
	// for a new atlas, so a load order with no icon-using mod pays nothing in atlas size. Render thread.
	enum IconFace : int { kIconSolid = 0, kIconRegular = 1, kIconBrands = 2, kIconFaceCount = 3 };
	bool IconFaceWanted(int a_face);
	void SetIconFont(int a_face, ImFont* a_font);   // null on every atlas Clear(); set by BuildFonts
	void PushRegular();
	void PushSolid();
	void PushBrands();
	void PopFont();

	// ---- textures ---------------------------------------------------------------------------
	// Cached by path: a consumer calling LoadTexture every frame (they do) must not re-decode.
	void* LoadTexture(const char* a_path, ImVec2* a_outSize);
	void DisposeTexture(const char* a_path);


	// Counts for the DevBench tool, so a live run can be checked without reading the log.
	std::size_t WindowCount();
	std::size_t HudElementCount();
	std::size_t TextureCount();

	// One top-level ImGui window a consumer's render function submitted on the last drawn frame
	// (Skyrim 2.0.4 probe) - what the input gate above decides on, readable over DevBench.
	struct SubmittedWindow
	{
		char name[64]{};               // fixed, so the per-frame probe allocates nothing
		int flags{ 0 };
		bool noMouseInputs{ false };   // ImGuiWindowFlags_NoMouseInputs - the input gate's test
		bool noInputs{ false };        // all of ImGuiWindowFlags_NoInputs
		float x{ 0.0f }, y{ 0.0f }, w{ 0.0f }, h{ 0.0f };
	};

	// One consumer window's flags, copied out. DIAGNOSTIC, added 2026-09-12.
	//
	// IsAnyBlockingWindowOpened() answers `IsMainWindowVisible() || AnyBlockingWindowOpen()`, and
	// mods gate their own hotkeys on it - Gear Toggle's SettingsPanel::BlocksInput() returns early
	// from its input sink when it is true, and Simple Power Attack imports the same export. Two
	// users reported hotkeys dead under AMF but fine on SKSE Menu Framework.
	//
	// The aggregate alone is not enough to act on: with several windows registered, "true" does not
	// say WHICH one is latched open, and guessing among them is what has made this defect take three
	// passes. Each entry names itself so one measurement identifies the culprit.
	struct WindowState
	{
		bool open{ false };
		bool blocking{ false };
		bool acceptsMouse{ false };    // Skyrim 2.0.4: a submitted window takes the mouse
		std::string view;   // AddWindowWithView's name; empty for a plain AddWindow
		std::vector<SubmittedWindow> submitted;
	};

	std::vector<WindowState> WindowStates();
}
