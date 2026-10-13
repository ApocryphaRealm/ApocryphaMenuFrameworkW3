#pragma once

#include <string>

// AMF's OWN Font Awesome icons (W3 1.0.5, from Skyrim 2.1.5 - the owner, 2026-10-07: "I just want AMF to include Font Awesome for the generated
// menus"). Font Awesome Free 6.7.2 solid glyphs, merged into the TEXT face by Renderer.cpp BuildFonts (only these
// codepoints), so a label can carry an icon inline: icons::Label(icons::kInfo, text). UTF-8 of U+Fxxx. The file
// ships in bin/x64_dx12/AMF/icons/ under SIL OFL 1.1 (icons CC BY 4.0), the licence beside it.
// A missing file leaves AMF's own labels as plain text (Ready() is false), never a "?" in front of them.

namespace icons
{
	inline constexpr const char* kInfo = "\xEF\x81\x9A";      // U+F05A circle-info: help text, page notes
	inline constexpr const char* kKeyboard = "\xEF\x84\x9C";  // U+F11C keyboard: a key binding button
	inline constexpr const char* kClear = "\xEF\x80\x8D";     // U+F00D xmark: clear / unmap a key
	inline constexpr const char* kReset = "\xEF\x8B\xAA";     // U+F2EA rotate-left: reset to default
	inline constexpr const char* kPen = "\xEF\x8C\x84";       // U+F304 pen: a text field
	inline constexpr const char* kPalette = "\xEF\x94\xBF";   // U+F53F palette: a colour
	inline constexpr const char* kLock = "\xEF\x80\xA3";      // U+F023 lock: an option the mod has disabled
	inline constexpr const char* kLoading = "\xEF\x89\x92";   // U+F252 hourglass-half: loading / waiting
	inline constexpr const char* kWarning = "\xEF\x81\xB1";   // U+F071 triangle-exclamation: not drawable here
	inline constexpr const char* kAction = "\xEF\x81\x94";    // U+F054 chevron-right: a row that runs something

	// AMF's OWN PAGES (1.0.6, from Skyrim 2.2.0 - the owner, 2026-10-13: "Build in the Font Awesome in AMF's own pages"): the
	// side list, the page headings, the tabs, the Help sections and the buttons. Same face, same merge; one meaning per glyph,
	// the same glyph as Skyrim's for the same control, and the glyphs above reused where the meaning is the same (kKeyboard,
	// kClear, kReset, kPen, kPalette).
	inline constexpr const char* kGear = "\xEF\x80\x93";        // U+F013 gear: Settings
	inline constexpr const char* kGamepad = "\xEF\x84\x9B";     // U+F11B gamepad: Controls, the Controller tab
	inline constexpr const char* kQuestion = "\xEF\x81\x99";    // U+F059 circle-question: Help
	inline constexpr const char* kPuzzle = "\xEF\x84\xAE";      // U+F12E puzzle-piece: the Mods heading
	inline constexpr const char* kCubes = "\xEF\x86\xB3";       // U+F1B3 cubes: the Framework heading
	inline constexpr const char* kSliders = "\xEF\x87\x9E";     // U+F1DE sliders: the General tab
	inline constexpr const char* kFileImport = "\xEF\x95\xAF";  // U+F56F file-import: the Mod menus tab (pages read from the game's menus)
	inline constexpr const char* kList = "\xEF\x80\xBA";        // U+F03A list: Menu list, the mod list
	inline constexpr const char* kFont = "\xEF\x80\xB1";        // U+F031 font: Theme and text
	inline constexpr const char* kWindowMax = "\xEF\x8B\x90";   // U+F2D0 window-maximize: the Window tab
	inline constexpr const char* kDroplet = "\xEF\x81\x83";     // U+F043 droplet: Colours
	inline constexpr const char* kImage = "\xEF\x80\xBE";       // U+F03E image: Art
	inline constexpr const char* kBook = "\xEF\x80\xAD";        // U+F02D book: Readme
	inline constexpr const char* kWrench = "\xEF\x82\xAD";      // U+F0AD wrench: Troubleshooting
	inline constexpr const char* kListCheck = "\xEF\x82\xAE";   // U+F0AE list-check: Features
	inline constexpr const char* kDoor = "\xEF\x94\xAB";        // U+F52B door-open: opening and closing the menu
	inline constexpr const char* kArrows = "\xEF\x81\x87";      // U+F047 arrows-up-down-left-right: moving around
	inline constexpr const char* kCheck = "\xEF\x80\x8C";       // U+F00C check: Apply, OK
	inline constexpr const char* kSave = "\xEF\x83\x87";        // U+F0C7 floppy-disk: Save
	inline constexpr const char* kFolderOpen = "\xEF\x81\xBC";  // U+F07C folder-open: Load
	inline constexpr const char* kTrash = "\xEF\x87\xB8";       // U+F1F8 trash: Delete
	inline constexpr const char* kSortAZ = "\xEF\x85\x9D";      // U+F15D arrow-down-a-z: Sort
	inline constexpr const char* kFilter = "\xEF\x82\xB0";      // U+F0B0 filter: Filter
	inline constexpr const char* kPlus = "\xEF\x81\xA7";        // U+F067 plus: Add
	inline constexpr const char* kFolder = "\xEF\x81\xBB";      // U+F07B folder: Fold (the separators)
	inline constexpr const char* kSearch = "\xEF\x80\x82";      // U+F002 magnifying-glass: Search, the mod-menus name filter
	inline constexpr const char* kUndo = "\xEF\x83\xA2";        // U+F0E2 arrow-rotate-left: Undo
	inline constexpr const char* kToggleOn = "\xEF\x88\x85";    // U+F205 toggle-on: All on (W3's Mod menus list)
	inline constexpr const char* kToggleOff = "\xEF\x88\x84";   // U+F204 toggle-off: All off (W3's Mod menus list)

