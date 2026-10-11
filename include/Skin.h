#pragma once

// Replacement ARTWORK for the framework's menu shell, so a UI author can make Apocrypha Menu
// Framework match their own interface instead of accepting the built-in look.
//
// Requested 2026-09-09 for borokoshow, to match Dragonborn UI. It is the standing project meaning
// of "theme": a theme is REPLACEMENT ART, not a colour tint over the same shapes.
//
// WHAT AN AUTHOR SHIPS
//   Frame       one square PNG, 32-bit RGBA, TRANSPARENT CENTRE, drawn as a nine-slice.
//               192x192 with 64px corners is the suggested default; the corner is an INI key
//               because only the artist knows where their ornament stops.
//   Background  either a small tileable PNG or a full-screen one. Which it is is decided by its
//               own size rather than a fifth INI key: <= kTileThreshold px on both sides tiles,
//               anything larger is stretched to the window. A 256x256 tile and a 1920x1080
//               backdrop therefore both just work.
//   Plates      optional small RGBA PNGs restyling individual controls, found by fixed name
//               inside one folder: toggle.png, slider.png, tab.png.
//
// ALL PNG, NO DDS. AMF decodes through WIC (Witcher 3: gfx::DecodeImageFile), which handles PNG (and JPG/BMP/
// TIFF) and does not handle DDS at all. Handing it a .dds silently fails to decode, so the
// loader below says so in the log rather than leaving an author guessing.
//
// Every piece is optional and independent: supply only a frame and the rest of the look is
// unchanged. Nothing here can make the menu unusable - a texture that fails to load is skipped
// and the built-in art draws instead.

#include <cstdint>
#include <string>
#include <vector>

#include "ArtKinds.h"

struct ImVec2;

namespace skin
{
	// A background at or below this on BOTH axes is treated as a tile; larger is stretched.
	inline constexpr std::uint32_t kTileThreshold = 512;

	enum class Plate : std::uint32_t
	{
		kToggle = 0,  // toggle.png - the on/off switch track
		kSlider,      // slider.png - the slider grab
		kTab,         // tab.png    - the section tab bar
		kCount
	};

	// THE ART LIBRARY (2.1.6, the owner, 2026-10-08: "add an assets folder with sub folders for the customization section
	// to choose from ... break down the themes into their art parts"). Every piece of art is one PART of a kind
	// (ArtKinds.h), a PNG in Data/SKSE/Plugins/ApocryphaMenuFramework/assets/<folder>/<name>.png, named after the theme it
	// came from. Witcher 3 (1.0.5): bin/x64_dx12/AMF/assets/<folder>/<name>.png, the same files. Beside it, all optional:
	//   <name>.ini        how it is cut: [Frame] uCorner, uDrawCorner, bTileEdges, sHighlight (frames); uCorner for any
	//                     nine-sliced part; uHotX / uHotY for a cursor
	//   <name>-edge.png   a second layer drawn over the first in the theme's line colour (boxes, buttons, tabs, tick
	//                     boxes, slider grabs, scroll bars): the first layer is tinted with the colour the control has
	//                     right now (at rest, under the mouse, held), so a part keeps the theme's colours and states
	//   <name>-mark.png   the tick, for a tick box
	//   <name>-track.png  the track (and <name>-track-edge.png), for a scroll bar
	// A theme names its parts (s<Kind>Art); the player can pick any part for any theme on Appearance > Art.

	// The part names in assets/<folder>/ (file names without .png), sorted. Read from disk on each call - the Art page
	// calls it when it opens, not every frame.
	std::vector<std::string> ListArt(ArtKind a_kind);
	// The active theme's own part of that kind ("" = it has none), and the part drawing now ("" = none drawing, or the
	// art came from a path rather than the library).
	std::string ThemeArt(ArtKind a_kind);
	std::string ActiveArt(ArtKind a_kind);
	// A part's texture for the Art page's previews, loaded once and kept (a few small PNGs). Null if it does not load.
	void* ArtThumb(ArtKind a_kind, const std::string& a_name, ImVec2* a_size);

