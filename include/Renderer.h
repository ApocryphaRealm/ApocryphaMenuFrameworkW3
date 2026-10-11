#pragma once

#include <string>

struct ImDrawList;

// ============================================================================================
// M1: the render loop. Two trampoline call-hooks (survey §7.1), a probed D3D-init site
// (Offsets.h explains the dispute), one ImGui context for the whole process.
//
// Hook installation happens in SKSEPluginLoad because renderer bring-up PRECEDES kDataLoaded -
// there is no later moment that still catches init. This is the established pattern across the
// surveyed ecosystem. It is in deliberate tension with this project's DEM crash lesson (avoid
// early relocation work); the mitigations are the byte-pattern guards - a site that does not
// look like a call instruction is never written, the framework logs why and stays inert, and
// the game boots untouched.
// ============================================================================================

struct ImDrawList;   // Dear ImGui's, for RecordGameFrame; this header is included where ImGui is not
struct ImVec2;

namespace renderer
{
	// What the last font atlas build holds (1.8.9): the languages it was built for, its glyph count
	// and size, and whether one probe glyph per script is present. Read by the driving tool.
	struct FontProbe
	{
		std::string language, gameLanguage;
		int glyphs = 0, atlasWidth = 0, atlasHeight = 0, builds = 0;
		bool hasKana = false, hasHangul = false, hasHanzi = false, hasCyrillic = false;
	};
	FontProbe GetFontProbe();

	// Oblivion Remastered: the D3D12 overlay (Overlay.cpp) finds the device and calls these two. OnDeviceReady once,
	// with the game window and back-buffer size (false = the framework stays inert, logged); OnFrame every presented
	// frame, which runs the whole ImGui frame up to and including ImGui::Render().
	bool OnDeviceReady(void* a_hwnd, unsigned a_width, unsigned a_height);
	void OnFrame();

	// The framework window's visibility toggle - flipped by the M1 input sink, consumed by the
	// present thunk. Atomic: touched from the input thread, read on the render thread.
	void ToggleMainWindow();
	bool IsMainWindowVisible();

	// The resolution scale (display height / 1080, never below 1) the style was scaled by at start-up
	// (ImGuiStyle::ScaleAllSizes). Render thread. The consumer header wrappers (Skyrim 2.0.6) read it.
	float UiScale();

	// The game's own menu (title screen or pause menu) is open - read from the framework's script by ModMenus.
	void SetGameMenuOpen(bool a_open);

	// The game's frame round a highlighted area (W3), drawn at the end of the frame on a_drawList, clipped to the clip
	// rect given - for a highlight ImGui does not draw itself (a mod page's row: label and control together).
	void RecordGameFrame(ImDrawList* a_drawList, const ImVec2& a_min, const ImVec2& a_max, const ImVec2& a_clipMin, const ImVec2& a_clipMax);

	// TRUE while a mod's own window (AddWindow / AddWindowWithView) is open, blocking the player's input AND drew a
	// window that takes the mouse, as sampled by the render thread at the top of its last frame (Skyrim 2.0.4). The
	// input layer then gives ImGui the keyboard and mouse and holds them from the game, as for our own menu - but
	// the game is not paused, the pad stays the game's, and the menu's own commands do not fire.
	bool ConsumerWindowOwnsInput();

	// 1.0.5 (Skyrim 2.1.7): a wheel click at (a_x, a_y) in display pixels, a_step +1 = down (next tab), -1 = up. TRUE when the
	// pointer is over the open mod's tab bar as drawn last frame - the step is then taken by the bar and the wheel must not
	// reach ImGui (so the page does not scroll). Render thread only (the input drain, before ImGui::NewFrame).
	bool WheelStepsModTabs(float a_x, float a_y, int a_step);

	// 1.0.5: amf.menu op=setting - set one of the values the settings page and the Mods row set (pointerSpeed 0.25-4.00,
	// wheelSwitchesTabs, foldAllSeparators, systemMenuRow, pauseGame, helpBar - 0/1) and save, exactly as the control does. The change
	// is applied on the render thread at the next frame. An empty name reports the values. Returns the tool's JSON.
	std::string SettingOp(const std::string& a_name, double a_value, bool a_haveValue);

	// 1.0.5: amf.menu op=filter - the Mods row's Filter words: action add (word, value = state 1 / -1 / 0) | state | cycle
	// (value -1 = back) | remove | clear | matchAll (value 0/1); an empty action reports the words, matchAll and shownRows.
	std::string FilterOp(const std::string& a_action, const std::string& a_word, double a_value);
	// 1.0.5 - amf.menu op=color: Appearance > Colours for the active theme (set / clear / reset / apply / discard / applyNow).
	std::string ColorOp(const std::string& a_action, const std::string& a_role, const std::string& a_color, double a_value);
	// 1.0.5 (Skyrim 2.1.6) amf.menu op=art: a_set false reports every art kind; true sets the active theme's pick for a_kind
	// (Frame, Background, Popup, Highlight, ...): a part name, "none", or "" for the theme's own.
	std::string ArtOp(const std::string& a_kind, const std::string& a_name, bool a_set);

