#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ArtKinds.h"

// ============================================================================================
// Theme REGISTRY (the author, 2026-08-27) - supersedes the original "no theme-customisation UI by
// design" stance recorded below, the same way a rule gets amended rather than left standing
// alongside its own contradiction (project convention: fold the update into the decision).
//
// Mirrors MO2's own theme mechanism exactly, confirmed by reading it directly
// (C:\Modlists\Apostasy\stylesheets\*.qss - one self-contained file per theme, additive,
// nothing overwrites anything else). AMF's equivalent: a Palette per theme, registered either
// at compile time (the two built-ins below) or scanned additively from
// OBSE/Plugins/ApocryphaMenuFramework/themes/*.ini at load - never mutating another
// theme's entry.
//
// Original spec this supersedes, kept for provenance:
//   "the same build philosophy as the Untarnished UI minimap edit - solid black at full
//    opacity, affected by the game's opacity setting, and the white framing around all the
//    buttons and menus." That look now SHIPS AS the "Untarnished" theme (the author's instruction),
//    one entry in the registry rather than the only possible one.
// ============================================================================================

namespace theme
{
	struct Palette
	{
		std::string id;     // stable key, used in the INI (never renamed once shipped)
		std::string name;   // display name in the theme picker

		std::uint32_t background;    // ABGR
		std::uint32_t frame;         // ABGR - primary border/text/accent colour (the ONE colour
		                             // a simple/INI theme provides; the fields below refine it)
		float borderThickness = 1.0f;

		// Optional refinements (2026-08-28, MO2-Skyrim rebuild). A real UI is not one flat colour:
		// the Trosski Skyrim style uses silver frame lines, brighter text, a dim secondary tone
		// and a gold accent. Any left 0 falls back to `frame`, so the two built-ins and every
		// INI-scanned theme keep working unchanged. ABGR, same packing as `frame`.
		std::uint32_t border = 0;    // panel/frame lines            (Trosski #b0b0b0 silver)
		std::uint32_t text = 0;      // primary text                 (Trosski #dddddd bright)
		std::uint32_t textDim = 0;   // disabled/secondary text      (Trosski #717171)
		std::uint32_t accent = 0;    // selection / checkmark / grab (Trosski #a1912b gold)

		// Draw the Nordic knotwork frame (border-image.png, embedded) around the window. The
		// Trosski MO2 style's defining feature; off for plain themes like Untarnished.
		bool knotwork = false;

		// With knotwork on, draw the framework's OBLIVION frame instead: the embroidered map's edge in gold and brown
		// (MapEdgeBorder.h, the owner, 2026-10-02), its edges tiled so the stitches keep their size. The built-in
		// "Oblivion" theme sets it; a theme INI may too (bMapEdge=1).
		bool mapEdge = false;

		// The theme's own ART (1.9.8, the owner, 2026-09-25: AMF themes built on Vel'dun UI and
		// Oathvein UI). A theme in this project means replacement art, so a theme INI may name the
		// same pieces [Skin] does - frame, background, control plates - and they draw whenever that
		// theme is picked. Paths resolve under Data like [Skin]'s. Empty = the theme has no art of
		// that kind. A player's own [Skin] art, when switched on, still wins over these.
		std::string   skinFrame;
		std::uint32_t skinFrameCorner = 64;
		std::string   skinBackground;
		std::string   skinPlates;

		// Text roles (1.0.5, Skyrim 2.1.5): section headings and help text. 0 = derived - headings take the accent, help the
		// dim tone a third of the way toward the main text. A theme INI sets them with sTextHeader / sTextHelp. Last in the
		// struct so the positional built-in initialisers above keep their meaning.
		std::uint32_t textHeader = 0;
		std::uint32_t textHelp = 0;

		// THE ART PARTS (1.0.5, Skyrim 2.1.6's art library): one part name per kind (ArtKinds.h) from AMF\assets\<folder>\,
		// INI key s<Kind>Art (sFrameArt, sBackgroundArt, ...). Empty = the built-in look of that kind. A name wins over the old
		// path keys above (which stay as the fallback when the library part is missing); a player's own pick on
		// Appearance > Art wins over both.
		std::array<std::string, skin::kArtKindCount> art;
	};

	// Registers a theme additively. Re-registering an existing id REPLACES that entry only
	// (never touches any other) - the same semantics as dropping a new .qss into MO2's folder
	// alongside the others.
	void RegisterTheme(Palette a_palette);

	// Scans OBSE/Plugins/ApocryphaMenuFramework/themes/*.ini and registers each as an
	// additional theme. Missing folder is not an error - the two built-ins still work.
	void ScanUserThemes();

	std::vector<Palette> ListThemes();

	// Empty/unknown id is a no-op (current theme stays active); logs either way.
	// Maps the ids retired in the 2026-09-01 theme merge ("vanilla", "mo2-skyrim") onto "skyrim";
	// anything else is returned unchanged. Called when reading a theme id out of the INI.
	std::string MigrateThemeId(const std::string& a_id);

	void SetActiveTheme(const std::string& a_id);
	const Palette& GetActiveTheme();

	// The game's own opacity, applied as ONE global alpha multiplier over the whole theme at
	// draw time - never threaded through individual colour constants. Orthogonal to which
	// palette is active.
	//
	// Resolution strategy is ported from Dragon's Eye Minimap (MiniMap.cpp GetHUDOpacitySetting,
	// proven in game 2026-08-27): try "fHUDOpacity", "fHUDOpacity:MAIN", "fHUDOpacity:Interface",
	// "fHUDOpacity:Display" across BOTH INI setting collections, log which name resolved, and
	// re-read every frame the menu is open so an options-menu change is followed without a
	// reload. FULL OPACITY IS THE FALLBACK - never fail toward translucent.
	float GetGameHUDOpacity();