	// EVERY codepoint above, so BuildFonts rasterises exactly these and the two lists cannot drift apart (tools/glyph_coverage_check
	// proves each one is declared, listed once, used where AMF draws it, and in the built atlas). Add a glyph above, add it here.
	inline constexpr unsigned int kCodepoints[] = {
		0xF05A, 0xF11C, 0xF00D, 0xF2EA, 0xF304, 0xF53F, 0xF023, 0xF252, 0xF071, 0xF054,
		0xF013, 0xF11B, 0xF059, 0xF12E, 0xF1B3, 0xF1DE, 0xF56F, 0xF03A, 0xF031, 0xF2D0, 0xF043, 0xF03E, 0xF02D, 0xF0AD,
		0xF0AE, 0xF52B, 0xF047, 0xF00C, 0xF0C7, 0xF07C, 0xF1F8, 0xF15D, 0xF0B0, 0xF067, 0xF07B, 0xF002, 0xF0E2, 0xF205,
		0xF204 };

	// Set by BuildFonts when the glyphs really are in the atlas. Without them (the icon file missing, or no TrueType face
	// loaded) AMF's own labels draw as plain text rather than with a "?" in front of every tab and button.
	inline bool& Ready()
	{
		static bool s_ready = false;
		return s_ready;
	}

	// "<icon>  <text>" - the one spelling every icon label uses (two spaces, as on the converted pages). For text that is not
	// an ImGui ID (headings, the side list's group names, a text box's hint). The text keeps its translation; only the glyph
	// is added in front of it, so no language file changes.
	inline std::string Label(const char* a_icon, const char* a_text)
	{
		const char* text = a_text ? a_text : "";
		return Ready() && a_icon ? std::string(a_icon) + "  " + text : std::string(text);
	}

	// The same, for a widget whose label is its ImGui ID (a button, a tab, a menu item, a switch): "###<a_id>" after the
	// shown text, so the ID is a_id's alone - the same with or without the icon and in every language, and a remembered tab
	// or a nav target never moves when the icon file comes or goes or the language changes.
	inline std::string Labelled(const char* a_icon, const char* a_text, const char* a_id)
	{
		return Label(a_icon, a_text) + "###" + (a_id ? a_id : "");
	}
}