	// Window coordinates -> the swap chain image's pixels (Skyrim 2.0.8). 1 when the game draws an image the size of
	// its window, which is the usual case; the input layer scales the OS cursor's client position by it.
	void WindowToImageScale(float& a_x, float& a_y);

	// TRUE while an ImGui text field has the keyboard (io.WantTextInput), sampled once per frame.
	// The input hook reads it on the game thread to turn the engine's own text entry on and off -
	// without that the engine makes no CharEvent at all and every text box in the framework is
	// deaf (the search bar report, phbd01 2026-09-19).
	bool WantsTextInput();
	// The game's window handle (HWND) as seen at D3DInit; null before the renderer is up.
	void* GetGameWindow();

	// External menu control + query, used by the DevBench tool (DevBenchTool.cpp) so the menu can
	// be driven and inspected headlessly for testing (rule 31): open/close, move the selection to a
	// node path (e.g. "system/quit", "mod" with the mod set via the tool, "stats"), run the
	// selected node's action, and read the whole menu as JSON. Thread-safe.
	// a_nested says the surface was opened from the row in the GAME's own System menu rather than
	// from the hotkey or a menu launcher. It only affects GEOMETRY: a nested window is sized and
	// placed to the journal panel hosting it, so it reads as a page of that menu instead of a
	// larger window on top of it. Everything it draws is identical either way.
	// A page tells the framework about its OWN tab bar and reads back the tab the D-pad asked for
	// (-1 = nothing). Call it once per frame from the page's render function. A page that never
	// calls it behaves exactly as before, so no other author has to change anything.
	int DeclareInnerTabs(int a_count, int a_current);

	void SetMenuVisible(bool a_visible, bool a_nested = false);
	void SetSelectedNode(const std::string& a_node);
	std::string GetSelectedNode();
	void ActivateSelectedNode();
	// Menu-shell personalization, driven from DevBench for testing (the settings page drives the
	// same personalization:: calls directly). Return false when no such mod is registered.
	// Switch the active theme by registry id (e.g. "skyrim", "untarnished") and
	// save it. Exposed for DevBench so a visual comparison of two themes can be photographed in
	// ONE game session instead of one launch per theme. Returns false for an unknown id.
	bool SetTheme(const std::string& a_themeId);

	// Driving the pane and tab navigation from DevBench (amf.menu op=nav / op=focus), so the
	// controller scheme can be exercised with no keypress at all (rule 64). QueueNav queues a
	// synthetic left/right press that is read in the same place a real D-pad press is read -
	// including the tab walking, so a test proves the shipped decision rather than a private path
	// around it. FocusPane puts nav in the mod list or the options pane, which is how a nav case
	// is set up headlessly. Both return false, and log why, for a value they do not recognise.
	// Thread-safe: called on devbench's listener thread, applied on the render thread.
	bool QueueNav(const std::string& a_direction);  // "left" | "right"
	bool FocusPane(const std::string& a_pane);      // "list" | "options"

	bool SetModAlias(const std::string& a_modName, const std::string& a_alias);
	bool MoveModTo(const std::string& a_modName, int a_position);
	void ResetModOrder();
	// separators (2026-10-02): action add {name, mod = above which} | remove {separator} | send {mod, separator ("" = none)}
	// | collapse {separator} | favourite {separator}; returns the tool's JSON answer
	std::string SeparatorOp(const std::string& a_action, const std::string& a_name, const std::string& a_mod, const std::string& a_separator);

	std::string GetMenuStateJson();
	// 1.8.9: draw the active theme's frame around a rect on a consumer's draw list (see Renderer.cpp).
	bool DrawThemeFrameAround(ImDrawList* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1);
	// 1.0.5: a framework panel window (the on-screen keyboard) in the theme's art - its Background (a_frame false, after Begin)
	// and its Frame on its own edge (a_frame true, before End). Whether anything was drawn.
	bool DrawPanelArt(ImDrawList* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1, bool a_frame);

	// In-process capture (ported from the Overhaul line, 2026-08-30): saves the NEXT presented
	// frame - WITH the ImGui overlay - as a PNG at a_path. Blocks the calling (DevBench listener)
	// thread until the render thread serviced it or a_timeoutMs passed. Empty return = success.
	std::string CaptureBlocking(const std::wstring& a_path, unsigned a_timeoutMs);
}
