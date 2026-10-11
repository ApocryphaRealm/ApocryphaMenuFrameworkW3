#pragma once

#include <cstddef>
#include <cstdint>

// THE KINDS OF ART A PLAYER CAN SWAP (2.1.6). The owner, 2026-10-08: "add an assets folder with sub folders for the
// customization section to choose from", then "I want more than just those three kinds ... the scroll bar ... the box
// shapes because the boxes are different from the frame ... sliders ... the toggles ... the lines that appear in a mod
// menu as a horizontal line under a section header ... the on-screen keyboard ... and anything else that you can think
// of that could be a kind for customizability".
//
// One table, read by the theme INIs (s<Key>Art), the player's picks ([Art.<theme id>] s<Key>), the library folders
// (assets/<folder>/) and the Art page. The order is the INI's and the page's; append, never reorder.
//
// The on-screen keyboard has no kind of its own: its keys are buttons and its panel is a framed window, so it follows
// Button, Frame and Background (the owner offered either; following keeps one look everywhere).
namespace skin
{
	enum class ArtKind : std::uint32_t
	{
		kFrame = 0,    // the window and pane frame (nine-slice)
		kBackground,   // the window background (tile or picture)
		kToggle,       // the on/off switch track ("Switch" on the page)
		kBox,          // text boxes, dropdowns and slider tracks - every field
		kButton,       // buttons, the on-screen keyboard's keys among them
		kTickBox,      // the tick box and its tick (<name>-mark.png)
		kSlider,       // the slider grab, the part you drag
		kScrollbar,    // the scroll bar's grab, and its track (<name>-track.png)
		kSection,      // the line under a section heading, and every other separator line
		kTab,          // the tabs across the top of a page
		kArrow,        // the fold and dropdown arrows (drawn pointing right, turned as needed)
		kPopup,        // the frame round dropdown lists, right-click menus and tooltips - from the frames
		kHighlight,    // the frame round the highlighted item - from the frames
		kCursor,       // the mouse pointer
		kKnob,         // the switch's knob, the part that slides (2.1.6: from each frame's corner piece; "Switch knobs" on the page)
		kSliderTrack,  // a slider's track, behind its grab (2.1.6, the owner: "the slider itself and then the slider grab separately")
		kScrollTrack,  // the scroll bar's track, behind its grab (was the scroll bar part's -track layer)
		kCount
	};
	inline constexpr std::size_t kArtKindCount = static_cast<std::size_t>(ArtKind::kCount);

	// [Art.<theme id>] keys; the theme INI's key is the same with "Art" added (sBox -> sBoxArt).
	inline constexpr const char* kArtKeys[kArtKindCount] = { "sFrame", "sBackground", "sToggle", "sBox", "sButton", "sTickBox",
		"sSlider", "sScrollbar", "sSection", "sTab", "sArrow", "sPopup", "sHighlight", "sCursor", "sKnob", "sSliderTrack", "sScrollTrack" };
	// assets/<folder>/ - Popup and Highlight pick from the frames.
	inline constexpr const char* kArtFolders[kArtKindCount] = { "frames", "backgrounds", "toggles", "boxes", "buttons",
		"tickboxes", "sliders", "scrollbars", "sections", "tabs", "arrows", "frames", "frames", "cursors", "knobs", "slidertracks", "scrolltracks" };
	inline constexpr const char* kArtNone = "none";   // a player's pick meaning "no art of this kind" (the built-in shape)
}