	// Applies the active theme's palette + border rules to the live ImGui style.
	void Apply();

	// The window padding Apply() sets, before any resolution scaling (knotwork corner + 8 = 34). ImGui's own
	// default is 8. The consumer header wrappers (Skyrim 2.0.6) recognise AMF's padding by this value.
	float BaseWindowPadding();

	// Dropdowns (Skyrim 2.1.1). ImGui's combo popup takes its top and bottom padding from WindowPadding, which Apply()
	// sets to the knotwork corner - an empty band above and below every list. These open a dropdown with the same small
	// padding the right-click menus use; everything else is ImGui::BeginCombo / ImGui::Combo unchanged.
	bool BeginComboTight(const char* a_label, const char* a_preview);
	bool ComboTight(const char* a_label, int* a_current, const char* const a_items[], int a_count);

	// THE COLOUR ROLES a player can set on Appearance > Colours (1.0.5, Skyrim 2.1.5), [Colors.<theme id>] keys in this order.
	// Each falls back to the active theme's own colour when the player has not set it.
	// The art library (Skyrim 2.1.6) added one role per art kind after kRoleHover (1.0.5). A new role is appended before
	// kRoleCount with its key below, and the array size in settings::Values::themeColors raised (Settings.cpp checks).
	enum ColorRole : int
	{
		kRoleBackground = 0,   // sBackground - window and pane backgrounds
		kRoleBorder,           // sBorder     - frame lines, borders, separators
		kRoleArt,              // sArt        - a tint over the theme's frame / background art (white = as drawn)
		kRoleBoxes,            // sBoxes      - the fill of fields, buttons and tabs at rest
		kRoleText,             // sText       - text
		kRoleTextDim,          // sTextDim    - secondary text: values, hints, disabled rows
		kRoleAccent,           // sAccent     - selection and tabs
		kRoleSlider,           // sSlider     - slider grabs and tick marks
		kRoleSwitchOn,         // sSwitchOn   - an on/off switch's track when on
		kRoleSwitchOff,        // sSwitchOff  - ... and when off
		kRoleHeading,          // sHeading    - section headings (mod pages' separators)
		kRoleHelp,             // sHelp       - help text and page notes
		kRoleHover,            // sHover      - the highlight under the mouse: the hover wash on rows and tabs, and the hover frame
		// The art library's roles (Skyrim 2.1.6, the owner: "all the different kinds should have a color change option"). Each
		// FOLLOWS the role it used to share until the player picks it, so nothing looks different until then.
		kRoleButtons,          // sButtons     - button fill at rest                 (follows Boxes)
		kRoleTabs,             // sTabs        - tab fill at rest                    (follows Boxes)
		kRoleTickMark,         // sTickMark    - the tick in a tick box              (follows Slider)
		kRoleScrollbar,        // sScrollbar   - the scroll bar's grab               (follows Border, dimmed)
		kRoleScrollTrack,      // sScrollTrack - the scroll bar's track              (follows Background)
		kRoleSection,          // sSection     - section lines and separators        (follows Border, dimmed)
		kRoleArrows,           // sArrows      - fold and dropdown arrows            (follows Text)
		kRoleKnob,             // sKnob        - the switch knob                     (the knob's own near-white until picked)
		kRolePointer,          // sPointer     - the mouse pointer art               (white: as drawn)
		kRolePopups,           // sPopups      - the frame art round popups and lists (follows Frame art)
		kRoleSliderTrack,      // sSliderTrack - a slider's track                    (follows Boxes)
		kRoleCount
	};
	inline constexpr const char* kColorRoleKeys[kRoleCount] = { "sBackground", "sBorder", "sArt", "sBoxes", "sText", "sTextDim",
		"sAccent", "sSlider", "sSwitchOn", "sSwitchOff", "sHeading", "sHelp", "sHover", "sButtons", "sTabs", "sTickMark", "sScrollbar",
		"sScrollTrack", "sSection", "sArrows", "sKnob", "sPointer", "sPopups", "sSliderTrack" };

	// The colour a role draws in now (the player's when set, else the theme's), and the theme's own one. ImU32 (ABGR).
	// Computed by Apply(), so both follow a theme switch.
	std::uint32_t RoleColor(int a_role);
	std::uint32_t ThemeRoleColor(int a_role);

	// The player's picks for the ACTIVE theme (settings themeColors[active id]) - each theme keeps its own. The mutable form
	// creates the theme's entry; RolePicked says whether the player set that role for the active theme.
	std::array<std::string, kRoleCount>& PlayerColors();
	bool RolePicked(int a_role);

	// The Colours page's preview in picks not yet applied: Begin swaps that look in (ImGui colours, role colours, heading /
	// help) for the widgets drawn until End puts the applied look back. PicksRoleColor: a role's colour under picks.
	void BeginPreviewColors(const std::array<std::string, kRoleCount>& a_picks);
	void EndPreviewColors();
	std::uint32_t PicksRoleColor(const std::array<std::string, kRoleCount>& a_picks, int a_role);

	// The text-role colours for ImGui::PushStyleColor - RoleColor(kRoleHeading / kRoleHelp) with the see-through text fade.
	std::uint32_t HeaderTextColor();
	std::uint32_t HelpTextColor();

	// "#RRGGBB" / "#RRGGBBAA" -> ABGR; false when it is not a colour.
	bool ParseHexColor(std::string_view a_hex, std::uint32_t& a_out);
}