	// A loaded part of one of the control kinds (Box .. Cursor) - what the ImGui art hook (ArtHooks.cpp) draws.
	struct ArtImage
	{
		void*  srv = nullptr;
		float  w = 0.0f, h = 0.0f;   // the texture's own size
		int    mips = 0;             // Witcher 3: the levels uploaded (a CPU-built chain down to 1x1; 0 = not loaded)
	};
	struct ArtPart
	{
		ArtImage main;       // <name>.png
		ArtImage edge;       // <name>-edge.png
		ArtImage extra;      // <name>-mark.png (a tick box's tick; the scroll bar track is its own kind since 2.1.6)
		float corner = 0.0f;       // uCorner in the texture (0 = a quarter of its smaller side)
		float drawCorner = 0.0f;   // uDrawCorner on a 1080p screen (0 = the same as corner); a cursor: its height there
		bool  tile = false;        // bTileEdges
		float hotX = 0.0f, hotY = 0.0f;   // a cursor's hot spot, in texture pixels
		bool  cornersOnly = false; // sHighlight=corners (a frame picked for Highlight: the line and four corners)
		bool  threeSlice = false;  // bThreeSlice (2.1.6): a grab drawn as a square cap at each end (the art's width tall) and the
		                           // middle between them, stretched or (bTileEdges) repeated - a line with an ornament at each end
		bool  ownColours = false;  // bOwnColours (2.1.6): the edge layer is art in its own colours (the knotwork, the map edge) -
		                           // drawn as painted (the Frame art tint), or in the kind's own colour once the player picks one
	};
	// The part drawing now for a kind, or null for the built-in look. Frame, Background and Switch keep their own
	// accessors below; Popup and Highlight are frames.
	const ArtPart* Part(ArtKind a_kind);

	// Loads (or reloads) every texture named by the current settings. Safe to call at any time
	// from the render thread; safe to call before the D3D device exists, in which case nothing
	// loads and the next call does the work. Called at D3D init and by the DevBench reload op,
	// so an artist can iterate without restarting the game.
	void Reload();

	// Frame. Size is the texture's own pixel size; corner is the INI value, clamped so that two
	// corners always fit inside the texture (an over-large corner would flip the middle slices).
	bool   HasFrame();
	void*  FrameTexture();
	ImVec2 FrameSize();
	float  FrameCorner();
	// From the frame's .ini (2.1.6): the corner's size on screen at 1080p when it differs from its size in the texture
	// (0 = the same; the map edge is drawn from art at twice its size), whether the edges repeat at their own size
	// instead of stretching (a pattern along the edge), and whether a highlighted item gets only the line and the four
	// corners (art with solid edge bands, which would cover the text) rather than the whole frame.
	float  FrameDrawCorner();
	// uBand (2.1.6): how far the frame's art reaches in from its outer edge, in texture pixels (0 = not given) - a framed
	// pane keeps its scroll bar inside it (the owner: the scroll bar art "isn't visible through the frame art")
	float  FrameBand();
	bool   FrameTiles();
	bool   FrameHighlightCorners();
	// Whether ANY frame draws round the window: loaded art, or the Skyrim theme's built-in knotwork when its art did not
	// load (no assets folder) - unless the player picked "none" for the frame.
	bool   DrawsFrame();

	// Background. Tiles() reports how it will be drawn, decided by kTileThreshold above.
	bool   HasBackground();
	void*  BackgroundTexture();
	ImVec2 BackgroundSize();
	bool   BackgroundTiles();

	// Optional per-control plates.
	bool   HasPlate(Plate a_plate);
	void*  PlateTexture(Plate a_plate);
	// 2.1.6: the switch track's outline layer, or null (drawn untinted by the switch colour)
	void*  ToggleEdgeTexture();
	ImVec2 PlateSize(Plate a_plate);

	// What actually loaded, for the DevBench tool and the log - so "my art is not showing" is
	// answered by asking the framework rather than by guessing at paths.
	std::string StatusJson();

	// Witcher 3 (1.0.5), amf.menu op=art: every kind's part drawing now - its name, the theme's own, the player's pick, and
	// its texture's size and mip levels - as a JSON object keyed by kind (Frame, Background, ...).
	std::string ArtKindsJson();
}
