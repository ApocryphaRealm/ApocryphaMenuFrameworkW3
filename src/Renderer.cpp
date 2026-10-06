#include "Paths.h"
#include "ModMenus.h"
#include "Red3.h"
#include "Renderer.h"
#include "Keyboard.h"

#include "ConsumerSurface.h"

#include "Compat.h"
#include "Curtain.h"
#include "Input.h"
#include "Persistence.h"
#include "PreciseSlider.h"
#include "KnotworkBorder.h"
#include "MapEdgeBorder.h"
#include "Screenshot.h"
#include "Skin.h"
#include "Bindings.h"
#include "Personalization.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "SystemRow.h"
#include "Theme.h"
#include "Watchdog.h"
#include "utils/ToggleSwitch.h"
#include "Logger.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>
#include "PreciseSlider.h"
#include <vector>
// Oblivion Remastered is D3D12-only: the overlay (Overlay.cpp) owns the device, and gfx:: lends out what the
// renderer needs (textures, the ImGui DX12 backend). The Win32 platform backend is the same as Skyrim's.
#include "Gfx.h"
#include <condition_variable>
#include <imgui_impl_win32.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <string>

namespace renderer
{
	// Search-box mirrors (driving tool). Written on the render thread, read on the listener thread.
	std::atomic<float> g_searchRect[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	std::atomic<bool> g_searchActive{ false };
	std::atomic<int> g_searchLen{ 0 };
	std::mutex g_searchTextLock;
	std::string g_searchText;
	std::atomic<bool> g_wantTextInput{ false };
	std::atomic<bool> g_backspaceDown{ false };
	std::atomic<unsigned int> g_activeIdMirror{ 0 };
	std::atomic<bool> g_modCtrl{ false }, g_modShift{ false }, g_modAlt{ false };

	using strings::TR;

	namespace
	{
		std::atomic<bool> g_d3dReady{ false };

		// In-process capture state (not yet on D3D12 - CaptureBlocking answers with the reason).
		std::mutex g_captureLock;
		std::condition_variable g_captureCv;
		std::wstring g_capturePath;      // non-empty = a capture is pending
		bool g_captureDone = false;
		std::string g_captureError;
		std::atomic<bool> g_windowVisible{ false };
		// A mod's own window holds the keyboard and mouse (Skyrim 2.0.4) - see renderer::ConsumerWindowOwnsInput. Written
		// once per frame by the render thread, read by the window thread's input decision.
		std::atomic<bool> g_consumerInput{ false };
		// Window client coordinates -> swap chain image pixels (Skyrim 2.0.8); 1 unless the two differ.
		std::atomic<float> g_imageScaleX{ 1.0f }, g_imageScaleY{ 1.0f };

		// PAUSE WHILE OPEN (1.9.7, [Menu] bPauseGame). The window is an overlay, not a game menu, so it pauses the
		// game the way a pausing menu does: by holding one count on UI::numPausesGame. The render thread notices the
		// wanted state change; the count itself is only ever touched on the main thread, once up and once down, and
		// g_pauseHeld says whether this framework is holding one - so a close, a toggle flip or a save/load between
		// them can never leave the game paused, or take a count some other menu holds.
		bool g_pauseHeld = false;   // main thread only

		void SyncGamePause(bool a_want)
		{
			static bool lastWanted = false;   // render thread only
			if (a_want == lastWanted)
			{
				return;
			}
			lastWanted = a_want;
			// Skyrim held a count on UI::numPausesGame from the main thread. The Witcher 3 pauses through the game's own
			// CGame::Pause / Unpause with a reason of our own (Red3), called on the game thread - the same pause the game's
			// menus hold, so world time, actors and weather stop.
			red3::Post([a_want] { red3::SetGamePaused(a_want); });
		}
		std::atomic<void*> g_gameWindow{ nullptr };   // the game's HWND, set at D3DInit; read by the watchdog
		std::atomic<bool> g_justOpened{ false };  // set on the input thread, consumed on the render thread

		// Opened from the row in the game's own System menu, rather than from the hotkey or a menu
		// launcher. Geometry only - see SetMenuVisible.
		std::atomic<bool> g_nested{ false };

		// Set whenever the window is opened; consumed by the draw once it has placed the window at
		// its profile's geometry. Separate from g_justOpened, which is consumed elsewhere for the
		// focus grab - two consumers of one exchange() flag would race to see it.
		std::atomic<bool> g_applyGeometry{ false };
		// THE STANDARD PLACE (W3, the owner, 2026-10-05: "I just adjusted the position of AMF in the main menu, and I want
		// this to be the standard position for AMF to sit and scale to"): just right of the game menu's black column,
		// read back from where he dragged it (x 0.269 - the column's right edge - y 0.193, w 0.717, h 0.70 of the screen).
		constexpr float kColumnX = 0.269062f, kColumnY = 0.193333f, kColumnW = 0.716875f, kColumnH = 0.70f;
		std::atomic<bool> g_gameMenuOpen{ false };   // set from the script's GameMenuOpen (ModMenus' frame hook)

		// Knotwork frame texture (the embedded MO2-Skyrim border-image.png). Uploaded once at device-ready
		// through gfx:: (a D3D12 descriptor); used by DrawKnotworkFrame as an ImGui texture id.
		void* g_knotSRV = nullptr;
		// The framework's own Oblivion frame (MapEdgeBorder.h, the embroidered map's edge), uploaded beside it.
		void* g_mapSRV = nullptr;
		// Menu navigation state, promoted from static locals so the DevBench tool can drive and read
		// it from the listener thread (see DevBenchTool.cpp). Guarded by g_selLock; the render loop
		// copies in at frame start and out at frame end.
		std::mutex g_selLock;
		std::string g_selTab  = "mods";            // kept for the DevBench state JSON; the SMF shape has one list
		std::string g_selNode = "settings";        // side-list entry: settings|controls|help|mod
		int g_selMod = 0;
		// The open mod's tab bar, mirrored under this same lock for the DevBench state JSON. The
		// render loop owns the live values below; these are the copy the listener thread may read.
		std::string g_selTabName;
		int g_selTabIndex = 0;
		int g_selTabCount = 0;
		// Set when the selection is changed from OUTSIDE the UI (the amf.menu DevBench tool).
		// Without this the render loop copied its own state back every frame and ImGui's tab bar,
		// which owns its selected tab internally, stomped the external change immediately - the
		// automated pane sweep on 2026-08-28 showed every select() snapping back to "quests".
		bool g_selExternal = false;
		// The framework window's real rect on the last drawn frame (Skyrim 2.1.1), under g_selLock, for the DevBench state
		// JSON's mainWindow - so a drag of the top row or an edge can be measured rather than eyeballed.
		float g_mainX = 0.0f, g_mainY = 0.0f, g_mainW = 0.0f, g_mainH = 0.0f;

		// Draws the 78x78 knotwork PNG as a 9-slice frame around the given screen rect: the four
		// ornate corners at fixed size, the four edges stretched between them, the centre left
		// transparent so the window shows through. Faithful reproduction, so the art is drawn at
		// its own colour (white tint = no recolour). No-op if the texture failed to create.
		// The nine-slice itself, for ANY texture. Split out on 2026-09-09 so a UI author's own
		// frame art (skin::FrameTexture) goes through exactly the same geometry the built-in
		// knotwork always did - one implementation, so a supplied frame cannot draw differently
		// from the one this was proven on.
		// a_tile (2026-10-02): the edges REPEAT their middle strip at its own size instead of stretching it - for art
		// with a pattern along the edge (the map edge's stitches), which stretching would smear. The last repeat on
		// each side is cut short, UVs and all, so nothing overhangs the far corner.
		// a_dcs: the corner's size ON SCREEN when it differs from its size in the texture (0 = the same) - the map
		// edge is drawn from a texture at twice its size, scaled with the UI.
		void DrawNineSlice(ImDrawList* dl, void* a_srv, float W, float H, float cs,
						   const ImVec2& p0, const ImVec2& p1, bool a_tile = false, float a_dcs = 0.0f)
		{
			if (!a_srv || !dl || W <= 0.0f || H <= 0.0f || cs <= 0.0f)
			{
				return;
			}
			const float dcs = a_dcs > 0.0f ? a_dcs : cs;
			const float k = dcs / cs;   // screen pixels per texture pixel

			// UV split points (source), and screen split points (dest, corners at dcs px).
			const float u0 = 0.0f, u1 = cs / W, u2 = (W - cs) / W, u3 = 1.0f;
			const float v0 = 0.0f, v1 = cs / H, v2 = (H - cs) / H, v3 = 1.0f;
			const float x0 = p0.x, x1 = p0.x + dcs, x2 = p1.x - dcs, x3 = p1.x;
			const float y0 = p0.y, y1 = p0.y + dcs, y2 = p1.y - dcs, y3 = p1.y;

			// Degenerate guard: a window smaller than two corners would flip the middle slices.
			if (x2 <= x1 || y2 <= y1)
			{
				return;
			}

			const auto tex = reinterpret_cast<ImTextureID>(a_srv);
			const ImU32 white = IM_COL32_WHITE;
			auto slice = [&](float ax, float ay, float bx, float by, float au, float av, float bu, float bv) {
				dl->AddImage(tex, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), white);
			};

			// corners
			slice(x0, y0, x1, y1, u0, v0, u1, v1);  // top-left
			slice(x2, y0, x3, y1, u2, v0, u3, v1);  // top-right
			slice(x0, y2, x1, y3, u0, v2, u1, v3);  // bottom-left
			slice(x2, y2, x3, y3, u2, v2, u3, v3);  // bottom-right
			if (a_tile && W > 2.0f * cs && H > 2.0f * cs)
			{
				const float runU = (W - 2.0f * cs) * k;   // the strip's own length, on screen
				const float runV = (H - 2.0f * cs) * k;
				constexpr int kMaxRepeats = 512;
				int n = 0;
				for (float x = x1; x < x2 && n < kMaxRepeats; x += runU, ++n)
				{
					const float xe = std::min(x + runU, x2);
					const float ue = u1 + (u2 - u1) * ((xe - x) / runU);
					slice(x, y0, xe, y1, u1, v0, ue, v1);   // top
					slice(x, y2, xe, y3, u1, v2, ue, v3);   // bottom
				}
				n = 0;
				for (float y = y1; y < y2 && n < kMaxRepeats; y += runV, ++n)
				{
					const float ye = std::min(y + runV, y2);
					const float ve = v1 + (v2 - v1) * ((ye - y) / runV);
					slice(x0, y, x1, ye, u0, v1, u1, ve);   // left
					slice(x2, y, x3, ye, u2, v1, u3, ve);   // right
				}
				return;
			}
			// edges (stretched along their run)
			slice(x1, y0, x2, y1, u1, v0, u2, v1);  // top
			slice(x1, y2, x2, y3, u1, v2, u2, v3);  // bottom
			slice(x0, y1, x1, y2, u0, v1, u1, v2);  // left
			slice(x2, y1, x3, y2, u2, v1, u3, v2);  // right
		}

		// Draws the window frame: the UI author's own art when one is configured and loaded,
		// otherwise the embedded 78x78 knotwork. A supplied frame REPLACES the knotwork rather
		// than drawing over it - a theme in this project means replacement art, not a second
		// ornament on top of the first.
		void DrawKnotworkFrame(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1)
		{
			if (skin::HasFrame())
			{
				const ImVec2 sz = skin::FrameSize();
				DrawNineSlice(dl, skin::FrameTexture(), sz.x, sz.y, skin::FrameCorner(), p0, p1);
				return;
			}
			if (theme::GetActiveTheme().mapEdge && g_mapSRV)
			{
				// Scaled with the layout: the window padding is the corner + 8 px at the 1080p baseline times the UI scale
				// (1.67 at 1800 px tall), so the band grows with the text and always fits the padding it sits in. Drawn
				// at a fixed 26 px it came out thin and lost its stitches on a 4K screen (first in-game look, 2026-10-02).
				const float base = static_cast<float>(knotwork::kCorner) + 8.0f;
				const float scale = std::max(1.0f, ImGui::GetStyle().WindowPadding.x / base);
				DrawNineSlice(dl, g_mapSRV, static_cast<float>(mapedge::kWidth), static_cast<float>(mapedge::kHeight),
							  static_cast<float>(mapedge::kCorner), p0, p1, /*tile*/ true,
							  static_cast<float>(mapedge::kDrawCorner) * scale);
				return;
			}
			DrawNineSlice(dl, g_knotSRV, static_cast<float>(knotwork::kWidth),
						  static_cast<float>(knotwork::kHeight), static_cast<float>(knotwork::kCorner), p0, p1);
		}

		// The author's background, drawn INSIDE the given rect and clipped to it, behind whatever
		// the window then draws. A small image tiles at its own pixel size; a large one is
		// stretched to fill. Which one it is is decided by the image, not by a fifth INI key.
		void DrawSkinBackground(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1)
		{
			if (!dl || !skin::HasBackground() || p1.x <= p0.x || p1.y <= p0.y)
			{
				return;
			}
			const auto  tex = reinterpret_cast<ImTextureID>(skin::BackgroundTexture());
			const ImVec2 sz = skin::BackgroundSize();
			// A UI author's background fades with the window when See-through window is on (Skyrim 2.1.1).
			const auto& sv = settings::Get();
			const int fade = sv.seeThrough ? std::clamp(sv.windowOpacity, 5, 100) : 100;
			const ImU32 white = IM_COL32(255, 255, 255, fade * 255 / 100);

			if (!skin::BackgroundTiles())
			{
				dl->AddImage(tex, p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), white);
				return;
			}

			// Tiled. Clip so the last row and column are cut rather than overhanging the window,
			// and cap the count so a 1px image cannot spend the frame budget on draw calls.
			dl->PushClipRect(p0, p1, true);
			const float tw = sz.x > 0.0f ? sz.x : 1.0f;
			const float th = sz.y > 0.0f ? sz.y : 1.0f;
			constexpr int kMaxTiles = 4096;
			int drawn = 0;
			for (float y = p0.y; y < p1.y && drawn < kMaxTiles; y += th)
			{
				for (float x = p0.x; x < p1.x && drawn < kMaxTiles; x += tw, ++drawn)
				{
					dl->AddImage(tex, ImVec2(x, y), ImVec2(x + tw, y + th),
								 ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), white);
				}
			}
			dl->PopClipRect();
		}

		// The knotwork frames a rect from just OUTSIDE it (author, 2026-09-01): drawn exactly on a
		// pane's rect, the art's own hairlines land on the pane's 1px border and the two read as one
		// smudged double line. Pushed out by a few pixels it reads as a frame AROUND the box, and the
		// box's own border and corners stay legible - which is what the Untarnished theme gets for
		// free by having no art at all.
		constexpr float kKnotOutset = 4.0f;

		void DrawKnotworkAround(ImDrawList* dl, ImVec2 p0, ImVec2 p1)
		{
			DrawKnotworkFrame(dl, ImVec2(p0.x - kKnotOutset, p0.y - kKnotOutset),
							  ImVec2(p1.x + kKnotOutset, p1.y + kKnotOutset));
		}

		// M1.1 (the author's smoke-test feedback): at 3200x1800 the stock ImGui font and a fixed
		// 520x340 window are "far too small". One scale factor, derived from the real display
		// height against 1080p as the baseline, applied to the font, the style metrics and the
		// default window size together so everything stays proportioned.
		float g_uiScale = 1.0f;

		// THE GAME'S FRAME ROUND A HIGHLIGHTED ENTRY (the owner, 2026-10-05: "use the game's ... frame art for whenever the
		// cursor or the control nav box is hovering over boxes. So it looks like the game"). The game's main menu frames the
		// entry under the cursor (LOAD GAME, the Select prompt) with a thin outer line and an inner line whose corners step
		// in. ImGui hands the hovered item and the nav item to RecordGameFrame (the [AMF] hook in extern/imgui); they are
		// drawn after everything else, so no item's own fill covers them. Drawn, not copied: no game art is used.
		struct GameFrameMark
		{
			ImDrawList* drawList = nullptr;
			ImRect      rect;
			ImRect      clip;
			bool        nav = false;
		};
		std::vector<GameFrameMark> g_gameFrames;   // render thread: this frame's highlighted items

		void DrawGameFrame(ImDrawList* a_dl, const ImRect& a_item, ImU32 a_col, float a_scale)
		{
			const float t = std::max(1.0f, std::round(a_scale));   // line thickness
			const float pad = 2.0f * a_scale;                       // the frame sits just outside the item
			const float gap = 3.0f * a_scale;                       // outer line to inner line
			const float notch = 4.0f * a_scale;                     // the inner line's stepped corners
			const ImRect r(a_item.Min.x - pad, a_item.Min.y - pad, a_item.Max.x + pad, a_item.Max.y + pad);
			a_dl->AddRect(r.Min, r.Max, a_col, 0.0f, 0, t);
			const ImVec2 a(r.Min.x + gap, r.Min.y + gap);
			const ImVec2 b(r.Max.x - gap, r.Max.y - gap);
			if (b.x - a.x <= 2.0f * notch || b.y - a.y <= 2.0f * notch) {
				return;   // too small for the inner line: the outer one alone
			}
			const ImVec2 pts[] = {
				{ a.x + notch, a.y }, { b.x - notch, a.y }, { b.x - notch, a.y + notch }, { b.x, a.y + notch },
				{ b.x, b.y - notch }, { b.x - notch, b.y - notch }, { b.x - notch, b.y }, { a.x + notch, b.y },
				{ a.x + notch, b.y - notch }, { a.x, b.y - notch }, { a.x, a.y + notch }, { a.x + notch, a.y + notch },
			};
			a_dl->AddPolyline(pts, IM_ARRAYSIZE(pts), a_col, ImDrawFlags_Closed, t);
		}

		// Set around an item that must never be framed: the window's invisible top-row move handle (the owner, 2026-10-05:
		// "it also selects the top bar, which shouldn't be having a frame that goes around it as it's invisible").
		bool g_noGameFrame = false;

		void RecordGameFrameHook(ImDrawList* a_dl, const ImRect& a_item, const ImRect& a_clip, bool a_nav)
		{
			if (g_noGameFrame) {
				return;
			}
			// a caller that hid ImGui's nav highlight on purpose (a mod page's row draws one frame round label AND control)
			if (a_nav && ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight).w <= 0.0f) {
				return;
			}
			g_gameFrames.push_back({ a_dl, a_item, a_clip, a_nav });
		}

		void FlushGameFrames()
		{
			const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			const ImU32  navCol = ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.95f));
			const ImU32  hoverCol = ImGui::GetColorU32(ImVec4(text.x, text.y, text.z, 0.55f));
			for (std::size_t i = 0; i < g_gameFrames.size(); ++i) {
				const GameFrameMark& m = g_gameFrames[i];
				if (!m.drawList) {
					continue;
				}
				if (!m.nav) {
					// one hover frame: the last item ImGui found under the mouse, and none where the nav frame already is
					bool skip = false;
					for (std::size_t j = 0; j < g_gameFrames.size() && !skip; ++j) {
						const GameFrameMark& o = g_gameFrames[j];
						skip = (j > i && !o.nav) ||
						       (o.nav && o.drawList == m.drawList && o.rect.Min.x == m.rect.Min.x && o.rect.Min.y == m.rect.Min.y);
					}
					if (skip) {
						continue;
					}
				}
				m.drawList->PushClipRect(m.clip.Min, m.clip.Max, false);
				DrawGameFrame(m.drawList, m.rect, m.nav ? navCol : hoverCol, g_uiScale);
				m.drawList->PopClipRect();
			}
			g_gameFrames.clear();
		}

		// FONT (1.4.2). The default ImGui font is ProggyClean, a 13px BITMAP face; the old code
		// magnified it with FontGlobalScale = uiScale * textScale (~2.17x at 3200x1800), which is
		// exactly why the text looked pixelated. Instead we rasterise a real TrueType face at the
		// NATIVE pixel size for the display, and keep FontGlobalScale at 1.0 so nothing is
		// magnified. Changing the text-size slider rebuilds the atlas rather than stretching it.
		constexpr float kBaseFontPx = 16.0f;   // at the 1080p baseline, before uiScale/textScale
		std::atomic<bool> g_fontRebuildPending{ false };

		// When Save was last pressed, so "saved" can appear beside the button for a few seconds
		// rather than the press doing nothing visible. settings::Save() returns nothing, so there
		// is no honest success/failure to report here - only that the write was asked for.
		double g_menuListSavedAt = 0.0;

		// Keyboard-loss diagnostic (1.9.5). Stamped WHEN THEY HAPPEN, read afterwards.
		int g_frameFocusHere = -1;      // SetKeyboardFocusHere() was called on this frame
		int g_frameWindowFocus = -1;    // SetNextWindowFocus() was called on this frame
		int g_frameNavConsumed = -1;    // the nav-to-selected flag was consumed TRUE on this frame
		int g_frameSearchDrawn = -1;    // the mod-search box was actually submitted on this frame

		// The rename asked for from a mod row's right-click menu. The modal itself is drawn once,
		// outside the list, because a popup opened from inside the loop would otherwise be
		// submitted once per row and fight itself for the id.
		std::string g_renameTarget;
		char g_renameBuffer[64] = {};
		bool g_renameOpenPending = false;

		// Layout presets (Skyrim 2.0.3): the name being typed, and the last action's result for a few seconds.
		char g_presetName[64] = {};
		std::string g_presetStatus;
		double g_presetStatusAt = 0.0;

		// SEPARATOR NAMES IN THE LANGUAGE SHOWN (Skyrim 2.1.1, the owner: "when you change the language, the separators did
		// not change their language"). The Witcher 3 build has no MCM sort and so no category separators; the names the
		// MENU gives a separator are the default "New separator" (stored in the language of the day when the player
		// keeps it) and the fallback for one with no name. Either shows in the language now picked; anything the player
		// typed is shown exactly as typed. Called by personalization with its lock held - TR and strings only.
		std::string ShownSeparatorName(const std::string& a_stored)
		{
			if (a_stored.empty()) { return TR("AMF_SeparatorUnnamed", "Separator"); }
			const auto lower = [](std::string a_s) {
				for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
				return a_s;
			};
			const std::string stored = lower(a_stored);
			if (stored == "new separator") { return TR("AMF_SeparatorDefaultName", "New separator"); }
			for (const std::string& text : strings::EveryLanguage("AMF_SeparatorDefaultName"))
			{
				if (lower(text) == stored) { return TR("AMF_SeparatorDefaultName", "New separator"); }
			}
			return a_stored;
		}
		const bool g_separatorFilterRegistered = (personalization::SetSeparatorNameFilter(&ShownSeparatorName), true);
	}

	// Strings::SetLanguage and kDataLoaded ask for a new atlas holding the language's glyphs.
	void RequestFontRebuild() { g_fontRebuildPending = true; }

	namespace
	{
		std::mutex g_fontProbeLock;
		FontProbe g_fontProbe;
	}

	FontProbe GetFontProbe()
	{
		std::scoped_lock l(g_fontProbeLock);
		return g_fontProbe;
	}

	namespace
	{

		// Ordered candidates: a clean sans that matches Skyrim's own menu lettering, then fallbacks.
		// A user-supplied path (sFontPath in the INI) wins when set, so any .ttf can be dropped in.
		const char* const kFontCandidates[] = {
			"C:/Windows/Fonts/segoeui.ttf",
			"C:/Windows/Fonts/calibri.ttf",
			"C:/Windows/Fonts/trebuc.ttf",
		};

		// Selectable faces for the FONT PICKER (design decision, 2026-08-28: "a separate selector from the
		// theme that lets you choose a font"). Deliberately its own control, not a theme property -
		// a theme sets colours; the face is an independent choice, so any font works with any theme.
		// Scanned once from the Windows font directory plus AMF's own optional fonts folder, so a
		// .ttf dropped in beside the plugin shows up in the list.
		struct FontChoice
		{
			std::string label;  // shown in the combo
			std::string path;   // empty = "Default (auto)"
		};
		std::vector<FontChoice> g_fontChoices;

		void ScanFonts()
		{
			g_fontChoices.clear();
			g_fontChoices.push_back({ "Default (auto)", "" });

			// Curated, widely-present Windows faces - a full enumeration of C:/Windows/Fonts would
			// be hundreds of entries, most of them useless for a game menu.
			const std::pair<const char*, const char*> known[] = {
				{ "Segoe UI",        "C:/Windows/Fonts/segoeui.ttf" },
				{ "Segoe UI Semibold","C:/Windows/Fonts/seguisb.ttf" },
				{ "Calibri",         "C:/Windows/Fonts/calibri.ttf" },
				{ "Trebuchet MS",    "C:/Windows/Fonts/trebuc.ttf" },
				{ "Georgia",         "C:/Windows/Fonts/georgia.ttf" },
				{ "Constantia",      "C:/Windows/Fonts/constan.ttf" },
				{ "Palatino Linotype","C:/Windows/Fonts/pala.ttf" },
				{ "Times New Roman", "C:/Windows/Fonts/times.ttf" },
				{ "Cambria",         "C:/Windows/Fonts/cambria.ttc" },
			};
			for (const auto& k : known)
			{
				std::error_code ec;
				if (std::filesystem::exists(k.second, ec)) { g_fontChoices.push_back({ k.first, k.second }); }
			}

			// Anything the user drops into OBSE/Plugins/ApocryphaMenuFramework/fonts/.
			const std::filesystem::path dir{ paths::Data() / "fonts" };
			std::error_code ec;
			if (std::filesystem::is_directory(dir, ec))
			{
				for (const auto& e : std::filesystem::directory_iterator(dir, ec))
				{
					if (!e.is_regular_file(ec)) { continue; }
					const auto ext = e.path().extension().string();
					if (_stricmp(ext.c_str(), ".ttf") == 0 || _stricmp(ext.c_str(), ".otf") == 0)
					{
						g_fontChoices.push_back({ e.path().stem().string(), e.path().string() });
					}
				}
			}
			logger::info("font picker: {} face(s) available", g_fontChoices.size());
		}

		// Rebuilds the font atlas at the current scale. Call OUTSIDE a frame (before NewFrame).
		void BuildFonts()
		{
			ImGuiIO& io = ImGui::GetIO();
			const float px = kBaseFontPx * g_uiScale * settings::Get().textScale;

			io.Fonts->Clear();
			ImFont* loaded = nullptr;
			std::string loadedPath;   // the text face's file, which each Font Awesome icon face is built on (Skyrim 2.0.4)
			ImFont* iconFonts[consumer::kIconFaceCount] = {};
			// Clear() freed every ImFont: the consumer surface must not push an icon face from the old atlas, whatever
			// happens below (the built-in fallback face returns before the faces are handed back).
			for (int f = 0; f < consumer::kIconFaceCount; ++f) { consumer::SetIconFont(f, nullptr); }

			// GLYPH RANGES (1.6.4, language support): the atlas holds the default Latin set plus
			// every character that appears in the loaded translation - Cyrillic, Polish and Czech
			// letters, kana, hanzi - built from the strings themselves, so no per-language table
			// can be wrong or incomplete. Static so the ranges outlive Build().
			// 1.8.9 (littlefot's Wheeler report, 2026-09-17, the same class checked here): the atlas
			// also holds the BUILT-IN ranges of the scripts in play - the framework's language AND the
			// game's own sLanguage - because not every character a page draws comes from a translation
			// file: a Japanese game lists Japanese item names in Item Explorer whatever language the
			// framework's pages are set to, and those kanji were in no file the builder had read.
			static ImVector<ImWchar> s_ranges;
			const std::string lang = strings::Language();
			const std::string gameLang = strings::GameLanguageSetting();
			{
				ImFontGlyphRangesBuilder builder;
				builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
				for (const std::string& l : { lang, gameLang })
				{
					const ImWchar* r = nullptr;
					if (l == "japanese") { r = io.Fonts->GetGlyphRangesJapanese(); }
					else if (l == "korean") { r = io.Fonts->GetGlyphRangesKorean(); }
					else if (l == "chinese" || l == "schinese" || l == "tchinese") { r = io.Fonts->GetGlyphRangesChineseSimplifiedCommon(); }
					else if (l == "russian" || l == "ukrainian" || l == "bulgarian") { r = io.Fonts->GetGlyphRangesCyrillic(); }
					else if (l == "thai") { r = io.Fonts->GetGlyphRangesThai(); }
					else if (l == "vietnamese") { r = io.Fonts->GetGlyphRangesVietnamese(); }
					if (r) { builder.AddRanges(r); }
				}
				builder.AddText(strings::AllText().c_str());
				s_ranges.clear();
				builder.BuildRanges(&s_ranges);
			}

			const std::string& custom = settings::Get().fontPath;
			if (!custom.empty())
			{
				loaded = io.Fonts->AddFontFromFileTTF(custom.c_str(), px, nullptr, s_ranges.Data);
				if (!loaded) { logger::warn("font: sFontPath \"{}\" could not be loaded; falling back", custom); }
				else { loadedPath = custom; }
			}
			for (const char* cand : kFontCandidates)
			{
				if (loaded) { break; }
				loaded = io.Fonts->AddFontFromFileTTF(cand, px, nullptr, s_ranges.Data);
				if (loaded) { loadedPath = cand; logger::info("font: rasterised \"{}\" at {:.1f}px", cand, px); }
			}
			// A FALLBACK FACE merged in for the glyphs the chosen face lacks (MergeMode adds only what
			// is missing): the Latin faces above carry Cyrillic and Latin Extended but no kana or
			// hanzi, so Japanese and Chinese draw from a system CJK face. Harmless for English.
			if (loaded)
			{
				// The script that picks the preferred face: the framework's language when it is CJK,
				// otherwise the game's (1.8.9 - a Japanese game with English pages still needs kana).
				auto isCjk = [](const std::string& l) { return l == "japanese" || l == "korean" || l == "chinese" || l == "schinese" || l == "tchinese"; };
				const std::string cjkLang = isCjk(lang) ? lang : isCjk(gameLang) ? gameLang : lang;
				// Per language first (the owner's priority order: Japanese, Korean, Chinese, Russian), then
				// every CJK/Hangul face Windows ships, so a missing preferred face still finds glyphs.
				const char* const cjk[] = {
					cjkLang == "japanese" ? "C:/Windows/Fonts/meiryo.ttc" : cjkLang == "korean" ? "C:/Windows/Fonts/malgun.ttf" : "C:/Windows/Fonts/msyh.ttc",
					cjkLang == "japanese" ? "C:/Windows/Fonts/YuGothM.ttc" : cjkLang == "korean" ? "C:/Windows/Fonts/malgunbd.ttf" : "C:/Windows/Fonts/simsun.ttc",
					"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/msgothic.ttc", "C:/Windows/Fonts/simsun.ttc" };
				if (lang != "english" || gameLang != "english")
				{
					ImFontConfig merge;
					merge.MergeMode = true;
					merge.PixelSnapH = true;
					for (const char* face : cjk)
					{
						std::error_code ec;
						if (!std::filesystem::exists(face, ec)) { continue; }
						if (io.Fonts->AddFontFromFileTTF(face, px, &merge, s_ranges.Data))
						{
							logger::info("font: merged \"{}\" for the glyphs \"{}\" (game \"{}\") needs", face, lang, gameLang);
							break;
						}
					}
				}

				// FONT AWESOME ICON FACES (Skyrim 2.0.4). A mod drawing its own window through the SMF-compatible API pushes a
				// Font Awesome face by name and draws its icons (U+E000-U+F8FF); with only the text face in the atlas every
				// icon drew as "?" (RaceMenu Atelier on Skyrim, mmmizuhara, 2026-10-03). Each face is a font of its own -
				// solid and regular share codepoints, so they cannot share one - made of the text face (Latin, Latin
				// Extended-A and Cyrillic only, so "<icon> Label" works without copying a CJK set three times) with that
				// style's icons merged in. Only the faces a mod has actually pushed are built (consumer::IconFaceWanted), so
				// a load order with no icon-using mod keeps the atlas it had.
				//
				// SIZE AND BASELINE. A merged glyph sits on the text face's baseline (ImGui offsets every glyph by the first
				// font's ascent), so no GlyphOffset is needed. At 0.8 of the text size an icon is one text em tall - the size
				// Font Awesome draws beside text of the same size on a web page. GlyphMinAdvanceX gives every icon at least a
				// square cell, centred, so a column of icons lines up. OversampleH 1: icons are pixel-snapped shapes that
				// gain nothing from horizontal oversampling, and it halves the atlas space they take.
				//
				// Files: bin\x64_dx12\AMF\icons\ (paths::Data(), the exe's folder - under Mod Organizer 2 the merged virtual
				// one), shipped with the SIL OFL 1.1 text beside them. A missing file is logged once and that face keeps
				// the old behaviour (the current font is pushed).
				struct IconFile { int face; const char* file; };
				static constexpr IconFile kIconFiles[] = {
					{ consumer::kIconSolid, "fa-solid-900.ttf" },
					{ consumer::kIconRegular, "fa-regular-400.ttf" },
					{ consumer::kIconBrands, "fa-brands-400.ttf" },
				};
				static const ImWchar kIconTextRanges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x0400, 0x04FF, 0 };
				static const ImWchar kIconRanges[] = { 0xE000, 0xF8FF, 0 };
				static bool s_missingLogged[consumer::kIconFaceCount] = {};
				const float iconPx = std::round(px * 0.8f);
				for (const IconFile& ic : kIconFiles)
				{
					if (!consumer::IconFaceWanted(ic.face)) { continue; }
					const std::string path = paths::Str((std::string("icons/") + ic.file).c_str());
					std::error_code ec;
					if (!std::filesystem::exists(path, ec))
					{
						if (!s_missingLogged[ic.face])
						{
							s_missingLogged[ic.face] = true;
							logger::warn("font: \"{}\" is missing - a mod asked for that Font Awesome face, so its icons "
										 "draw as \"?\" (reinstall Apocrypha Menu Framework)", path);
						}
						continue;
					}
					ImFont* const iconFont = io.Fonts->AddFontFromFileTTF(loadedPath.c_str(), px, nullptr, kIconTextRanges);
					if (!iconFont) { continue; }
					ImFontConfig icons;
					icons.MergeMode = true;
					icons.PixelSnapH = true;
					icons.OversampleH = 1;
					icons.GlyphMinAdvanceX = iconPx;
					if (!io.Fonts->AddFontFromFileTTF(path.c_str(), iconPx, &icons, kIconRanges))
					{
						logger::warn("font: \"{}\" could not be read as a font - that icon face stays text only", path);
					}
					iconFonts[ic.face] = iconFont;
				}
			}
			if (!loaded)
			{
				// Never fail to render: fall back to the built-in face, magnified as before.
				io.Fonts->AddFontDefault();
				io.FontGlobalScale = g_uiScale * settings::Get().textScale;
				logger::warn("font: no TrueType face could be loaded; using the built-in bitmap font");
				return;
			}

			io.FontGlobalScale = 1.0f;  // native size - no magnification, so no pixelation
			// THE ATLAS HEIGHT IS NOT ROUNDED UP TO A POWER OF TWO (Skyrim 2.0.4). ImGui does that by default, and with the
			// icon faces it nearly doubled the texture for nothing (measured on the Skyrim build at 34.7 px: English with all
			// three faces 2048x2048 -> 2048x1397, Japanese without any 2048x4096 -> 2048x2857). D3D12 takes any texture
			// height, so this also trims the CJK atlas.
			io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
			io.Fonts->Build();

			// Hand the icon faces to the consumer surface and say once per atlas which are in it. Atlas builds are rare
			// (start-up, a language, face or size change, a face first asked for), so this is not a per-frame line.
			{
				std::string faces;
				static constexpr const char* kFaceNames[consumer::kIconFaceCount] = { "solid", "regular", "brands" };
				for (int f = 0; f < consumer::kIconFaceCount; ++f)
				{
					consumer::SetIconFont(f, iconFonts[f]);
					if (!iconFonts[f]) { continue; }
					const int icons = iconFonts[f]->FindGlyphNoFallback(f == consumer::kIconBrands ? 0xF09B : 0xF007) ? 1 : 0;
					faces += (faces.empty() ? "" : ", ") + std::string(kFaceNames[f]) + " (" +
							 std::to_string(iconFonts[f]->Glyphs.Size) + " glyphs" + (icons ? "" : ", NO icon glyphs") + ")";
				}
				if (!faces.empty()) { logger::info("font: Font Awesome icon faces in the atlas: {}", faces); }
			}

			// What the atlas can draw, one probe glyph per script (1.8.9): hiragana A, hangul HAN, the
			// hanzi for water, Cyrillic ZHE. Read back by the driving tool so a language switch is
			// proved by the atlas rather than a capture.
			{
				FontProbe probe;
				probe.language = lang;
				probe.gameLanguage = gameLang;
				probe.glyphs = loaded->Glyphs.Size;
				probe.hasKana = loaded->FindGlyphNoFallback(0x3042) != nullptr;
				probe.hasHangul = loaded->FindGlyphNoFallback(0xD55C) != nullptr;
				probe.hasHanzi = loaded->FindGlyphNoFallback(0x6C34) != nullptr;
				probe.hasCyrillic = loaded->FindGlyphNoFallback(0x0416) != nullptr;
				// GetTexDataAsRGBA32 writes through its pixel pointer unconditionally - a null there is a
				// crash at the first atlas build (boot, 2026-09-17 14:24), not a "skip".
				unsigned char* pixels = nullptr;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &probe.atlasWidth, &probe.atlasHeight);
				std::scoped_lock l(g_fontProbeLock);
				probe.builds = g_fontProbe.builds + 1;
				g_fontProbe = probe;
				logger::info("font: atlas {} built for \"{}\" (game \"{}\"): {} glyphs, {}x{}, kana {} hangul {} hanzi {} cyrillic {}",
					probe.builds, lang, gameLang, probe.glyphs, probe.atlasWidth, probe.atlasHeight, probe.hasKana, probe.hasHangul, probe.hasHanzi, probe.hasCyrillic);
			}
		}

		// -----------------------------------------------------------------------------------
		// Device ready - one shot, called by the overlay once the game's swap chain, device and
		// presenting queue are known (Overlay.cpp). Skyrim reached this point through a hooked
		// D3D-init call; Oblivion Remastered is found through the DXGI factory instead, so there
		// is no call site to guard - the overlay only calls this with every object in hand.
		// -----------------------------------------------------------------------------------
		bool DeviceReady(HWND a_hwnd, unsigned a_width, unsigned a_height)
		{
			if (g_d3dReady.exchange(true))
			{
				return true;  // once-only
			}
			g_gameWindow.store(reinterpret_cast<void*>(a_hwnd), std::memory_order_release);   // for the watchdog's foreground check

			ImGui::CreateContext();

			// Trickle-off: apply every queued input event in the same NewFrame. With
			// trickling, the Win32 backend's OS-cursor poll and our software cursor could
			// land in DIFFERENT frames and the cursor visibly alternated between the two
			// (the 1.1.0 flicker). All sources resolve within one frame, last-writer wins,
			// and the last writer is always our integrated position.
			ImGui::GetIO().ConfigInputTrickleEventQueue = false;

			// THE WINDOW MOVES BY ITS TITLE BAR AND NOTHING ELSE (the owner, 2026-09-19): a drag meant
			// for a slider, a 3D preview or a row of items must never move the framework's window.
			ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
			logger::info("window move: title bar only = {}", ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly);

			ImGui_ImplWin32_Init(a_hwnd);
			if (!gfx::InitImGuiBackend())
			{
				logger::error("ImGui's D3D12 backend did not start; framework stays inert");
				g_d3dReady = false;
				return false;
			}

			// Upload the embedded knotwork frame once. Failure is non-fatal: DrawKnotworkFrame
			// no-ops and the theme still applies its colours, just without the ornament.
			g_knotSRV = gfx::CreateTextureRGBA(knotwork::kRGBA, static_cast<int>(knotwork::kWidth), static_cast<int>(knotwork::kHeight));
			if (!g_knotSRV)
			{
				logger::warn("knotwork: the frame texture could not be uploaded; frame ornament disabled");
			}
			g_mapSRV = gfx::CreateTextureRGBA(mapedge::kRGBA, static_cast<int>(mapedge::kWidth), static_cast<int>(mapedge::kHeight));
			if (!g_mapSRV)
			{
				logger::warn("map edge: the frame texture could not be uploaded; the Oblivion theme falls back to the knotwork");
			}

			theme::Apply();
			// After theme::Apply has registered the themes, so an active theme's own art is found.
			skin::Reload();

			g_uiScale = a_height > 0 ? static_cast<float>(a_height) / 1080.0f : 1.0f;
			if (g_uiScale < 1.0f)
			{
				g_uiScale = 1.0f;  // never shrink below the 1080p baseline
			}

			// Text is rasterised at native size for this display (see BuildFonts) rather than
			// magnifying the built-in bitmap font, which is what made it look pixelated.
			ScanFonts();
			BuildFonts();
			watchdog::Init();
			{ std::string why; watchdog::InstallFastExit(why); }
			ImGui::GetStyle().ScaleAllSizes(g_uiScale);

			static const std::string s_layoutIni = paths::Str("ApocryphaMenuFramework_layout.ini");
			ImGui::GetIO().IniFilename = s_layoutIni.c_str();

			logger::info("UI scale set to {:.2f} for a {}px-tall display (1080p baseline)", g_uiScale, a_height);
			logger::info("ImGui initialized on the game's D3D12 device (window {}, {}x{}); theme applied",
						 static_cast<const void*>(a_hwnd), a_width, a_height);
			return true;
		}
		// -----------------------------------------------------------------------------------
		// The framework window: SMF's two-pane structure (design decision, 2026-08-27 - left pane lists
		// the mods' menus, right pane shows the selected menu's settings). M3's registry fills
		// the left pane; until then the framework's own settings page is the only entry.
		// -----------------------------------------------------------------------------------
		// Which pane nav should be moved into on the NEXT frame (0 = leave it alone, 1 = the mod
		// list, 2 = the options). Set when the player pushes across the border; applied by
		// SetNextWindowFocus before that child begins, which also makes ImGui pick a sensible item
		// inside it (the first one, or the one it was last on).
		// Atomic because a driving tool sets it from devbench's listener thread (renderer::FocusPane).
		std::atomic<int> g_focusPane{ 0 };

		// When nav returns to the mod list, put the cursor back on the entry whose page is open -
		// not wherever the list's cursor happened to be left (author, 2026-09-01: "if I select
		// settings and go right and I scroll to the bottom and then I go back left then it should
		// take me back to the settings menu selector not to the bottom of the left pane").
		std::atomic<bool> g_navToSelected{ false };

		// Tab navigation inside the options pane (the author, 2026-09-08: "using the left d pad
		// doesnt move out of the menu until you get to the begining of the tabs otherwise you cant
		// use the lft d pad in the menu except to exit the menu"). A mod with several pages draws a
		// tab bar, and the D-pad now WALKS that bar: left steps back one tab, and only a left press
		// already at the FIRST tab hands nav back to the mod list. Before this the very first left
		// press left the pane, so the twelve sections of a mod like Character Progression Control
		// could not be reached with the D-pad at all - left's only use inside a menu was to leave it.
		//
		// Right steps FORWARD a tab only while the cursor is on the bar itself, and that asymmetry
		// is deliberate. Left was already spent on leaving the pane, so taking it costs nothing;
		// right is still ImGui's own move-between-widgets key down in the page and stays that way.
		// On the bar nothing is lost either: ImGui moves the nav highlight along the tabs but does
		// NOT select the one it lands on - selecting needs an activate press - so all that changes
		// is that the highlight and the selection now move together.
		int  g_tabCount = 0;          // tabs the open mod drew this frame; 0 or 1 = no bar to walk
		int  g_tabIndex = 0;          // which of them is selected - re-read from the bar every frame,
		                              // so a mouse click or the tab-list popup keeps it honest
		int  g_tabRequest = -1;       // tab to force-select on the next frame; -1 = none
		bool g_tabBarHasNav = false;  // the cursor is on the bar itself, not down in the page
		// The framework page's open tab by its ENGLISH name (W3 1.0.4), for the state JSON's "page": a tool reads it, so it
		// does not change with the language (rule 66). A mod's page reports its own tab name instead.
		std::string g_tabName;
		// A page's OWN tab bar, declared by the page itself (AMF_DeclareInnerTabs).
		//
		// The framework can only measure the bar IT submits, so a consumer that draws its own BeginTabBar
		// inside a page is invisible to the nav decision and the D-pad does nothing there (the owner,
		// 2026-09-16: "the nav box behaves properly on the main tabs ... but when going to the other tabs
		// within those tabs, it does not"). It cannot be fixed by guessing from outside, so the page says
		// what it has and reads back which one to open. A mod that never calls this is unaffected - which
		// is the point: no other author has to patch anything.
		int g_innerCount = 0;    // tabs the open page declared THIS frame; 0 = it has none
		int g_innerIndex = 0;    // which of them the page says is open
		int g_innerRequest = -1; // the tab the page should open next frame; -1 = no request
		// 1.8.8: a sideways press inside the content pane is FIRST offered to ImGui's own item navigation,
		// and steps a tab only when ImGui found nothing to move to. The press is noted on the frame it
		// happens (+1 right, -1 left) and decided on the next one, when GImGui->NavJustMovedToId says
		// whether the cursor landed on another widget. The owner, 2026-09-16, in Item Explorer: "dpad
		// right sends you to the favorites tab instead of the add item box" - the page's own widgets sit
		// side by side (SameLine), and the tab step used to win before ImGui had a chance to move.
		int g_pendingTabStep = 0;
		bool g_innerFresh = false;  // the declaration was renewed this frame

		// Where a driving tool's synthetic press lands (amf.menu op=nav). It is read in exactly the
		// place a real D-pad press is read, so the tool exercises this logic rather than a shortcut
		// past it (rule 64). 0 = nothing pending, 1 = left, 2 = right.
		std::atomic<int> g_navRequest{ 0 };

		void DrawMenuListSection();  // defined below, next to the other leaf panes

		// A page tab that ImGui's nav INIT passes over (W3 1.0.3 test, 2026-10-05). When the highlight is put into the
		// options pane it starts on the page's first control, not on the first tab of its bar; the tabs stay reachable with
		// Up and still take a press of their own. ImGui falls back to the tab when the page has no control at all.
		bool BeginPageTab(const char* a_label, ImGuiTabItemFlags a_flags)
		{
			ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
			const bool open = ImGui::BeginTabItem(a_label, nullptr, a_flags);
			ImGui::PopItemFlag();
			return open;
		}

		// THE OPTIONS PANE STARTS ITS HIGHLIGHT AFRESH (W3 1.0.3 test, 2026-10-05: on Settings > General a Down after
		// `focus pane=options` moved nothing, and on Auto Take All two Downs landed on the second-to-last row). The pane is
		// ONE ImGui child window ("##content") for every page, and FocusWindow restores that window's remembered NavLastIds
		// and NavRectRel - an item and a rect from whatever page last had the highlight there. A Down was scored from that
		// old rect: below every item of a short page it found nothing (General), on a longer one it landed far down
		// (the mod page). So whenever nav is put into the pane, or the page under a live highlight changes, the memory is
		// cleared and ImGui is asked for a fresh init: the highlight lands on the page's first control.
		bool g_contentNavReset = false;   // render thread: a page change under the highlight asks for a restart next frame
		void StartContentNav(ImGuiWindow* a_pane, const char* a_why)
		{
			ImGuiContext* g = GImGui;
			if (!g || !a_pane) { return; }
			a_pane->NavLastChildNavWindow = nullptr;   // not a child window of an earlier page
			if (g->NavWindow != a_pane) { ImGui::FocusWindow(a_pane); }
			if (g->NavWindow != a_pane)
			{
				logger::debug("nav: options pane not focused ({}) - '{}' holds the nav, no restart", a_why,
							  g->NavWindow ? g->NavWindow->Name : "(none)");
				return;
			}
			a_pane->NavLastIds[0] = 0;
			a_pane->NavRectRel[0] = ImRect();
			ImGui::NavInitWindow(a_pane, true);
			g->NavDisableHighlight = false;   // entered by the pad, the keyboard or the driving tool: show where it is
			logger::debug("nav: options pane highlight restarted on its first control ({})", a_why);
		}

		// The side list as it is drawn (Skyrim 2.1.0's rule): an entry whose every page is hidden (AMF_SetPageVisible)
		// has no row, and a separator counts only the rows it shows. The state JSON's displayOrder reads the same list, so
		// a driving tool sees what the player sees (W3 1.0.3 test: displayOrder still listed a mod hidden from the list).
		bool AllPagesHidden(const std::vector<registry::Entry>& a_entries, const personalization::DisplayEntry& a_row)
		{
			if (a_row.separator || a_row.registryIndex < 0 || a_row.registryIndex >= static_cast<int>(a_entries.size())) { return false; }
			const auto& pages = a_entries[static_cast<std::size_t>(a_row.registryIndex)].pages;
			return !pages.empty() && std::all_of(pages.begin(), pages.end(), [](const registry::Page& p) { return p.hidden; });
		}
		std::vector<personalization::DisplayEntry> ShownOrder(const std::vector<registry::Entry>& a_entries)
		{
			std::vector<personalization::DisplayEntry> rows = personalization::Order(a_entries);
			std::erase_if(rows, [&](const personalization::DisplayEntry& r) { return AllPagesHidden(a_entries, r); });
			personalization::DisplayEntry* separator = nullptr;
			for (auto& r : rows)
			{
				if (r.separator) { separator = &r; separator->children = 0; continue; }
				if (separator && r.depth > 0) { ++separator->children; }
			}
			return rows;
		}

		// THE SCREEN'S OUTER BAND IS NEVER DRAWN ON (W3 1.0.3 test, 2026-10-05: a row of frame corners stayed along the
		// bottom of the screen, menu open or closed, after resize drags that reached the edge). The Witcher 3 draws its
		// picture inset from the swap chain image's edges and never repaints the thin band round it (black on the main
		// menu: about 17 px left and right and 11 px top and bottom of 2133x1200, measured off the marks-closed capture).
		// Whatever the overlay draws there stays in all three back buffers after the window moves or closes. So the
		// window keeps this margin from every edge - 2.5% of the screen height (30 px at 1200) - and so do popups,
		// combo lists and tooltips (style.DisplaySafeAreaPadding, set in Frame).
		float EdgeMargin(const ImVec2& a_display)
		{
			return std::max(8.0f, std::round(a_display.y * 0.025f));
		}

		// NOTHING OF OURS IS EVER RASTERISED IN THE BAND (W3 1.0.4, the tester's run on 1.0.3 rebuild 3: after a corner resize
		// dragged past the screen, a resize-grip triangle, a 1-px piece of the window's left border and the cursor arrow
		// stayed in the bottom band with the menu closed). Keeping the window's geometry inside the margin is not enough on
		// its own: ImGui applies a corner drag inside Begin and draws the frame, grip and border from that size at once, and
		// the software cursor is drawn wherever the mouse is. So after ImGui::Render every draw command's clip rect is cut to
		// the rect inside the band - our window, popups, the on-screen keyboard, the cursor, and other mods' windows and HUD
		// elements drawn through AMF alike. The backend skips a command whose clip comes out empty. A callback command
		// (ImDrawCallback_ResetRenderState) carries no pixels and is left alone.
		void ClipDrawDataToSafeRect()
		{
			ImDrawData* dd = ImGui::GetDrawData();
			if (!dd || !dd->Valid || dd->DisplaySize.x < 1.0f || dd->DisplaySize.y < 1.0f) { return; }
			const float edge = EdgeMargin(dd->DisplaySize);
			const ImVec4 safe(dd->DisplayPos.x + edge, dd->DisplayPos.y + edge, dd->DisplayPos.x + dd->DisplaySize.x - edge,
							  dd->DisplayPos.y + dd->DisplaySize.y - edge);
			for (int l = 0; l < dd->CmdListsCount; ++l)
			{
				ImDrawList* list = dd->CmdLists[l];
				if (!list) { continue; }
				for (ImDrawCmd& cmd : list->CmdBuffer)
				{
					if (cmd.UserCallback) { continue; }
					cmd.ClipRect.x = std::max(cmd.ClipRect.x, safe.x);
					cmd.ClipRect.y = std::max(cmd.ClipRect.y, safe.y);
					cmd.ClipRect.z = std::min(cmd.ClipRect.z, safe.z);
					cmd.ClipRect.w = std::min(cmd.ClipRect.w, safe.w);
				}
			}
			static ImVec4 s_logged{ -1.0f, -1.0f, -1.0f, -1.0f };   // render thread; logged when the rect changes (start, resolution)
			if (s_logged.x != safe.x || s_logged.y != safe.y || s_logged.z != safe.z || s_logged.w != safe.w)
			{
				s_logged = safe;
				logger::debug("edge band: every draw is clipped to ({:.0f}, {:.0f}) - ({:.0f}, {:.0f}) of {:.0f}x{:.0f}, {:.0f} px in from each edge",
							  safe.x, safe.y, safe.z, safe.w, dd->DisplaySize.x, dd->DisplaySize.y, edge);
			}
		}

		// ---- Framework Settings, one function per tab (Skyrim 2.1.0's tabs by area) ----------------------------------
		// GENERAL: how the menu exits, the on-screen keyboard, what drives it, and the window's place and size.
		void DrawSettingsGeneralTab()
		{
			auto& values = settings::Get();

			// THE SYSTEM ROW IS A SETTING, NOT AN INSTALL-TIME CHOICE (author, 2026-09-04: "we can
			// just have one version and not a fomod"). It shipped briefly as a FOMOD fork, which
			// made a reversible preference into something you had to reinstall to change - and
			// forked the documentation, the INI and the support answers along with it. One build,
			// one INI, and the choice lives here where it can be changed and changed back.
			// OBLIVION REMASTERED: three of Skyrim's rows drive features this port does not have yet - the startup
			// curtain (keyed to Skyrim's MainMenu), pausing (PLAN.md M3) and the journal's System row (M3 puts AMF on
			// the pause menu instead). A toggle that does nothing is worse than no toggle, so they are not drawn; the
			// INI keys still read and save, so nothing is lost when each is wired.
			const bool kPauseRow = red3::PauseAvailable();   // shown once the game's own pause was found (Red3)
			constexpr bool kSystemRow = false;   // the System row: hidden on Witcher 3 (SystemRow.cpp is stubs - no row, no journal)
			// The Witcher 3 build has no startup curtain at all (the owner's decision), so it has no row and no INI keys.
			if (kPauseRow) {
			if (widgets::Toggle(TR("AMF_PauseGame", "Pause the game while this menu is open"), &values.pauseGameWhileOpen))
			{
				logger::info("settings page: pause the game while open -> {}", values.pauseGameWhileOpen);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_PauseGameHelp", "On: time stops while this menu is open, the way it does in the game's own "
							   "menus - nothing moves, fights or ticks down behind it. Off: the game keeps running while you change settings."));
			ImGui::Spacing();

			}
			if (widgets::Toggle(TR("AMF_SkipIntro", "Skip the intro videos when the game starts"), &values.skipIntro))
			{
				logger::info("settings page: skip intro -> {}", values.skipIntro);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_SkipIntroHelp", "On: the game goes straight to its main menu, without the disclaimer, "
							   "legal and logo videos or the story recap it plays at start. Takes effect from the next start. Off: the videos play as usual."));
			ImGui::Spacing();
			if (widgets::Toggle(TR("AMF_SkipLoadingRecap", "Skip the story recap on loading screens"), &values.skipLoadingRecap))
			{
				logger::info("settings page: skip loading recap -> {}", values.skipLoadingRecap);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_SkipLoadingRecapHelp", "On: loading a save shows the plain loading screen, without the narrated "
							   "recap of the story so far. Takes effect from the next load. Off: the recap plays again."));
			ImGui::Spacing();

			if (widgets::Toggle(TR("AMF_FastExit", "Fast exit - end the process the moment the game exits"), &values.fastExit))
			{
				logger::info("settings page: fast exit -> {}", values.fastExit);
				settings::Save();
			}
			if (kSystemRow) {
			if (widgets::Toggle(TR("AMF_SystemRow", "Mod settings in the game's System menu"), &values.systemMenuRow))
			{
				logger::info("settings page: system menu row -> {}", values.systemMenuRow);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_SystemRowHelpOR", "On: an Apocrypha Menu Framework row is added to the game's System page, "
							   "under Save, Load and Quit, and opens this menu. The row is added to the page as it "
							   "opens rather than by replacing any game file, so it works with whatever menu artwork "
							   "you have installed. Off: the game's page is left completely untouched and this menu "
							   "is reached by its key alone. Takes effect at the next launch."));
			ImGui::TextDisabled("%s", TR("AMF_TakesEffectJournal", "Takes effect the next time the journal is opened."));
			ImGui::Spacing();
			}

			// THE ON-SCREEN KEYBOARD (1.8.9, the owner, 2026-09-18): a framework feature, so every mod's
			// search box gets it; a toggle here, drawn at the bottom of the screen, never over the page.
			if (widgets::Toggle(TR("AMF_OnScreenKeyboard", "On-screen keyboard for controllers"), &values.onScreenKeyboard))
			{
				logger::info("settings page: on-screen keyboard -> {}", values.onScreenKeyboard);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_OnScreenKeyboardHelp", "On: highlight any text box on a mod's page with the D-pad and press A, "
							   "and a key grid appears across the bottom of the screen. The D-pad walks the keys, A types one, B puts "
							   "the highlight back on the box, X is shift and Y is backspace. It works in every mod's page. Off: text "
							   "boxes take a real keyboard only."));
			ImGui::Spacing();

			// INPUT MODE IS DETECTED, AND IS NOT A SETTING (author, 2026-09-04: "I want the auto
			// detection feature built-in with no toggle and there doesn't need to be a controller
			// toggle anymore"). There were two switches here - one holding the mode, one deciding
			// whether the detector was allowed to write it - which is two controls describing one
			// fact the game already knows, and they could be left disagreeing with reality. The
			// detector's reading is now simply used, and shown, so it can still be judged while
			// playing rather than taken on trust.
			ImGui::TextUnformatted(TR("AMF_Navigation", "Navigation"));
			ImGui::TextWrapped("%s", TR("AMF_NavigationHelp", "Follows whatever you last used: press a key or move the mouse for "
							   "keyboard navigation (arrow keys, Enter, Escape), touch the pad for "
							   "controller navigation (D-pad moves, A activates, B cancels)."));
			{
				const input::Device device = input::LastDevice();
				const float since = input::SecondsSinceLastDevice();
				const char* name = device == input::Device::kGamepad ? TR("AMF_DevController", "controller")
								 : device == input::Device::kKeyboardMouse ? TR("AMF_DevKeyboard", "keyboard and mouse")
								 : TR("AMF_DevNone", "nothing yet");
				if (since < 0.0f) { ImGui::TextDisabled(TR("AMF_Detected", "Detected: %s"), name); }
				else { ImGui::TextDisabled(TR("AMF_DetectedAgo", "Detected: %s (%.1fs ago)"), name, since); }
			}
			ImGui::Spacing();
			ImGui::Spacing();

			// WINDOW PROFILES. The window opens where it was left, at the size it was left (Skyrim 2.1.1: moved by its top
			// row, resized by any edge or corner - the two switches are on Appearance); this is the way back to the starting
			// geometry, the middle of the screen at the default size.
			{
				auto& v = settings::Get();
				const bool anySet = v.nestedWindow.IsSet() || v.hotkeyWindow.IsSet();
				ImGui::TextUnformatted(TR("AMF_WindowPosSize", "Window position and size"));
				ImGui::TextWrapped("%s", TR("AMF_WindowProfilesHelp", "The window opens where you left it, at the size you left it: drag its top row to move it, and an edge or a corner to resize it (Settings -> Appearance can switch either off). The button puts it back at its standard place, beside the game's menu column."));
				ImGui::BeginDisabled(!anySet);
				if (ImGui::Button(TR("AMF_ResetBoth", "Reset to default")))
				{
					v.nestedWindow.Clear();
					v.hotkeyWindow.Clear();
					settings::Save();
					g_applyGeometry.store(true, std::memory_order_release);
					logger::info("settings page: window profiles reset to their defaults");
				}
				ImGui::EndDisabled();
				if (!anySet)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", TR("AMF_BothDefault", "(at its default)"));
				}
			}
			ImGui::Spacing();

			// The menu key is set in ONE place, Controls > Open and close the menu (Skyrim 2.1.1; the owner, 2026-10-05:
			// "there's duplicate entries for the menus toggle key ... There should just be one"). The Rebind that sat here
			// changed only uToggleKey, while the menu opened on the Controls binding - so it moved the label and not the
			// key. It went, with the "Window position: Centre" line beside it, a stub that offered nothing to set.

			// Persistence-channel test harness (decisions doc S10) - lets the per-save round
			// trip be exercised end to end (write, save, quit, reload, confirm) with no Papyrus
			// compiler involved. Debug-only surface; not a real setting.
			// Off for players (the Witcher 3 wording pass, 2026-10-05: an English-only debug panel on the Settings page).
			constexpr bool kPersistenceTest = false;
			if (kPersistenceTest)
			{
			ImGui::TextUnformatted("Persistence test (S10)");
			static char testBuffer[128] = "";
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
			ImGui::InputText("##persistValue", testBuffer, sizeof(testBuffer));
			keyboard::NoteTextField(ImGui::GetItemID());
			ImGui::SameLine();
			if (ImGui::Button("Set"))
			{
				persistence::SetValue("test-value", testBuffer);
			}
			ImGui::Text("Currently stored: \"%s\"", persistence::GetValue("test-value", "<unset>").c_str());
			ImGui::TextWrapped("Set a value, save the game, quit, reload the same save - the "
							   "value should still be here. A DIFFERENT save should show <unset>.");
			}
		}

		// APPEARANCE: theme, font, language, text size, the window's three switches, custom art.
		void DrawSettingsAppearanceTab()
		{
			auto& values = settings::Get();

			// APPEARANCE ALWAYS APPLIES, in both installs (corrected 2026-09-04). These settings
			// used to be hidden whenever the System row was on, under the belief that a nested
			// surface would wear the game's own menu artwork and so have nothing to theme. It does
			// not: nesting changes GEOMETRY only - the window is fitted to the journal panel around
			// it - and every pixel inside that rectangle is still drawn by this framework, in this
			// theme. Hiding the controls left the one install that most needs them unable to reach
			// them, and told the player something untrue about their own menu while doing it.

			// Theme picker (design decision, 2026-08-27) - supersedes the original "no theme UI by design"
			// stance; the registry is additive (theme::Theme.h), never overwriting an entry.
			const std::vector<theme::Palette> themes = theme::ListThemes();
			const theme::Palette& active = theme::GetActiveTheme();

			int currentIndex = 0;
			std::vector<const char*> names;
			names.reserve(themes.size());
			for (std::size_t i = 0; i < themes.size(); ++i)
			{
				names.push_back(themes[i].name.c_str());
				if (themes[i].id == active.id)
				{
					currentIndex = static_cast<int>(i);
				}
			}

			// The three dropdowns open without an empty band above and below the list (Skyrim 2.1.1, theme::ComboTight).
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (theme::ComboTight(TR("AMF_Theme", "Theme"), &currentIndex, names.data(), static_cast<int>(names.size())))
			{
				theme::SetActiveTheme(themes[currentIndex].id);
				theme::Apply();
				skin::Reload();   // the new theme's own frame and background, if it has any
				values.themeId = themes[currentIndex].id;
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_ThemeHelp", "\"Skellige\", the default, is grey lines on the black of the game's menu with a blood-red highlight. "
							   "\"Untarnished\" is the framework's original identity: the same layout with clean lines and no frame art. "
							   "\"Oblivion\" (an embroidered map's edge in gold and brown on parchment) and \"Skyrim\" (the Nordic "
							   "knotwork frame with silver and gold lines) are the looks of the framework's other builds, kept for "
							   "anyone who prefers them."));

			ImGui::Spacing();

			// FONT picker - separate from the theme on purpose (the author): the theme decides colours,
			// this decides the letterforms, and the two combine freely.
			{
				int current = 0;
				std::vector<const char*> labels;
				labels.reserve(g_fontChoices.size());
				for (std::size_t i = 0; i < g_fontChoices.size(); ++i)
				{
					labels.push_back(g_fontChoices[i].label.c_str());
					if (g_fontChoices[i].path == values.fontPath) { current = static_cast<int>(i); }
				}
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
				if (!labels.empty() && theme::ComboTight(TR("AMF_Font", "Font"), &current, labels.data(), static_cast<int>(labels.size())))
				{
					values.fontPath = g_fontChoices[current].path;
					settings::Save();
					g_fontRebuildPending = true;  // re-rasterise in the new face
					logger::info("settings page: font -> \"{}\" ({})",
								 g_fontChoices[current].label,
								 values.fontPath.empty() ? "auto" : values.fontPath.c_str());
				}
				ImGui::TextWrapped("%s", TR("AMF_FontHelp", "Drop a .ttf into bin/x64_dx12/AMF/fonts "
								   "to add it to this list."));
			}
			ImGui::Spacing();
			ImGui::Spacing();

			// LANGUAGE (1.6.4): which translation file the framework's own text comes from. "Game
			// language" follows the game's sLanguage; a named entry forces that file (the INI's
			// sLanguage). Changing it reloads the strings and rebuilds the atlas for the new glyphs.
			{
				static std::vector<std::string> s_langs;
				static double s_scannedAt = -1.0;
				const double now = ImGui::GetTime();
				if (s_scannedAt < 0.0 || now - s_scannedAt > 5.0) { s_langs = strings::Available(); s_scannedAt = now; }
				std::vector<std::string> labels;
				labels.push_back(std::string(TR("AMF_LanguageAuto", "Game language")) + " (" + strings::Language() + ")");
				for (const auto& l : s_langs) { std::string t = l; if (!t.empty()) { t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0]))); } labels.push_back(t); }
				std::vector<const char*> cLabels;
				for (const auto& l : labels) { cLabels.push_back(l.c_str()); }
				int current = 0;
				const std::string& forced = settings::Get().language;
				for (std::size_t i = 0; i < s_langs.size(); ++i) { if (!forced.empty() && s_langs[i] == forced) { current = static_cast<int>(i) + 1; } }
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
				if (theme::ComboTight(TR("AMF_Language", "Language"), &current, cLabels.data(), static_cast<int>(cLabels.size())))
				{
					strings::SetLanguage(current == 0 ? "" : s_langs[static_cast<std::size_t>(current - 1)]);
				}
				ImGui::TextWrapped("%s", TR("AMF_LanguageHelp", "The framework's own text. Game language follows the text language set in The Witcher 3's own options "
								   "(or your Windows language, if there is no translation here for the game's); pick one to force it. "
								   "Each mod's own page is translated by that mod. Translation files: bin/x64_dx12/AMF/Translations/ApocryphaMenuFramework_<language>.txt."));
			}
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (precise::SliderFloat(TR("AMF_TextSize", "Text size"), &values.textScale, 1.0f, 2.0f, "%.2f"))
			{
				// applied live via FontGlobalScale each frame
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				logger::info("settings page: text scale -> {:.2f}", values.textScale);
				settings::Save();
				g_fontRebuildPending = true;  // re-rasterise at the new size rather than stretch
			}
			ImGui::TextWrapped("%s", TR("AMF_TextSizeHelp", "Extra text scaling on top of the automatic resolution scale."));
			ImGui::Spacing();
			ImGui::Spacing();

			// THE WINDOW, THREE SWITCHES (Skyrim 2.1.1 - Barzing on Nexus, 2026-10-05: resize "also in height", "move the
			// window", "the semi transparence of the window"; the owner: "seperate toggles" ... "in apperance teb"). Each ON by
			// default (the owner: "have it default to on, along with the other settings we just added"); See-through starts at
			// 100% opacity, so it looks solid until the slider is lowered (a precise slider - rule 68: one percent per nudge).
			if (widgets::Toggle(TR("AMF_MovableWindow", "Move the window"), &values.movableWindow))
			{
				logger::info("settings page: move the window -> {}", values.movableWindow);
				settings::Save();
				if (!values.movableWindow) { g_applyGeometry.store(true, std::memory_order_release); }   // back to its standard place
			}
			ImGui::TextWrapped("%s", TR("AMF_MovableWindowHelp", "On: drag the top row - the name and version - to move the "
				"menu, and it opens where you left it. Off: it sits at its standard place, beside the game's menu column."));
			if (widgets::Toggle(TR("AMF_SnapToGameMenu", "Sit beside the game's menu column"), &values.snapToGameMenu))
			{
				logger::info("settings page: sit beside the game's menu column -> {}", values.snapToGameMenu);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_SnapToGameMenuHelp", "On: while the game's own menu is open - the title screen or "
				"the pause menu - the window opens just right of its black column, at the standard size. In the world it opens "
				"where you left it. Off: it always opens where you left it."));
			if (widgets::Toggle(TR("AMF_FreeResize", "Resize the window"), &values.freeResize))
			{
				logger::info("settings page: resize the window -> {}", values.freeResize);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_FreeResizeHelp", "On: drag any edge or corner to resize the menu - height "
				"and width alike - and the size is kept. Off: the size is fixed."));
			if (widgets::Toggle(TR("AMF_SeeThrough", "See-through window"), &values.seeThrough))
			{
				logger::info("settings page: see-through window -> {}", values.seeThrough);
				settings::Save();
				theme::Apply();
			}
			ImGui::TextWrapped("%s", TR("AMF_SeeThroughHelp", "On: the opacity below fades the menu's background so the game "
				"shows through. Off: the background is solid."));
			ImGui::BeginDisabled(!values.seeThrough);
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (precise::SliderInt(TR("AMF_WindowOpacity", "Window opacity"), &values.windowOpacity, 5, 100, "%d%%"))
			{
				theme::Apply();   // live while dragging
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				logger::info("settings page: window opacity -> {}%", values.windowOpacity);
				settings::Save();
			}
			ImGui::TextWrapped("%s", TR("AMF_WindowOpacityHelp", "How solid the menu is: 100% is solid, lower lets the game show through. The black background fades the most, boxes and borders less, text least; right-click menus stay solid."));
			ImGui::EndDisabled();
			ImGui::Spacing();
			ImGui::Spacing();

			// CUSTOM MENU ART IS OFF UNLESS ASKED FOR (the owner, 2026-09-10: "i dont want the custom
			// menu art to be visible ... there needs to be a way to not have it on at all times").
			// The switch is here as well as in the INI so a player can turn it off without editing a
			// file, and turning it off reloads at once rather than at the next launch.
			if (widgets::Toggle(TR("AMF_SkinEnabled", "Custom menu art from a UI author"), &values.skinEnabled))
			{
				logger::info("settings page: custom menu art -> {}", values.skinEnabled);
				settings::Save();
				skin::Reload();
			}
			ImGui::TextWrapped("%s", TR("AMF_SkinEnabledHelp", "Off: the menu keeps its built-in look, whatever is set under [Skin] in the "
							   "INI. On: the frame, background and toggle switch are replaced by the PNGs a UI "
							   "author has pointed the framework at. Leave this off unless you have installed "
							   "artwork made for it."));
			ImGui::Spacing();
		}

		// MENU LIST: the side list's order, names and separators, and the layout presets.
		void DrawSettingsMenuListTab()
		{
			DrawMenuListSection();
		}

		void DrawFrameworkSettingsPane()
		{
			ImGui::TextUnformatted(TR("AMF_FrameworkSettings", "Framework Settings"));
			ImGui::Separator();
			ImGui::Spacing();

			// TABS BY AREA (Skyrim 2.1.0 - the owner, 2026-10-05: "divide the AMF settings page into several tabs that are
			// divided by their area that they affect"). Same tab mechanics as Controls and Help, so the bumpers and Page Up /
			// Page Down walk them the same way (g_tabCount / g_tabIndex / g_tabRequest). One function per tab: a new tab
			// is one more line here and one more function above. Skyrim's "MCM menus" tab has no Witcher 3 counterpart.
			if (!ImGui::BeginTabBar("##settingstabs", ImGuiTabBarFlags_FittingPolicyScroll)) { return; }
			int index = 0;
			const auto tab = [&](const char* a_label, const char* a_name) {
				const ImGuiTabItemFlags flags = (index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				const bool open = BeginPageTab(a_label, flags);
				if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
				if (open) { g_tabIndex = index; g_tabName = a_name; }
				++index;
				return open;
			};

			if (tab(TR("AMF_TabGeneral", "General"), "General"))       { DrawSettingsGeneralTab(); ImGui::EndTabItem(); }
			if (tab(TR("AMF_TabAppearance", "Appearance"), "Appearance")) { DrawSettingsAppearanceTab(); ImGui::EndTabItem(); }
			// The Witcher 3's counterpart of Skyrim's "MCM menus" tab: which mods' Options > Mods menus are listed, and the
			// sort into categories (ModMenusSort.cpp).
			if (tab(TR("AMF_TabModMenus", "Mod menus"), "Mod menus"))   { modmenus::DrawSettingsTab(); ImGui::EndTabItem(); }
			if (tab(TR("AMF_MenuList", "Menu list"), "Menu list"))       { DrawSettingsMenuListTab(); ImGui::EndTabItem(); }

			g_tabCount = index;     // the bumpers walk these tabs, as on Controls and Help
			g_tabRequest = -1;      // a requested tab is taken once, not every frame
			ImGui::EndTabBar();
		}

		// ---- Separators in the side list (the owner, 2026-10-02 - MO2's separators) --------------------------------------
		// Creating one names it at once: the rename modal opens on it with the default name filled in.
		void BeginNewSeparator(const std::vector<registry::Entry>& a_entries, const std::string& a_beforeName)
		{
			const char* name = TR("AMF_SeparatorDefaultName", "New separator");
			g_renameTarget = personalization::AddSeparator(a_entries, a_beforeName, name);
			std::snprintf(g_renameBuffer, sizeof(g_renameBuffer), "%s", name);
			g_renameOpenPending = true;
			settings::Save();
		}

		// A separator row: a fold arrow, the name, how many menus it holds when folded, a white box when pinned. A (or a
		// click) folds and unfolds it; Y (or a right-click) opens its menu - the same binding a mod row uses.
		// GRAB AND MOVE (the owner, 2026-10-02). The mod picked up with the grab action, empty when none is.
		std::string g_grabbedMod;

		// The move actions bound to a stick direction are polled rather than raised: one step as the stick is pushed,
		// then a step every 0.12 s while it is held past a 0.35 s pause. A move bound to a button or key is raised
		// like any command instead and steps once per press. Called every frame so a held stick is not mistaken for
		// a fresh push the moment a mod is picked up.
		int GrabStickStep()
		{
			static int s_lastDir = 0;
			static double s_nextAt = 0.0;
			const auto held = [](bindings::Action a_action) {
				const bindings::Binding b = bindings::Get(a_action);
				if (b.padKind != bindings::PadKind::kStickDir) { return false; }
				float x = 0.0f, y = 0.0f;
				bool clicked = false, live = false;
				input::GetStick((b.padCode >> 4) & 0xF, x, y, clicked, live);
				constexpr float kPush = 0.5f;   // y > 0 is up, as for the left stick's navigation
				switch (b.padCode & 0xF)
				{
				case 0: return y > kPush;
				case 1: return y < -kPush;
				case 2: return x < -kPush;
				case 3: return x > kPush;
				default: return false;
				}
			};
			const int dir = held(bindings::Action::kGrabUp) ? -1 : (held(bindings::Action::kGrabDown) ? 1 : 0);
			const double now = ImGui::GetTime();
			int step = 0;
			if (dir != 0 && dir != s_lastDir) { step = dir; s_nextAt = now + 0.35; }
			else if (dir != 0 && now >= s_nextAt) { step = dir; s_nextAt = now + 0.12; }
			s_lastDir = dir;
			return step;
		}

		void DrawSeparatorRow(const std::vector<registry::Entry>& a_entries, const personalization::DisplayEntry& a_row,
							  bool a_contextMenu, bool a_favouriteKey)
		{
			ImGui::PushID(a_row.modName.c_str());
			const bool favourite = personalization::IsFavourite(a_row.modName);
			const float boxSide = ImGui::GetFontSize() * 0.55f;
			const float gutter = boxSide + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
			const ImVec2 rowTopLeft = ImGui::GetCursorScreenPos();

			// the name (and, folded, how many menus it holds) is the row's one label - one nav stop; the fold arrow is drawn
			// in front of it, ImGui's own tree arrow (down when open, right when folded)
			char label[160];
			if (a_row.collapsed) { std::snprintf(label, sizeof(label), "%s  (%d)", a_row.displayName.c_str(), a_row.children); }
			else { std::snprintf(label, sizeof(label), "%s", a_row.displayName.c_str()); }
			const float arrowRoom = ImGui::GetFontSize() * 1.1f;
			const ImVec2 arrowAt(rowTopLeft.x + gutter, rowTopLeft.y);
			ImGui::Indent(gutter + arrowRoom);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			const bool picked = ImGui::Selectable(label, false);
			ImGui::PopStyleColor();
			ImGui::Unindent(gutter + arrowRoom);
			ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowAt, ImGui::GetColorU32(ImGuiCol_Text),
							   a_row.collapsed ? ImGuiDir_Right : ImGuiDir_Down, 0.8f);
			// a hairline under the separator, from the end of its name to the pane's edge
			{
				const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
				const float y = (mn.y + mx.y) * 0.5f;
				const float x0 = mn.x + ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;
				if (x0 < mx.x) { ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(mx.x, y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f); }
			}
			if (favourite)
			{
				const float top = rowTopLeft.y + (ImGui::GetTextLineHeight() - boxSide) * 0.5f;
				const float left = rowTopLeft.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(left + boxSide, top + boxSide), IM_COL32(255, 255, 255, 255));
			}
			// Y opens the row's menu and nothing else (W3 1.0.5 run: Y also folded the separator - the pad's Y reaches ImGui
			// as its "input" press, which activates the focused row the same frame the context-menu action fires).
			const bool yOnThisRow = a_contextMenu && ImGui::IsItemFocused();
			if (picked && !yOnThisRow)
			{
				personalization::ToggleCollapsed(a_row.modName);
				settings::Save();
			}
			if (ImGui::IsItemFocused())
			{
				if (a_contextMenu)
				{
					ImGui::OpenPopup("##sepctx");
					// opened from the controller (Y): beside the highlighted row, not wherever the mouse was left
					ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x + ImGui::GetFontSize(), ImGui::GetItemRectMax().y),
						ImGuiCond_Appearing);
				}
				if (a_favouriteKey)
				{
					personalization::ToggleFavourite(a_row.modName);
					settings::Save();
				}
			}
			const float ctxPad = ImGui::GetFontSize() * 0.35f;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
			const bool ctxOpen = ImGui::BeginPopupContextItem("##sepctx");
			ImGui::PopStyleVar();
			if (ctxOpen)
			{
				if (ImGui::MenuItem(a_row.collapsed ? TR("AMF_SeparatorExpand", "Expand") : TR("AMF_SeparatorCollapse", "Collapse")))
				{
					personalization::ToggleCollapsed(a_row.modName);
					settings::Save();
				}
				if (ImGui::MenuItem(favourite ? TR("AMF_Unfavourite", "Remove from favourites") : TR("AMF_Favourite", "Add to favourites")))
				{
					personalization::ToggleFavourite(a_row.modName);
					settings::Save();
				}
				if (ImGui::MenuItem(TR("AMF_Rename", "Rename...")))
				{
					g_renameTarget = a_row.modName;
					std::snprintf(g_renameBuffer, sizeof(g_renameBuffer), "%s", a_row.displayName.c_str());
					g_renameOpenPending = true;
				}
				ImGui::Separator();
				if (ImGui::MenuItem(TR("AMF_NewSeparatorAbove", "New separator above")))
				{
					BeginNewSeparator(a_entries, a_row.modName);
				}
				if (ImGui::MenuItem(TR("AMF_MoveToTop", "Move to the top")))
				{
					personalization::MoveTo(a_entries, a_row.modName, 1);
					settings::Save();
				}
				ImGui::Separator();
				if (ImGui::MenuItem(TR("AMF_SeparatorDelete", "Delete separator")))
				{
					personalization::RemoveSeparator(a_row.modName);   // its menus join the separator above
					settings::Save();
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		// ---- Menu list: rename and reorder (author verdict 2026-09-01) ----------------------
		// Presentation only - the registry and the mods themselves are untouched. Numbering is
		// insert-and-shift: type a position and every other entry re-flows around it, so nobody
		// has to number the whole list by hand.
		void DrawMenuListSection()
		{
			const std::vector<registry::Entry> entries = registry::Snapshot();
			ImGui::SeparatorText(TR("AMF_MenuList", "Menu list"));
			ImGui::TextWrapped("%s", TR("AMF_MenuListHelp", "Rename any mod's entry, and set the order of the list. Type a position "
							   "number to move an entry there - every other entry re-flows around it."));

			ImGui::Text(TR("AMF_Order", "Order: %s"), personalization::IsCustomOrder() ? TR("AMF_OrderCustom", "custom") : TR("AMF_OrderAlphabetical", "alphabetical"));
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_ResetAlphabetical", "Reset to alphabetical")))
			{
				personalization::ResetToAlphabetical();
				settings::Save();
			}
			ImGui::SameLine();
			// Asked for directly (the owner, 2026-09-10). Every edit above already writes the file
			// on commit, so this is the reassurance that the list on screen is the list on disk -
			// and the way out if a field was left mid-edit.
			if (ImGui::Button(TR("AMF_SaveMenuList", "Save menu list")))
			{
				settings::Save();
				g_menuListSavedAt = ImGui::GetTime();
			}
			if (g_menuListSavedAt > 0.0 && ImGui::GetTime() - g_menuListSavedAt < 3.0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", TR("AMF_MenuListSaved", "saved"));
			}

			static bool s_aliasBuffersStale = false;   // set by a preset load; the table below drops its cached names

			// Layout presets (Skyrim 2.0.3, xLenax via the owner, 2026-10-02): the list's order, separators, favourites and
			// renames saved under a name, to load back later or keep a second arrangement. Every preset can be deleted
			// (the owner's standing rule for presets). Files in bin\x64_dx12\AMF\Presets, which the download never holds.
			ImGui::Spacing();
			ImGui::TextUnformatted(TR("AMF_LayoutPresets", "Layout presets"));
			ImGui::TextWrapped("%s", TR("AMF_LayoutPresetsHelp", "Save this list - its order, separators, favourites and names - "
							   "under a name, and load it back any time. Your settings are kept in a file the download never "
							   "contains, so an update does not reset them."));
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
			ImGui::InputTextWithHint("##presetName", TR("AMF_PresetNameHint", "Preset name"), g_presetName, sizeof(g_presetName));
			keyboard::NoteTextField(ImGui::GetItemID());
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_SavePreset", "Save as preset")))
			{
				const std::string name = g_presetName;
				g_presetStatus = settings::SaveLayoutPreset(name) ? TR("AMF_PresetSaved", "Saved.") : TR("AMF_PresetNotSaved", "Not saved - type a name first.");
				g_presetStatusAt = ImGui::GetTime();
			}
			// Listed from the folder at most twice a second rather than every frame (the HUD-mods rule: keep per-frame
			// work down), and at once after any action here.
			static std::vector<std::string> s_presets;
			static double s_presetsListedAt = -1.0;
			if (s_presetsListedAt < 0.0 || ImGui::GetTime() - s_presetsListedAt > 0.5 || g_presetStatusAt >= s_presetsListedAt)
			{
				s_presets = settings::ListLayoutPresets();
				s_presetsListedAt = ImGui::GetTime();
			}
			if (s_presets.empty())
			{
				ImGui::TextDisabled("%s", TR("AMF_NoPresets", "No presets saved yet."));
			}
			const std::vector<std::string> presets = s_presets;   // a copy: a Delete below changes the list
			for (const std::string& preset : presets)
			{
				ImGui::PushID(preset.c_str());
				ImGui::BulletText("%s", preset.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton(TR("AMF_LoadPreset", "Load")))
				{
					const bool loaded = settings::LoadLayoutPreset(preset);
					g_presetStatus = loaded ? TR("AMF_PresetLoaded", "Loaded.") : TR("AMF_PresetNotLoaded", "Could not be read - see the log.");
					g_presetStatusAt = ImGui::GetTime();
					s_aliasBuffersStale = s_aliasBuffersStale || loaded;   // the table's name fields show the preset's names
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(TR("AMF_DeletePreset", "Delete")))
				{
					g_presetStatus = settings::DeleteLayoutPreset(preset) ? TR("AMF_PresetDeleted", "Deleted.") : TR("AMF_PresetNotDeleted", "Could not be deleted - see the log.");
					g_presetStatusAt = ImGui::GetTime();
				}
				ImGui::PopID();
			}
			if (!g_presetStatus.empty() && ImGui::GetTime() - g_presetStatusAt < 4.0)
			{
				ImGui::TextDisabled("%s", g_presetStatus.c_str());
			}
			ImGui::Spacing();

			if (entries.empty())
			{
				ImGui::TextDisabled("%s", TR("AMF_NoMods", "No mods have registered a page yet."));
				return;
			}

			const std::vector<personalization::DisplayEntry> rows = personalization::Order(entries);
			static std::unordered_map<std::string, std::array<char, 64>> aliasBuffers;
			if (s_aliasBuffersStale)
			{
				aliasBuffers.clear();
				s_aliasBuffersStale = false;
			}

			// Rows with something to show (Skyrim 2.1.0): an entry whose every page is hidden (a mod that hid them all with
			// AMF_SetPageVisible) has no row here, as it has none in the side list. It keeps its place in the saved order; the
			// number shown is its place among the rows shown, and a number typed is mapped back to the whole order before
			// the move.
			auto allPagesHidden = [&](const personalization::DisplayEntry& r) {
				if (r.separator || r.registryIndex < 0 || r.registryIndex >= static_cast<int>(entries.size())) { return false; }
				const auto& pages = entries[r.registryIndex].pages;
				return !pages.empty() && std::all_of(pages.begin(), pages.end(), [](const registry::Page& p) { return p.hidden; });
			};
			std::vector<int> shownRows;  // indices into rows
			for (int i = 0; i < static_cast<int>(rows.size()); ++i)
			{
				if (!allPagesHidden(rows[i])) { shownRows.push_back(i); }
			}

			// A reorder requested this frame, applied AFTER the table closes.
			//
			// It used to call personalization::MoveTo() inline, in the middle of the loop that is
			// still walking `rows`. MoveTo rewrites the global order immediately, so every row drawn
			// after the commit was laid out against a sequence that no longer matched - the widget
			// ids are pushed from the mod NAME while the committed value is compared against the row
			// INDEX (`position != i + 1`), and after a move those two refer to different entries.
			// A player could move two or three and then found the fields stopped responding
			// (xLenax, 2026-09-11). Deferring keeps the frame's layout consistent with the sequence
			// it was built from, and applies exactly one move per frame.
			std::string pendingMoveMod;
			int pendingMovePosition = 0;

			if (ImGui::BeginTable("##menulist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.2f);
				ImGui::TableSetupColumn(TR("AMF_ColMod", "Mod"));
				ImGui::TableSetupColumn(TR("AMF_ColShowsAs", "Shows as"));
				ImGui::TableHeadersRow();

				for (int shownIndex = 0; shownIndex < static_cast<int>(shownRows.size()); ++shownIndex)
				{
					const int i = shownRows[shownIndex];
					const personalization::DisplayEntry& row = rows[i];
					ImGui::TableNextRow();
					ImGui::PushID(row.modName.c_str());

					ImGui::TableSetColumnIndex(0);
					int position = shownIndex + 1;
					ImGui::SetNextItemWidth(-FLT_MIN);
					// Commit on Enter OR on losing focus. EnterReturnsTrue alone meant that typing a
					// position and then clicking away threw the number away without a word, which
					// is why reordering appeared not to save at all (the owner, 2026-09-10). The
					// alias field beside this one already committed both ways; now they match.
					const bool posEntered = ImGui::InputInt("##pos", &position, 0, 0,
															ImGuiInputTextFlags_EnterReturnsTrue);
					keyboard::NoteTextField(ImGui::GetItemID());
					if ((posEntered || ImGui::IsItemDeactivatedAfterEdit()) && position != shownIndex + 1)
					{
						// RECORDED, not applied - see the note above the declaration. Applying here
						// rewrote the order while this same loop was still walking `rows`.
						// The number typed is a place among the rows shown: the move goes to that row's place in the whole order.
						const int target = std::clamp(position, 1, static_cast<int>(shownRows.size()));
						pendingMoveMod = row.modName;
						pendingMovePosition = shownRows[target - 1] + 1;
						logger::debug("menu list: \"{}\" typed to row {} of {} shown -> place {} of {} in the whole order",
									  row.modName, position, shownRows.size(), pendingMovePosition, rows.size());
					}

					ImGui::TableSetColumnIndex(1);
					if (row.separator) { ImGui::TextDisabled("%s", TR("AMF_SeparatorRow", "(separator)")); }
					else
					{
						if (row.depth > 0) { ImGui::Indent(ImGui::GetFontSize() * 0.9f); }
						ImGui::TextUnformatted(row.modName.c_str());
						if (row.depth > 0) { ImGui::Unindent(ImGui::GetFontSize() * 0.9f); }
					}

					ImGui::TableSetColumnIndex(2);
					auto buffer = aliasBuffers.find(row.modName);
					if (buffer == aliasBuffers.end())
					{
						std::array<char, 64> fresh{};
						// A separator starts from the name it shows (Skyrim 2.1.1: a name the menu gave it, in the language picked).
						const std::string alias = row.separator ? row.displayName : personalization::GetAlias(row.modName);
						std::snprintf(fresh.data(), fresh.size(), "%s", alias.c_str());
						buffer = aliasBuffers.emplace(row.modName, fresh).first;
					}
					ImGui::SetNextItemWidth(-FLT_MIN);
					const bool committed =
						ImGui::InputTextWithHint("##alias", row.separator ? row.displayName.c_str() : row.modName.c_str(), buffer->second.data(),
												 buffer->second.size(), ImGuiInputTextFlags_EnterReturnsTrue);
						keyboard::NoteTextField(ImGui::GetItemID());
					if ((committed || ImGui::IsItemDeactivatedAfterEdit()) && !(row.separator && buffer->second[0] == '\0'))
					{
						personalization::SetAlias(row.modName, buffer->second.data());
						settings::Save();
					}

					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			// Applied once, after the table has closed, so the sequence only changes between frames
			// and never underneath the rows being drawn from it.
			if (!pendingMoveMod.empty())
			{
				personalization::MoveTo(entries, pendingMoveMod, pendingMovePosition);
				settings::Save();
			}
		}

		// ---- nested game-menu leaf panes (the author's game-menu-replacement model, 2026-08-28) ----

		// ---- Controls: two tabs, keyboard and controller, every function reboundable -------
		// The owner, 2026-09-19: "add a tab to the controls row to divide controller and keyboard
		// and let them rebind the different functions to different buttons/stick/mouse". Each tab
		// is one row per function: its name, what it is bound to now, and a button that captures
		// the next press. The two halves are deliberately the SAME list of functions, so a player
		// on a pad is never offered fewer controls than a player on a keyboard.
		void DrawControlsPane()
		{
			auto& values = settings::Get();
			ImGui::TextUnformatted(TR("AMF_Controls", "Controls"));
			ImGui::Separator();
			ImGui::TextWrapped("%s", TR("AMF_ControlsHelp2", "These are the framework's own controls - what moves through this menu and what opens "
							   "and closes it. A mod's own keys belong on that mod's page. Press Rebind and then the "
							   "key, mouse button, pad button or stick direction you want."));
			ImGui::Spacing();

			if (!ImGui::BeginTabBar("##controlstabs", ImGuiTabBarFlags_FittingPolicyScroll)) { return; }

			int index = 0;
			const auto tab = [&](const char* a_label, const char* a_name) {
				const ImGuiTabItemFlags flags =
					(index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				const bool open = BeginPageTab(a_label, flags);
				if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
				if (open) { g_tabIndex = index; g_tabName = a_name; }
				++index;
				return open;
			};

			// One tab's worth of rows. gamepadSide picks which half of each binding is shown and
			// which device the capture listens to; everything else is identical, on purpose.
			const auto drawRows = [&](bool a_gamepadSide) {
				if (!ImGui::BeginTable(a_gamepadSide ? "##padbinds" : "##keybinds", 3,
									   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					return;
				}
				// The key column is as wide as its widest entry (Skyrim 2.1.1): stretched by proportion, it clipped to "unbour",
				// "Backsp" and "Left stick lef" in a narrower window.
				float boundWidth = ImGui::CalcTextSize(TR("AMF_ColBoundTo", "Bound to")).x;
				for (int i = 0; i < static_cast<int>(bindings::Action::kCount); ++i)
				{
					const auto action = static_cast<bindings::Action>(i);
					const std::string bound = a_gamepadSide ? bindings::PadText(action) : bindings::KeyText(action);
					boundWidth = (std::max)(boundWidth, ImGui::CalcTextSize(bound.c_str()).x);
				}
				ImGui::TableSetupColumn(TR("AMF_ColFunction", "Function"));
				ImGui::TableSetupColumn(TR("AMF_ColBoundTo", "Bound to"), ImGuiTableColumnFlags_WidthFixed,
										boundWidth + ImGui::GetStyle().CellPadding.x * 2.0f);
				// As wide as Rebind and Unbind side by side (Skyrim 2.1.1), not a flat 13 em: at the default window size the
				// flat width left a gap after the buttons while the Function column clipped "Open and close the menu".
				const ImGuiStyle& cs = ImGui::GetStyle();
				const float buttonsWidth = ImGui::CalcTextSize(TR("AMF_BindRebind", "Rebind")).x + ImGui::CalcTextSize(TR("AMF_BindUnbindBtn", "Unbind")).x +
										   cs.FramePadding.x * 4.0f + cs.ItemSpacing.x + cs.CellPadding.x * 2.0f;
				ImGui::TableSetupColumn("##rebind", ImGuiTableColumnFlags_WidthFixed, buttonsWidth);
				ImGui::TableHeadersRow();

				for (int i = 0; i < static_cast<int>(bindings::Action::kCount); ++i)
				{
					const auto action = static_cast<bindings::Action>(i);
					ImGui::TableNextRow();
					ImGui::PushID(i + (a_gamepadSide ? 1000 : 0));

					ImGui::TableSetColumnIndex(0);
					ImGui::TextWrapped("%s", bindings::Label(action));   // wraps rather than clipping in a narrow window (Skyrim 2.1.1)
					if (const char* help = bindings::Description(action); help && help[0])
					{
						// Wrapped inside the column (Skyrim 2.1.1) - TextDisabled ran on past the cell and was cut mid-sentence.
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
						ImGui::TextWrapped("%s", help);
						ImGui::PopStyleColor();
					}

					ImGui::TableSetColumnIndex(1);
					const std::string bound = a_gamepadSide ? bindings::PadText(action) : bindings::KeyText(action);
					ImGui::TextUnformatted(bound.c_str());

					ImGui::TableSetColumnIndex(2);
					const bool capturingThis = bindings::IsCapturing() &&
											   bindings::CapturingAction() == action &&
											   bindings::CapturingGamepadSide() == a_gamepadSide;
					if (capturingThis)
					{
						ImGui::TextUnformatted(TR("AMF_BindPress", "press..."));
					}
					else
					{
						if (ImGui::Button(TR("AMF_BindRebind", "Rebind")))
						{
							bindings::BeginCapture(action, a_gamepadSide);
						}
						// Unbind sits beside Rebind and is shown only when there is something to
						// clear, so a row that is already unbound offers one button, not two.
						const bool bound = a_gamepadSide
							? bindings::Get(action).padKind != bindings::PadKind::kNone
							: bindings::Get(action).keyKind != bindings::KeyKind::kNone;
						if (bound)
						{
							ImGui::SameLine();
							if (ImGui::Button(TR("AMF_BindUnbindBtn", "Unbind")))
							{
								bindings::Unbind(action, a_gamepadSide);
								settings::Save();
							}
						}
					}

					ImGui::PopID();
				}
				ImGui::EndTable();

				if (bindings::IsCapturing() && bindings::CapturingGamepadSide() == a_gamepadSide)
				{
					ImGui::Spacing();
					ImGui::TextWrapped("%s", a_gamepadSide
						? TR("AMF_BindPadPrompt", "Press a pad button, click a stick, or push a stick in the direction you want. B cancels.")
						: TR("AMF_BindKeyPrompt", "Press a key or a mouse button. Escape cancels."));
				}
				if (const char* refused = bindings::LastRefusal(); refused && refused[0])
				{
					ImGui::Spacing();
					ImGui::TextWrapped("%s", refused);
				}
			};

			if (tab(TR("AMF_TabKeyboard", "Keyboard and mouse"), "Keyboard and mouse"))
			{
				ImGui::Spacing();
				drawRows(false);
				ImGui::EndTabItem();
			}
			if (tab(TR("AMF_TabController", "Controller"), "Controller"))
			{
				ImGui::Spacing();
				drawRows(true);
				ImGui::EndTabItem();
			}

			g_tabCount = index;
			g_tabRequest = -1;
			ImGui::EndTabBar();

			ImGui::Spacing();
			ImGui::Separator();
			if (ImGui::Button(TR("AMF_BindSave", "Save controls")))
			{
				settings::Save();
				g_menuListSavedAt = ImGui::GetTime();
			}
			if (g_menuListSavedAt > 0.0 && ImGui::GetTime() - g_menuListSavedAt < 3.0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", TR("AMF_MenuListSaved", "saved"));
			}
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_BindReset", "Reset every control")))
			{
				bindings::ResetToDefaults();
				settings::Save();
			}
			// Its own wrapped line (Skyrim 2.1.1): beside the buttons it ran off the pane's edge.
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", TR("AMF_BindNote", "Reserved keys are refused, and two functions that can be used at the same time cannot share a control."));
			ImGui::PopStyleColor();
			(void)values;
		}

		// ---- Help: the instruction manual, in tabs (the owner, 2026-09-19: "the help row should
		// have tabs: controls, features, readme, and others as you see fit") -----------------
		// A page a player can actually learn the menu from, rather than two paragraphs saying
		// where things live. The bar is submitted exactly like a mod's own page bar, and reports
		// the same tab count and index, so the D-pad walks these tabs the way it walks any other -
		// onto the tab itself, never by stepping sideways off a control.
		void DrawHelpPane()
		{
			ImGui::TextUnformatted(TR("AMF_Help", "Help"));
			ImGui::Separator();

			const auto para = [](const char* a_text) { ImGui::TextWrapped("%s", a_text); ImGui::Spacing(); };
			const auto bullet = [](const char* a_text) { ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%s", a_text); };

			if (!ImGui::BeginTabBar("##helptabs", ImGuiTabBarFlags_FittingPolicyScroll)) { return; }

			int index = 0;
			const auto tab = [&](const char* a_label, const char* a_name) {
				const ImGuiTabItemFlags flags =
					(index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				const bool open = BeginPageTab(a_label, flags);
				if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
				if (open) { g_tabIndex = index; g_tabName = a_name; }
				++index;
				return open;
			};

			if (tab(TR("AMF_HelpTabControls", "Controls"), "Controls"))
			{
				ImGui::Spacing();
				ImGui::SeparatorText(TR("AMF_ManOpening", "Opening and closing the menu"));
				para(TR("AMF_ManOpening1", "Press F1 to open the menu and F1 again to close it. Escape closes it too. The key "
						"is yours to change: Controls -> Open and close the menu -> Rebind, then press the key you want."));
				para(TR("AMF_ManOpening2", "On a controller, open it from the game's own menu: \"Apocrypha Menu Framework\" sits just above Settings on the title screen and in the pause menu. Start or B closes it. F1 (or the key you set under Controls) works too, and the menu follows whichever you touched last."));
				para(TR("AMF_ManOpening3", "While the menu is up the game does not see your keys or your mouse, so the camera and "
						"your character stay still. Mods' own hotkeys are held off as well, so a key that opens "
						"something else cannot fire while you are reading a page."));

				ImGui::SeparatorText(TR("AMF_ManMoving", "Moving around"));
				bullet(TR("AMF_ManMoving1", "Mouse: point and click, as anywhere else. The cursor is drawn by the menu itself."));
				bullet(TR("AMF_ManMoving2", "Keyboard: the arrow keys move the highlight, Enter activates, Escape closes."));
				bullet(TR("AMF_ManMoving3", "Controller: the D-pad and the left stick move the highlight, A activates, B goes back. "
						  "Take hold of a slider with A and the RIGHT stick moves it, so adjusting a value never "
						  "also moves the highlight."));
				bullet(TR("AMF_ManMoving4", "Left and right cross between the list and the page beside it. The bumpers - Page Up and Page Down on a keyboard - step through the tabs at the top of a page; moving sideways never changes the tab under you."));
				bullet(TR("AMF_ManMoving6", "Right-click a mod in the list, or press Y on a controller, for its options."));
				ImGui::Spacing();
				para(TR("AMF_ManMoving5", "The menu follows whatever you last used: touch the pad and it switches to controller "
						"navigation, touch the mouse or a key and it switches back. There is nothing to set."));

				ImGui::SeparatorText(TR("AMF_ManTyping", "Typing"));
				para(TR("AMF_ManTyping1", "Click a text box and type. Ctrl+A selects everything in it, and Ctrl+C, Ctrl+X, "
						"Ctrl+V and Ctrl+Z work as they do anywhere."));
				para(TR("AMF_ManTyping2", "On a controller, put the highlight on a text box and press A: a key grid appears "
						"across the bottom of the screen. The D-pad walks it, A types, B puts the highlight back "
						"on the box, X is shift and Y is backspace. It works on every mod's page, and it can be "
						"turned off under Settings."));
				ImGui::EndTabItem();
			}

			if (tab(TR("AMF_HelpTabFeatures", "Features"), "Features"))
			{
				ImGui::Spacing();
				ImGui::SeparatorText(TR("AMF_ManList", "The mod list"));
				para(TR("AMF_ManList1", "Every mod that registers a page appears under Mods, with the framework's own Settings, "
						"Controls and this Help page above it. Type in the Search box to narrow the list - two or "
						"three letters is usually enough - and it matches whatever name the entry is showing."));
				bullet(TR("AMF_ManList2", "A-Z and Z-A beside Mods sort the list. Turn both off and the list goes back to the order "
						  "you arranged it in."));
				bullet(TR("AMF_ManList3", "Right-click a mod (or press Y on a controller) for its options: add it to your "
						  "favourites, rename it, or move it to the top."));
				bullet(TR("AMF_ManList4", "A favourite sits at the top of the list with a filled white box beside its name. "
						  "Favourites keep the order you added them in, so a new one lands after the last."));
				bullet(TR("AMF_ManList5", "Renaming changes only what this menu shows. The mod itself never sees it, and the "
						  "search box finds the entry by the name you gave it."));
				ImGui::Spacing();
				para(TR("AMF_ManList6", "Settings -> Menu list has the same controls as a table, with a position number you can "
						"type into: put 3 in a row's number and it moves there, and everything else re-flows around it."));

				ImGui::SeparatorText(TR("AMF_ManLook", "How it looks"));
				bullet(TR("AMF_ManLook1", "Theme: Skellige, the default, is grey lines on the black of the game's menu with a blood-red highlight; "
						  "Untarnished is plain, and Oblivion and Skyrim are the looks of the framework's other builds. Settings -> Appearance -> Theme."));
				bullet(TR("AMF_ManLook2", "Font: drop a .ttf into bin/x64_dx12/AMF/fonts and pick it under "
						  "Settings -> Appearance -> Font."));
				bullet(TR("AMF_ManLook3", "Text size scales on top of the automatic resolution scale, so the menu reads the same on "
						  "a 1080p screen and a 4K one."));
				bullet(TR("AMF_ManLook4", "Language: the framework's own text follows the game's text language, set in the game's own options, unless you force one."));
				bullet(TR("AMF_ManLook5", "Drag the top row - the name and version - to move the window, and an edge or a corner "
						  "to resize it; it opens where you left it, at that size. Either can be switched off under Settings -> Appearance."));
				ImGui::EndTabItem();
			}

			if (tab(TR("AMF_HelpTabReadme", "Readme"), "Readme"))
			{
				ImGui::Spacing();
				ImGui::TextWrapped("%s", TR("AMF_Help1", "ApocryphaRealm Menu Framework presents mod settings in one menu: a list down the side, and the selected entry's options here."));
				ImGui::Spacing();
				ImGui::TextWrapped("%s", TR("AMF_Help2", "Mod settings live under Mods in the list on the left. The framework's own options are under Settings, and its key bindings under Controls."));
				ImGui::Spacing();
				para(TR("AMF_ReadmeWhat", "It is one menu for every mod that asks for one. A mod does not have to know anything "
						"about this framework's look, its themes or its controller support - it hands over its "
						"settings and gets all of it."));
				para(TR("AMF_ReadmeCompat", "This is The Witcher 3 build of the framework. Each Witcher 3 mod's own settings menu - the ones under the game's Options > Mods - is shown here as a page of its own, and a change made there is saved the same way the game's Options > Mods saves it."));
				para(TR("AMF_ReadmeAuthors", "For mod authors: one header, AMF.h, is the whole API. Register pages, draw them with Dear ImGui through the framework's own context, and the menu does the rest - layout, theme, font, translation, keyboard, controller and the on-screen keyboard. The header is safe when the framework is not installed."));
				para(TR("AMF_ReadmeFiles", "Your settings are kept in bin/x64_dx12/AMF/User.ini, a file the download never contains, "
						"so an update keeps them; ApocryphaMenuFramework.ini beside it holds the defaults. The log is in "
						"Documents/The Witcher 3/AMF/."));
				ImGui::EndTabItem();
			}

			if (tab(TR("AMF_HelpTabTrouble", "Troubleshooting"), "Troubleshooting"))
			{
				ImGui::Spacing();
				bullet(TR("AMF_ManTrouble1", "A mod's page is missing: the mod has not registered one, or it needs a newer framework "
						  "than the one installed. Its own log will say."));
				bullet(TR("AMF_ManTrouble2", "The menu will not open: the framework is bin/x64_dx12/ApocryphaMenuFramework.asi, loaded by the dinput8.dll that comes with it - if either file is missing, the framework is not loaded at all and its log file does not exist. If it is loaded, something else may have taken F1; rebind it under Controls."));
				bullet(TR("AMF_ManTrouble3", "A key does nothing inside the menu: another mod may be claiming it. The framework's log "
						  "names the device and key whenever that happens."));
				bullet(TR("AMF_ManTrouble5", "The game reacts to a key, click or pad button you used inside the menu: that is a bug - while the menu is up the framework takes every one of them. Send the log with what you pressed and where the cursor was."));
				ImGui::Spacing();
				para(TR("AMF_ManTrouble4", "The log is at Documents/The Witcher 3/AMF/ApocryphaMenuFramework.log. "
						"uLogLevel under [Log] in ApocryphaMenuFramework.ini decides how much it writes."));
				ImGui::EndTabItem();
			}

			g_tabCount = index;
			g_tabRequest = -1;
			ImGui::EndTabBar();
		}

		void DrawFrameworkWindow()
		{
			const ImVec2 display = ImGui::GetIO().DisplaySize;
			// The band round the screen the window never reaches (EdgeMargin), and the room left inside it.
			const float edge = EdgeMargin(display);
			const ImVec2 room(std::max(1.0f, display.x - edge * 2.0f), std::max(1.0f, display.y - edge * 2.0f));

			// TWO PROFILES, NOT TWO PRESETS (author, 2026-09-04: "lets have it treat them as
			// profiles to save the users settings to so that we set the default to vanilla
			// positioning and size and if their ui mod does different then they can change it and
			// it will remember").
			//
			// Each way of opening the framework has its own saved geometry. Until the player moves
			// or resizes that window, the profile is unset and takes its DEFAULT - the measured
			// journal panel when nested, the centred proportions otherwise. The first drag or
			// resize fills the profile in, and from then on that is what the window opens at.
			//
			// This supersedes the 2026-08-27 "preset positions, never free placement" decision for
			// this window. That rule existed so the window could not be lost off-screen or left
			// somewhere useless; a REMEMBERED position with a reset button gives the same safety
			// while letting someone whose menu replacer puts its panel elsewhere fix it once. The
			// preset below still decides where an unset profile centres itself.
			//
			// Geometry is kept as fractions of the display, so the numbers stay correct if the
			// resolution changes between sessions.
			ImVec2 anchor{ 0.5f, 0.5f };
			switch (settings::Get().windowPreset)
			{
			default:
				break;  // 0 (and any unknown value) = centre
			}

			// Each mode Begin()s a DIFFERENT ImGui id, so ImGui also keeps their layout entries
			// apart. Sharing one id is what let the nested mode's forced size overwrite a size the
			// apart. The text after ### is the identity and is not displayed. ### rather than ##
			// on purpose: with ##, ImGui hashes the WHOLE label, so changing the visible name
			// would silently orphan that saved entry - exactly what renaming this to
			// "ApocryphaRealm" would otherwise have done. With ###, the id is the id and the
			// shown text is free to change.
			const bool nested = g_nested.load(std::memory_order_acquire);
			const char* windowId = nested ? "ApocryphaRealm Menu Framework###amf-nested"
										  : "ApocryphaRealm Menu Framework###amf-main";
			// OBLIVION KEEPS TWO PLACEMENTS (2026-10-02). The Skyrim framework's 2.0 made both ways in one centred window; here
			// the System-row window stays on the right of the screen (the owner, 2026-09-29: "so that it doesn't block out the
			// view of the system rows to its left") and the key-opened one where it was left. Each remembers its own geometry.
			settings::WindowGeometry& profile =
				nested ? settings::Get().nestedWindow : settings::Get().hotkeyWindow;

			// ---- this profile's default, in screen fractions ----
			float dx = 0.0f, dy = 0.0f, dw = 0.55f, dh = 0.70f;
			bool haveDefault = false;
			if (nested)
			{
				// The panel is measured off the LIVE movie rather than assumed: its size differs
				// under every art replacer, which is the same reason the row is injected rather
				// than shipped. The last good measurement is kept, because the journal fades its
				// panel in and a frame where the read fails must not move the window.
				static float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
				float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
				if (systemrow::GetPanelRect(x, y, w, h))
				{
					px = x; py = y; pw = w; ph = h;
				}
				if (pw > 0.0f && ph > 0.0f)
				{
					// Inset by the window padding: ImGui strokes its border ON the rect it is given
					// and the journal strokes its panel border on the same line, so filling the rect
					// exactly puts two borders flush together and reads as one thick misaligned
					// rule. Padding comes from the active style, so it tracks the theme and the text
					// size rather than being right at one of them only.
					const ImVec2 pad = ImGui::GetStyle().WindowPadding;
					dx = px + pad.x / display.x;
					dy = py + pad.y / display.y;
					dw = pw - (pad.x * 2.0f) / display.x;
					dh = ph - (pad.y * 2.0f) / display.y;
					haveDefault = dw > 0.0f && dh > 0.0f;
				}
				if (!haveDefault)
				{
					// OBLIVION REMASTERED (1.0.4): no journal panel to measure - the window opened from the System row
					// sits on the RIGHT of the screen so the page's rows on the left stay in view (the owner,
					// 2026-09-29: "reposition itself ... so that it doesn't block out the view of the system rows to its
					// left"). The numbers are where he dragged it to that day, read back from the saved hotkey profile:
					// x 0.316, y 0.132, w 0.685, h 0.70 of the display (1011, 238, 2193 x 1260 at 3200x1800). A drag
					// afterwards is remembered as the nested profile, as before.
					dx = 0.315937f; dy = 0.132222f; dw = 0.685313f; dh = 0.70f;
					haveDefault = true;
				}
			}
			else
			{
				dx = anchor.x - dw * 0.5f;
				dy = anchor.y - dh * 0.5f;
				haveDefault = true;
			}

			// ---- apply, ONCE per opening ----
			// Only on the frame the window is opened, so a drag afterwards is not undone the next
			// frame. If the nested measurement is not ready yet the flag is left set and the next
			// frame tries again, rather than falling back to the centre and jumping later.
			bool appliedThisFrame = false;
			const auto& sv = settings::Get();
			// NO TITLE BAR (the owner, 2026-10-02: "do the same thing that AMF for Skyrim did by removing the top bar and
			// ... connecting the frame on all four sides" - the Skyrim framework's 2.0.0). The frame now runs round the
			// window's own top edge and the collapse arrow is gone; the window closes by its key or the Start button.
			// NoMove stays (Skyrim 2.1.1): without a title bar ImGui's own move would let any empty part of the window drag
			// it (ConfigWindowsMoveFromTitleBarOnly only restrains windows WITH a title bar), so a page's slider drag or row
			// click could move the window - which the owner ruled out on 2026-09-19 ("we need to make it so you can't drag
			// AMF by anything but the top bar"). The top row is the handle instead - see "THE TOP ROW MOVES THE WINDOW".
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
			// Resize the window OFF really is off (Skyrim 2.1.1 - the owner, 2026-10-05: "make sure the toggle actually toggles
			// off the resizing"): no edge or corner resizes it. On (the default), every edge and corner does, freely.
			if (!sv.freeResize) { windowFlags |= ImGuiWindowFlags_NoResize; }
			{
				static int s_loggedFlags = -1;   // render thread; transition log only
				const int now = (sv.movableWindow ? 1 : 0) | (sv.freeResize ? 2 : 0);
				if (now != s_loggedFlags)
				{
					s_loggedFlags = now;
					logger::debug("window: move by the top row {}, resize by the edges {}", sv.movableWindow ? "on" : "off",
								  sv.freeResize ? "on (free - width and height alike)" : "off (NoResize)");
				}
			}
			// The KEY-OPENED window is held at a centre point every frame, so an edge drag grows both sides about it and
			// the window can never be larger than the game screen (1.7.7/1.7.8, the owner, 2026-09-13). Done through
			// ImGui's own size constraint callback, so ImGui resizes once per frame with the rule already applied and
			// nothing is forced afterwards (no jumping). Until Skyrim 2.1.1 a corner drag also kept the window's SHAPE;
			// with Resize the window on it is free - width and height each follow the mouse (Barzing on Nexus,
			// 2026-10-05: "the possibility to resize window also in height size"). None of it applies to the nested
			// window, which keeps its own placement below.
			// safe/moveL..moveB (W3 1.0.4): the rect inside the edge band, and which of the window's sides a resize held this
			// frame is moving. A corner drag past the screen used to be applied and DRAWN inside Begin before anything clamped
			// it, so the grip and the border were drawn in the band for that frame. Limited here, inside ImGui's own size
			// callback, the size a drag asks for keeps every moving side inside the band on the frame it is drawn.
			struct HotkeyConstraint
			{
				ImVec2 display{};
				float  aspect = 0.0f;
				bool   free = false;
				bool   centred = true;   // the key-opened window (held at a centre); the nested one is placed by its top-left
				ImRect safe{};
				bool   moveL = false, moveR = false, moveT = false, moveB = false;
			};
			static HotkeyConstraint s_hotkeyConstraint{};
			// The size callback: the room inside the band, the shape a non-free corner drag keeps, and the moving sides held
			// inside the band. Pos and CurrentSize are the window's on this frame (ImGui calls it from the resize in Begin).
			const ImGuiSizeCallback sizeCallback = +[](ImGuiSizeCallbackData* a_data) {
				auto* c = static_cast<HotkeyConstraint*>(a_data->UserData);
				const bool wChanged = std::fabs(a_data->DesiredSize.x - a_data->CurrentSize.x) > 0.5f;
				const bool hChanged = std::fabs(a_data->DesiredSize.y - a_data->CurrentSize.y) > 0.5f;
				// The largest each side may be: the room, less whatever would carry a MOVING side into the band. A side that
				// stays put is where it is; the window was inside the band when the drag began.
				ImVec2 most = c->display;
				if (c->moveR) { most.x = std::min(most.x, c->safe.Max.x - a_data->Pos.x); }
				if (c->moveL) { most.x = std::min(most.x, a_data->Pos.x + a_data->CurrentSize.x - c->safe.Min.x); }
				if (c->moveB) { most.y = std::min(most.y, c->safe.Max.y - a_data->Pos.y); }
				if (c->moveT) { most.y = std::min(most.y, a_data->Pos.y + a_data->CurrentSize.y - c->safe.Min.y); }
				most.x = std::max(1.0f, most.x);
				most.y = std::max(1.0f, most.y);
				// With Resize the window on (Skyrim 2.1.1) a corner drag is free - width and height each follow the mouse.
				const bool corner = c->centred && wChanged && hChanged && c->aspect > 0.0f && !c->free;
				if (corner)
				{
					a_data->DesiredSize.y = a_data->DesiredSize.x / c->aspect;   // keep the shape
				}
				const ImVec2 asked = a_data->DesiredSize;
				a_data->DesiredSize.x = std::min(a_data->DesiredSize.x, most.x);
				a_data->DesiredSize.y = std::min(a_data->DesiredSize.y, most.y);
				if (corner && a_data->DesiredSize.y * c->aspect < a_data->DesiredSize.x)
				{
					a_data->DesiredSize.x = a_data->DesiredSize.y * c->aspect;   // the clamp held one side: keep the shape
				}
				// Transition log: a drag reached the band and is being held at it (render thread only).
				const bool held = (c->moveL || c->moveR || c->moveT || c->moveB) &&
								  (asked.x > a_data->DesiredSize.x + 0.5f || asked.y > a_data->DesiredSize.y + 0.5f);
				static bool s_held = false;
				if (held != s_held)
				{
					s_held = held;
					logger::debug("window: resize {} the edge band (moving {}{}{}{}) - size {:.0f} x {:.0f} asked, {:.0f} x {:.0f} given", held ? "held at" : "clear of",
								  c->moveL ? "L" : "", c->moveR ? "R" : "", c->moveT ? "T" : "", c->moveB ? "B" : "", asked.x, asked.y,
								  a_data->DesiredSize.x, a_data->DesiredSize.y);
				}
			};
			// Which sides a resize is moving, read from ImGui's active item BEFORE Begin: the grips and borders have fixed ids
			// (GetWindowResizeCornerID / GetWindowResizeBorderID), and a pad/keyboard resize (ImGui's window switcher) moves
			// the right and bottom. Nothing is moving when none of them holds the active id.
			ImGuiWindow* const existing = ImGui::FindWindowByName(windowId);
			{
				HotkeyConstraint& c = s_hotkeyConstraint;
				c.safe = ImRect(ImVec2(edge, edge), ImVec2(edge + room.x, edge + room.y));
				c.moveL = c.moveR = c.moveT = c.moveB = false;
				ImGuiContext& g = *GImGui;
				if (existing && g.ActiveId != 0)
				{
					for (int n = 0; n < 4; ++n)
					{
						if (g.ActiveId == ImGui::GetWindowResizeCornerID(existing, n))
						{
							// 0 lower-right, 1 lower-left, 2 upper-left, 3 upper-right (ImGui's resize_grip_def)
							c.moveR = n == 0 || n == 3;
							c.moveL = n == 1 || n == 2;
							c.moveB = n == 0 || n == 1;
							c.moveT = n == 2 || n == 3;
						}
					}
					if (g.ActiveId == ImGui::GetWindowResizeBorderID(existing, ImGuiDir_Left)) { c.moveL = true; }
					if (g.ActiveId == ImGui::GetWindowResizeBorderID(existing, ImGuiDir_Right)) { c.moveR = true; }
					if (g.ActiveId == ImGui::GetWindowResizeBorderID(existing, ImGuiDir_Up)) { c.moveT = true; }
					if (g.ActiveId == ImGui::GetWindowResizeBorderID(existing, ImGuiDir_Down)) { c.moveB = true; }
				}
				if (existing && g.NavWindowingTarget && g.NavWindowingTarget->RootWindowDockTree == existing) { c.moveR = c.moveB = true; }
				static int s_loggedSides = -1;   // render thread; transition log only
				const int sides = (c.moveL ? 1 : 0) | (c.moveR ? 2 : 0) | (c.moveT ? 4 : 0) | (c.moveB ? 8 : 0);
				if (sides != s_loggedSides)
				{
					s_loggedSides = sides;
					if (sides)
					{
						logger::debug("window: resize moving {}{}{}{} - those sides held inside ({:.0f}, {:.0f}) - ({:.0f}, {:.0f})",
									  c.moveL ? "L" : "", c.moveR ? "R" : "", c.moveT ? "T" : "", c.moveB ? "B" : "",
									  c.safe.Min.x, c.safe.Min.y, c.safe.Max.x, c.safe.Max.y);
					}
					else
					{
						logger::debug("window: no resize in progress");
					}
				}
			}
			// The centre the key-opened window is held at: the screen's, or wherever the player has dragged the top row to
			// (Skyrim 2.1.1). ImGui's own move never runs (NoMove above); the drag handle moves this point instead.
			static ImVec2 s_hotCentre{ -1.0f, -1.0f };
			if (!nested)
			{
				if (g_applyGeometry.load(std::memory_order_acquire))
				{
					// Never larger than the room inside the edge band, whatever a saved profile says.
					const float ex = display.x >= 1.0f ? edge / display.x : 0.0f;
					const float ey = display.y >= 1.0f ? edge / display.y : 0.0f;
					// beside the game menu's column while that menu is open (bSnapToGameMenu), else where it was left
					const bool snap = sv.snapToGameMenu && g_gameMenuOpen.load(std::memory_order_acquire);
					const bool own = !snap && profile.IsSet();
					const float gw = std::min(own ? profile.w : kColumnW, 1.0f - ex * 2.0f);
					const float gh = std::min(own ? profile.h : kColumnH, 1.0f - ey * 2.0f);
					ImGui::SetNextWindowSize(ImVec2(display.x * gw, display.y * gh), ImGuiCond_Always);
					// WHERE THE PLAYER LEFT IT (Skyrim 2.1.1 - Barzing on Nexus, 2026-10-05: "the possibility to move the
					// window"): its saved top-left plus half its size is the centre it is held at - only with Move the window
					// on; off, and the first time or after Reset, the screen's centre. Kept whole on the screen, inside the band.
					const bool free = !snap && sv.movableWindow && profile.IsSet();
					float cx = free ? profile.x + gw * 0.5f : kColumnX + gw * 0.5f;
					float cy = free ? profile.y + gh * 0.5f : kColumnY + gh * 0.5f;
					cx = std::clamp(cx, ex + gw * 0.5f, std::max(ex + gw * 0.5f, 1.0f - ex - gw * 0.5f));
					cy = std::clamp(cy, ey + gh * 0.5f, std::max(ey + gh * 0.5f, 1.0f - ey - gh * 0.5f));
					s_hotCentre = ImVec2(display.x * cx, display.y * cy);
					g_applyGeometry.store(false, std::memory_order_release);
					appliedThisFrame = true;
					s_hotkeyConstraint.aspect = 0.0f;
					logger::info("window: opened {} at centre ({:.3f}, {:.3f}), size {:.3f} x {:.3f} of the screen",
								 snap ? "beside the game menu's column" : free ? "where it was left" : "at its standard place", cx, cy, gw, gh);
				}
				if (s_hotCentre.x < 0.0f) { s_hotCentre = ImVec2(display.x * (kColumnX + kColumnW * 0.5f), display.y * (kColumnY + kColumnH * 0.5f)); }
				s_hotkeyConstraint.display = room;   // the largest it may be: the screen less the edge band on each side
				s_hotkeyConstraint.free = sv.freeResize;
				s_hotkeyConstraint.centred = true;
				ImGui::SetNextWindowSizeConstraints(ImVec2(display.x * 0.2f, display.y * 0.2f), room, sizeCallback, &s_hotkeyConstraint);
				// THE CENTRE IS KEPT WHERE THE WHOLE WINDOW FITS INSIDE THE BAND, BEFORE BEGIN (W3 1.0.4). A corner drag grows the
				// window from its top-left on the frame of the drag; the next frame centres it again about this point, and a
				// centre left where it was then put the far side into the band for one drawn frame before keepOnScreen moved it.
				// The size used is the one ImGui will draw at: the one just applied on opening, otherwise the window's own.
				if (!appliedThisFrame && existing && existing->SizeFull.x > 0.0f && existing->SizeFull.y > 0.0f)
				{
					const ImVec2 half(std::min(existing->SizeFull.x, room.x) * 0.5f, std::min(existing->SizeFull.y, room.y) * 0.5f);
					const ImVec2 was = s_hotCentre;
					s_hotCentre.x = std::clamp(s_hotCentre.x, edge + half.x, std::max(edge + half.x, display.x - edge - half.x));
					s_hotCentre.y = std::clamp(s_hotCentre.y, edge + half.y, std::max(edge + half.y, display.y - edge - half.y));
					static bool s_moved = false;   // render thread; transition log only
					const bool moved = was.x != s_hotCentre.x || was.y != s_hotCentre.y;
					if (moved != s_moved)
					{
						s_moved = moved;
						if (moved)
						{
							logger::debug("window: centre moved in from ({:.0f}, {:.0f}) to ({:.0f}, {:.0f}) so the {:.0f} x {:.0f} window stays inside the band",
										  was.x, was.y, s_hotCentre.x, s_hotCentre.y, half.x * 2.0f, half.y * 2.0f);
						}
					}
				}
				// Held at its centre every frame - that is what keeps an edge resize symmetric. A drag of the top row moves the
				// centre itself (below), so the window follows on the next frame.
				ImGui::SetNextWindowPos(s_hotCentre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			}
			else
			{
				// The nested window (DevBench's nested open, the System-row placement) is placed by its top-left, not held at a
				// centre; a resize of it is held inside the band the same way (W3 1.0.4).
				s_hotkeyConstraint.display = room;
				s_hotkeyConstraint.free = true;
				s_hotkeyConstraint.centred = false;
				ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), room, sizeCallback, &s_hotkeyConstraint);
			}
			if (nested && g_applyGeometry.load(std::memory_order_acquire))
			{
				bool useProfile = profile.IsSet();
				// 1.8.1: the nested profile is remembered PER JOURNAL ART (the owner, 2026-09-13: "the
				// position should only change to match the redesign when the redesign is active"). The
				// art on screen is what GetPanelRect just measured; a position dragged under other art
				// is ignored, the journal is measured afresh, and the next drag saves for this art.
				const std::string artNow = systemrow::ArtKey();
				if (useProfile && haveDefault && profile.art != artNow)
				{
					useProfile = false;
					static std::string s_saidFor;
					if (s_saidFor != artNow)
					{
						s_saidFor = artNow;
						logger::info("window profile (nested) was saved under journal art '{}'; the journal on screen is '{}', so the measured panel is used instead",
							profile.art.empty() ? "unknown" : profile.art, artNow);
					}
				}
				if (useProfile || haveDefault)
				{
					// Inside the edge band from the frame it opens (W3 1.0.4): no larger than the room, its top-left moved in.
					const float ex = display.x >= 1.0f ? edge / display.x : 0.0f;
					const float ey = display.y >= 1.0f ? edge / display.y : 0.0f;
					const float gw = std::min(useProfile ? profile.w : dw, 1.0f - ex * 2.0f);
					const float gh = std::min(useProfile ? profile.h : dh, 1.0f - ey * 2.0f);
					const float gx = std::clamp(useProfile ? profile.x : dx, ex, std::max(ex, 1.0f - ex - gw));
					const float gy = std::clamp(useProfile ? profile.y : dy, ey, std::max(ey, 1.0f - ey - gh));
					ImGui::SetNextWindowPos(ImVec2(display.x * gx, display.y * gy), ImGuiCond_Always);
					ImGui::SetNextWindowSize(ImVec2(display.x * gw, display.y * gh), ImGuiCond_Always);
					g_applyGeometry.store(false, std::memory_order_release);
					appliedThisFrame = true;
					// Evidence for the owner's standing requirement (2026-09-21): the System-row window reopens where
					// the player left it, across game reloads. One line per opening says which geometry was used.
					logger::info("window profile (nested) applied on open: {} x={:.3f} y={:.3f} w={:.3f} h={:.3f} (art '{}')",
						useProfile ? "the player's saved position" : "the measured journal panel (no saved position for this art)",
						gx, gy, gw, gh, artNow);
				}
			}

			if (ImGui::Begin(windowId, nullptr, windowFlags))
			{
				// KEPT INSIDE THE EDGE BAND, every frame and after anything that grows the window (EdgeMargin). Before the
				// window is drawn, so no frame of it ever lands in the band: a size over the room is cut back for the next
				// frame, and the position is moved in now. The key-opened window's held centre follows, so it stays put.
				const auto keepOnScreen = [&](const char* a_why) {
					ImGuiWindow* self = ImGui::GetCurrentWindow();
					const ImVec2 size(std::min(self->Size.x, room.x), std::min(self->Size.y, room.y));
					const bool cut = size.x < self->Size.x || size.y < self->Size.y;
					if (cut) { ImGui::SetWindowSize(size); }
					const ImVec2 pos(std::max(edge, std::min(self->Pos.x, edge + room.x - size.x)),
									 std::max(edge, std::min(self->Pos.y, edge + room.y - size.y)));
					const bool moved = pos.x != self->Pos.x || pos.y != self->Pos.y;
					if (moved) { ImGui::SetWindowPos(pos); }
					if (!nested && (moved || cut)) { s_hotCentre = ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f); }
					static bool s_held = false;   // render thread; transition log only
					if ((moved || cut) != s_held)
					{
						s_held = moved || cut;
						if (s_held)
						{
							logger::debug("window: kept {:.0f} px inside the screen edge ({}) - pos ({:.0f}, {:.0f}) size {:.0f} x {:.0f}",
										  edge, a_why, pos.x, pos.y, size.x, size.y);
						}
					}
				};
				keepOnScreen("placed");
				{
					const ImVec2 rp = ImGui::GetWindowPos();
					const ImVec2 rs = ImGui::GetWindowSize();
					std::scoped_lock l(g_selLock);
					g_mainX = rp.x; g_mainY = rp.y; g_mainW = rs.x; g_mainH = rs.y;
				}
				// The Screenshot control (1.0.6): raised only while this window is open, consumed here once per frame.
				// Not while the on-screen keyboard is up: View is its Done button too, and one press must not do both.
				if (bindings::TakeTriggered(bindings::Action::kScreenshot) && !keyboard::Capturing()) { screenshot::Take(); }
				if (!nested)
				{
					// The shape a corner drag keeps (Resize the window off is NoResize, so this matters only to a build
					// that turns the free corner off again) is the shape the window had when the drag began: refreshed
					// every frame the mouse is up, frozen while it is down.
					const ImVec2 cur = ImGui::GetWindowSize();
					if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && cur.x > 1.0f && cur.y > 1.0f)
					{
						s_hotkeyConstraint.aspect = cur.x / cur.y;
					}
				}
				// The author's background, before any content: ImGui has already painted the window's
				// own colour, and everything drawn after this lands on top of the art.
				if (skin::HasBackground())
				{
					const ImVec2 bp = ImGui::GetWindowPos();
					const ImVec2 bs = ImGui::GetWindowSize();
					DrawSkinBackground(ImGui::GetWindowDrawList(), bp, ImVec2(bp.x + bs.x, bp.y + bs.y));
				}
				// REMEMBER WHERE THE PLAYER LEAVES IT. Written back as fractions of the display, so
				// the profile stays correct if the resolution changes between sessions.
				//
				// Not on the frame we just placed the window - that would record our own default as
				// though the player had chosen it, and the profile would never be "unset" again, so
				// Reset could not restore the journal fit. And not mid-drag either: the settings
				// file is rewritten on each save, and a drag would rewrite it every frame. Waiting
				// for the mouse to come up saves once, when the player has finished.
				// Never with no display size: minimised or driven in the background the client area is 0x0, and the fractions
				// came out inf - saved, the next opening filled the whole screen (W3 M1.2 run, 2026-10-05).
				if (!appliedThisFrame && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && display.x >= 1.0f && display.y >= 1.0f)
				{
					// Saved as drawn, which keepOnScreen has already held inside the edge band; the size is capped to the room
					// as well, for the one frame a cut-back size (SetWindowSize) has not been applied yet.
					const ImVec2 wpos = ImGui::GetWindowPos();
					const ImVec2 wsize(std::min(ImGui::GetWindowSize().x, room.x), std::min(ImGui::GetWindowSize().y, room.y));
					const float nx = wpos.x / display.x;
					const float ny = wpos.y / display.y;
					const float nw = wsize.x / display.x;
					const float nh = wsize.y / display.y;
					const auto moved = [](float a, float b) { return std::fabs(a - b) > 0.001f; };
					if (moved(nx, profile.x) || moved(ny, profile.y) ||
						moved(nw, profile.w) || moved(nh, profile.h))
					{
						profile.x = nx; profile.y = ny; profile.w = nw; profile.h = nh;
						if (nested) { profile.art = systemrow::ArtKey(); }   // 1.8.1: remembered for THIS journal art
						settings::Save();
						logger::debug("window profile ({}) saved: x={:.3f} y={:.3f} w={:.3f} h={:.3f}",
							nested ? "nested" : "hotkey", nx, ny, nw, nh);
					}
				}

				// Version always on show - a version-less status line reads as a stale build
				// (the author, third smoke test).
				static const std::string version = AMF_VERSION;
				ImGui::Text("ApocryphaRealm Menu Framework  v%s", version.c_str());
				// THE TOP ROW MOVES THE WINDOW (Skyrim 2.1.1 - Barzing on Nexus, 2026-10-05: "the possibility to move the
				// window"). The window has no title bar, and its body must never drag it: a page's slider drag or row click
				// would move the window instead. So the band from the window's top edge to the bottom of this line is an
				// invisible handle: drag it and the window follows; let go and the place is saved with the size (above, on
				// the frame the mouse is up). ImGui's own edge-resize zones are tested in Begin, before any item, so the very
				// edge still resizes. The handle is kept out of keyboard and pad navigation (NoNav), so a D-pad press never
				// lands on an invisible button.
				if (sv.movableWindow)
				{
					const ImVec2 afterRow = ImGui::GetCursorScreenPos();
					const ImVec2 wp = ImGui::GetWindowPos();
					const float border = ImGui::GetStyle().WindowBorderSize + 2.0f;
					const float bandH = ImGui::GetItemRectMax().y - wp.y - border;
					if (bandH > 1.0f)
					{
						ImGui::SetCursorScreenPos(ImVec2(wp.x + border, wp.y + border));
						ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
						g_noGameFrame = true;    // the move handle is invisible: no hover frame round it
						ImGui::InvisibleButton("##amf-move", ImVec2(std::max(1.0f, ImGui::GetWindowWidth() - border * 2.0f), bandH));
						g_noGameFrame = false;
						ImGui::PopItemFlag();
						static bool s_dragging = false;   // render thread; start/end of a drag, logged once each
						const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
						if (dragging)
						{
							const ImVec2 d = ImGui::GetIO().MouseDelta;
							const ImVec2 sz = ImGui::GetWindowSize();
							if (!nested)
							{
								// Kept whole on the screen, inside the edge band: the centre stays half a window plus the band
								// from every edge.
								s_hotCentre.x = std::clamp(s_hotCentre.x + d.x, edge + sz.x * 0.5f, std::max(edge + sz.x * 0.5f, display.x - edge - sz.x * 0.5f));
								s_hotCentre.y = std::clamp(s_hotCentre.y + d.y, edge + sz.y * 0.5f, std::max(edge + sz.y * 0.5f, display.y - edge - sz.y * 0.5f));
							}
							else
							{
								// The nested window (opened by DevBench's nested open) is not held at a centre: it moves itself.
								ImGui::SetWindowPos(ImVec2(std::clamp(wp.x + d.x, edge, std::max(edge, display.x - edge - sz.x)),
														   std::clamp(wp.y + d.y, edge, std::max(edge, display.y - edge - sz.y))));
							}
						}
						if (dragging != s_dragging)
						{
							s_dragging = dragging;
							logger::debug("window: top-row drag {} at ({:.0f}, {:.0f})", dragging ? "started" : "ended", wp.x, wp.y);
						}
						ImGui::SetCursorScreenPos(afterRow);
					}
				}
				ImGui::Separator();

				// Whether this theme wants the knotwork frame - captured once, applied to every
				// panel below and the outer window for a consistent framed look.
				// A supplied frame is drawn whatever the theme says: an author who ships frame art
				// has asked for a frame, and it replaces the knotwork rather than adding to it.
				const bool knot = theme::GetActiveTheme().knotwork || skin::HasFrame();

				const std::vector<registry::Entry> entries = registry::Snapshot();
				// THE SIDE PANE FITS ITS NAMES (the owner, 2026-10-02: "make it so that the names are always fully visible
				// ... by making the left pane auto adjust its width to fit the names of the menus"). It used to be a flat
				// 30% of the window, which clipped "ApocryphaRealm Lock Interaction Overhaul" to "ApocryphaR". Now it is the
				// widest name actually shown - a mod under a separator with its indent, a separator with its fold arrow and
				// count - plus the pinned-box gutter, the window padding and a scrollbar; never under the old 30%.
				float leftWidth = 0.0f;
				{
					const float avail = ImGui::GetContentRegionAvail().x;
					const ImGuiStyle& st = ImGui::GetStyle();
					const float gutter = ImGui::GetFontSize() * 0.55f + st.ItemInnerSpacing.x * 2.0f;
					float widest = ImGui::CalcTextSize(TR("AMF_Framework", "Framework")).x;
					for (const personalization::DisplayEntry& row : ShownOrder(entries))
					{
						float w = ImGui::CalcTextSize(row.displayName.c_str()).x;
						if (row.separator) { w += ImGui::GetFontSize() * 1.1f + ImGui::CalcTextSize("  (000)").x; }
						else if (row.depth > 0) { w += ImGui::GetFontSize() * 0.9f; }
						widest = std::max(widest, w);
					}
					const float needed = widest + gutter + st.WindowPadding.x * 2.0f + st.ScrollbarSize + st.ItemSpacing.x * 2.0f;
					// THE WINDOW GROWS RATHER THAN SQUEEZING THE PAGE (the owner, 2026-10-02, after the names pane took the
					// room: "we're gonna have to have the right pane automatically fit its mod menus by size as well ... now
					// you can't see hardly anything on the right side"). The page pane keeps at least 28 characters' width;
					// when the names and that minimum do not both fit, the window widens itself once (up to 98% of the screen;
					// it is centred, so it grows both ways) - from the names alone, so it does not change with the mod picked.
					const float rightMin = ImGui::GetFontSize() * 28.0f;
					const float between = ImGui::GetStyle().ItemSpacing.x + kKnotOutset * 4.0f;
					if (avail < needed + rightMin + between)
					{
						ImGuiWindow* self = ImGui::GetCurrentWindow();
						// At most the room inside the edge band (EdgeMargin), never into it.
						const float grown = std::min(self->Size.x + (needed + rightMin + between - avail), room.x);
						if (grown > self->Size.x + 0.5f)
						{
							ImGui::SetWindowSize(ImVec2(grown, self->Size.y));
							// A window that would now run into the band on the right grows leftwards instead (the System-row
							// window sits against the right of the screen; the key-opened one is re-centred next frame).
							if (self->Pos.x + grown > display.x - edge)
							{
								ImGui::SetWindowPos(ImVec2(std::max(edge, display.x - edge - grown), self->Pos.y));
								if (!nested) { s_hotCentre.x = std::max(edge, display.x - edge - grown) + grown * 0.5f; }
							}
							// once per mouse press, not every frame of a drag (247 lines in 2 s in the 1.0.4 run)
							static bool s_widenLogged = false;
							if (!s_widenLogged) {
								logger::debug("window: widened to {:.0f} px so the side list's names and the page both fit", grown);
							}
							s_widenLogged = ImGui::IsMouseDown(ImGuiMouseButton_Left);   // held: logged once for this press
						}
					}
					const float most = std::max(avail * 0.30f, avail - rightMin - between);
					leftWidth = std::clamp(needed, avail * 0.30f, most);
				}

				// SMF SHAPE (design decision, 2026-08-30): a one-for-one replacement of SKSE Menu Framework's
				// window - a SIDE LIST of the registered mods (plus the framework's own entries) and a
				// CONTENT pane for the selected mod's pages (tabs when it has several). No game-menu
				// tabs, no Save/Load/Quit: the game's own menus are not this framework's job.
				std::string sel;
				int selMod = 0;
				{
					std::scoped_lock l(g_selLock);
					sel = g_selNode; selMod = g_selMod;
					g_selExternal = false;
				}
				bool changed = false;  // set only by a real UI interaction this frame
				// CONSUMED HERE, ONCE, whatever happens below. It used to be cleared only inside the
				// branch that acts on it - which needs the selected entry to be DRAWN - so with any
				// text in the search box the selected mod was filtered out, the flag was never
				// cleared, and it sat armed until that row reappeared and stole the keyboard from
				// the search box (the owner, 2026-09-19: "the typing indicator just disappears").
				const bool navToSelected = g_navToSelected.exchange(false);
				if (navToSelected) { g_frameNavConsumed = ImGui::GetFrameCount(); }
				// (the registry snapshot is taken above, before the side pane's width is measured from it)
				if (selMod >= static_cast<int>(entries.size())) { selMod = 0; }

				// ---- SIDE LIST -------------------------------------------------------------------
				// PANE CROSSING (author playtest 2026-09-01: "neither the d-pad the left or the right stick
				// will let me go from the left to the right pane"). ImGuiWindowFlags_NavFlattened is
				// documented for children with NO scrolling; the content pane scrolls, and flattening it
				// gave asymmetric crossing - content->list worked, list->content never did. So nav stays
				// contained in each pane and the crossing is done explicitly below, which is also exactly
				// what the controller spec asks for.
				if (g_focusPane == 1) { ImGui::SetNextWindowFocus(); g_focusPane = 0; g_frameWindowFocus = ImGui::GetFrameCount(); }
				ImGui::BeginChild("##side", ImVec2(leftWidth, 0.0f), true);
				auto sideItem = [&](const char* label, const char* id) {
					const bool isOpen = (sel == id);
					if (isOpen && navToSelected)
					{
						ImGui::SetKeyboardFocusHere();  // the next item submitted takes the nav cursor
						g_frameFocusHere = ImGui::GetFrameCount();
					}
					if (ImGui::Selectable(label, isOpen)) { sel = id; changed = true; }
				};
				ImGui::TextDisabled("%s", TR("AMF_Framework", "Framework"));
				sideItem(TR("AMF_Settings", "Settings"), "settings");
				sideItem(TR("AMF_Controls", "Controls"), "controls");
				sideItem(TR("AMF_Help", "Help"),     "help");
				ImGui::Separator();
				ImGui::TextDisabled("%s", TR("AMF_Mods", "Mods"));

				// THE MODS ROW, as the Skyrim framework's 2.1.1 (the owner, 2026-10-05, on Witcher 3: "There's currently two
				// toggles for the mods row for A to Z and Z to A when it's supposed to be a tick box and one toggle, and there
				// should be a sort button next to it to sort all the mod menus"):
				//   tick box - alphabetical order on or off (off: the order the player arranged by hand);
				//   switch   - on A-Z, off Z-A; greyed while the tick box is off, and remembered for the next tick;
				//   Sort     - the mod-menu category sort, separators and all, as Settings > Mod menus > Sort into categories
				//              (where Undo is).
				// A tick box rather than a switch for the first, against rule 32, because the owner asked for one by name.
				// Favourites stay pinned at the top under any order.
				{
					const auto mode = personalization::GetSortMode();
					static bool s_ascending = mode != personalization::SortMode::kAlphaDesc;   // the direction while unticked
					bool alphabetical = mode != personalization::SortMode::kListOrder;
					if (mode == personalization::SortMode::kAlphaAsc) { s_ascending = true; }
					if (mode == personalization::SortMode::kAlphaDesc) { s_ascending = false; }
					const auto apply = [&]() {
						personalization::SetSortMode(!alphabetical ? personalization::SortMode::kListOrder
													 : (s_ascending ? personalization::SortMode::kAlphaAsc : personalization::SortMode::kAlphaDesc));
						settings::Save();
					};
					ImGui::SameLine();
					if (ImGui::Checkbox("##alphabetical", &alphabetical)) { apply(); }
					if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_SortAlphaTip", "Sort the list alphabetically. Off: the order you arranged by hand.")); }
					ImGui::SameLine();
					ImGui::BeginDisabled(!alphabetical);
					if (widgets::Toggle(s_ascending ? TR("AMF_SortAsc", "A-Z") : TR("AMF_SortDesc", "Z-A"), &s_ascending)) { apply(); }
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("%s", TR("AMF_SortDirTip", "On: A to Z. Off: Z to A.")); }
					ImGui::SameLine();
					static std::string s_sortStatus;
					if (ImGui::SmallButton(TR("AMF_SortButton", "Sort")))
					{
						s_sortStatus = modmenus::SortFromSideList();
					}
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s%s%s", TR("AMF_W3SortButtonTip", "Sort the mod menus into categories, each under a separator for its kind. Undo is on Settings > Mod menus."),
										  s_sortStatus.empty() ? "" : "\n\n", s_sortStatus.c_str());
					}
				}

				// Search the list by name. Once a load order registers thirty or more pages the
				// list is longer than the pane and finding one means scrolling; typing two or
				// three letters is faster than any amount of ordering.
				static char s_modFilter[64] = {};
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputTextWithHint("##modsearch", TR("AMF_SearchMods", "Search"),
										 s_modFilter, sizeof(s_modFilter));
				// The framework's OWN text boxes are drawn with ImGui directly, so the generator's
				// amf_NoteTextField hook (which only the C-API wrappers carry) never sees them; each one
				// notes itself, or the on-screen keyboard works on every mod's box except ours (the owner,
				// 2026-09-18: "the keyboard appears while in item explorer but not when using amfs own search bar").
				keyboard::NoteTextField(ImGui::GetItemID());
				g_frameSearchDrawn = ImGui::GetFrameCount();
				// Mirrored for the driving tool (report 2026-09-12: the box stops taking input after the
				// text is erased). Rect so the REAL box can be clicked; active/text/key state so the
				// failure is measured at the widget rather than guessed from a symptom.
				{
					const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
					g_searchRect[0].store(mn.x); g_searchRect[1].store(mn.y); g_searchRect[2].store(mx.x); g_searchRect[3].store(mx.y);
					g_searchActive.store(ImGui::IsItemActive());
					g_searchLen.store(static_cast<int>(std::strlen(s_modFilter)));
					{ std::scoped_lock l(g_searchTextLock); g_searchText = s_modFilter; }
					const ImGuiIO& sio = ImGui::GetIO();
					g_wantTextInput.store(sio.WantTextInput);
					g_backspaceDown.store(ImGui::IsKeyDown(ImGuiKey_Backspace));
					g_modCtrl.store(sio.KeyCtrl); g_modShift.store(sio.KeyShift); g_modAlt.store(sio.KeyAlt);
					g_activeIdMirror.store(GImGui ? GImGui->ActiveId : 0u);   // imgui_internal: which item holds keyboard input
				}

				const auto lower = [](std::string a_in) {
					std::transform(a_in.begin(), a_in.end(), a_in.begin(),
								   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					return a_in;
				};
				const std::string needle = lower(s_modFilter);

				// Player-facing order and names (menu-shell personalization). The rows carry the
				// REGISTRY index, so selection, the C API and DevBench addressing are unaffected.
				// Consumed ONCE for the frame, then applied to whichever row has the highlight. Taken
				// outside the loop so a single press cannot fire on several rows.
				const bool rowContextMenu = bindings::TakeTriggered(bindings::Action::kContextMenu);
				const bool rowFavourite = bindings::TakeTriggered(bindings::Action::kFavourite);
				// GRAB AND MOVE (the owner, 2026-10-02: "pressing right stick will select the mod and then going and
				// moving the stick up or down will move its position up or down. And this should be rebindable"). The
				// grab picks the highlighted mod up; the two moves walk it one place at a time (Nudge - the same step as
				// the Reorder arrows); the grab again, B, or the highlight leaving it puts it down.
				const bool rowGrab = bindings::TakeTriggered(bindings::Action::kGrabMod);
				int grabStep = 0;
				if (bindings::TakeTriggered(bindings::Action::kGrabUp)) { grabStep = -1; }
				if (bindings::TakeTriggered(bindings::Action::kGrabDown)) { grabStep = 1; }
				if (const int stickStep = GrabStickStep(); grabStep == 0) { grabStep = stickStep; }
				bool grabbedFocused = false;

				int shown = 0;
				// An entry whose every page is hidden (AMF_SetPageVisible) draws no row (Skyrim 2.1.0). It KEEPS its place in the
				// saved order and under its separator - only the drawing skips it, so it returns to the same spot when a page is
				// shown again - and a separator's "(n)" counts only the rows it actually shows; a separator whose mods are all
				// hidden still draws, as an empty one does. ShownOrder is shared with the state JSON's displayOrder.
				const std::vector<personalization::DisplayEntry> displayRows = ShownOrder(entries);
				for (const personalization::DisplayEntry& row : displayRows)
				{
					// The name the player actually reads is what they will type at, so the filter
					// matches the DISPLAY name - an aliased entry is findable by its alias. While searching, the list is
					// flat: separator rows step aside and a match inside a folded group is shown all the same.
					if (!needle.empty() && (row.separator || lower(row.displayName).find(needle) == std::string::npos))
					{
						continue;
					}
					// a folded separator hides its mods (MO2's collapse - the owner, 2026-10-02)
					if (needle.empty() && row.hidden) { continue; }

					if (row.separator)
					{
						DrawSeparatorRow(entries, row, rowContextMenu, rowFavourite);
						continue;
					}
					++shown;

					const bool isOpen = (sel == "mod" && selMod == row.registryIndex);
					if (isOpen && navToSelected)
					{
						ImGui::SetKeyboardFocusHere();
						g_frameFocusHere = ImGui::GetFrameCount();
					}

					ImGui::PushID(row.modName.c_str());

					// A FILLED WHITE BOX to the left of a favourited menu's name (the owner,
					// 2026-09-19), in a gutter every row reserves so the names stay in one column
					// whether or not they are pinned.
					const bool favourite = personalization::IsFavourite(row.modName);
					const float boxSide = ImGui::GetFontSize() * 0.55f;
					const float gutter = boxSide + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
					const ImVec2 rowTopLeft = ImGui::GetCursorScreenPos();

					// a mod under a separator sits one step in, so the group reads as a group
					const float rowIndent = gutter + (needle.empty() && row.depth > 0 ? ImGui::GetFontSize() * 0.9f : 0.0f);
					ImGui::Indent(rowIndent);
					const bool picked = ImGui::Selectable(row.displayName.c_str(), isOpen);
					ImGui::Unindent(rowIndent);

					// the picked-up mod is boxed, so it reads as held rather than merely highlighted
					if (!g_grabbedMod.empty() && g_grabbedMod == row.modName)
					{
						const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
						ImDrawList* dl = ImGui::GetWindowDrawList();
						dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 40));
						dl->AddRect(mn, mx, ImGui::GetColorU32(ImGuiCol_Text), 0.0f, 0, 2.0f);
					}

					if (favourite)
					{
						const float top = rowTopLeft.y + (ImGui::GetTextLineHeight() - boxSide) * 0.5f;
						const float left = rowTopLeft.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
						ImGui::GetWindowDrawList()->AddRectFilled(
							ImVec2(left, top), ImVec2(left + boxSide, top + boxSide),
							IM_COL32(255, 255, 255, 255));
					}

					// Y opens the row's menu and nothing else (see DrawSeparatorRow): its "input" activation is not a pick
					if (picked && !(rowContextMenu && ImGui::IsItemFocused()))
					{
						sel = "mod";
						selMod = row.registryIndex;
						changed = true;
					}

					// Y IS THE RIGHT-CLICK (the owner, 2026-09-19: "y should do the same as the right
					// click"), and from 1.9.6 it is an ordinary bindable action rather than a
					// hard-wired key, so it can be moved from the Controls page like everything else.
					// Favouriting the highlighted mod is a second action, for players who would
					// rather not go through the menu at all.
					if (ImGui::IsItemFocused())
					{
						if (rowContextMenu)
						{
							ImGui::OpenPopup("##modctx");
							// opened from the controller (Y): beside the highlighted row, not wherever the mouse was left
							ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x + ImGui::GetFontSize(), ImGui::GetItemRectMax().y),
								ImGuiCond_Appearing);
						}
						if (rowFavourite)
						{
							personalization::ToggleFavourite(row.modName);
							settings::Save();
						}
						if (rowGrab)
						{
							if (g_grabbedMod == row.modName)
							{
								g_grabbedMod.clear();
								logger::info("menu order: put \"{}\" down", row.modName);
							}
							else
							{
								g_grabbedMod = row.modName;
								logger::info("menu order: picked \"{}\" up", row.modName);
							}
						}
						if (g_grabbedMod == row.modName) { grabbedFocused = true; }
					}

					// RIGHT-CLICK: favourite/unfavourite, rename, and the two moves that a pinned
					// list makes obvious. Rename hands off to the modal below, so the text field is
					// drawn once rather than once per row.
					// A TIGHT BOX (the owner, 2026-09-21: "fix the empty space ... make the outer bounds of the box smaller so
					// it fits around the 3 options"). The theme pads every window by the knotwork corner + 8 px so the
					// frame art has room; a context menu carries no frame art, so that padding was an empty band around
					// three short items. The popup reads WindowPadding at Begin, so it is pushed just for that call.
					const float ctxPad = ImGui::GetFontSize() * 0.35f;
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
					const bool ctxOpen = ImGui::BeginPopupContextItem("##modctx");
					ImGui::PopStyleVar();
					if (ctxOpen)
					{
						if (ImGui::MenuItem(favourite ? TR("AMF_Unfavourite", "Remove from favourites")
													  : TR("AMF_Favourite", "Add to favourites")))
						{
							personalization::ToggleFavourite(row.modName);
							settings::Save();
						}
						if (ImGui::MenuItem(TR("AMF_Rename", "Rename...")))
						{
							g_renameTarget = row.modName;
							const std::string alias = personalization::GetAlias(row.modName);
							std::snprintf(g_renameBuffer, sizeof(g_renameBuffer), "%s", alias.c_str());
							g_renameOpenPending = true;
						}
						ImGui::Separator();
						// the top of ITS OWN group, not of the list - pinning is what puts a mod at the very top (the owner,
						// 2026-10-02: "That way it's distinct from favoriting")
						if (ImGui::MenuItem(TR("AMF_MoveToTop", "Move to the top")))
						{
							personalization::MoveToGroupTop(entries, row.modName);
							settings::Save();
						}
						// REORDER (the owner, 2026-10-02): a little window to the right with an up and a down arrow, one place per
						// press. Arrow BUTTONS, not menu items, so the window stays open and the mod can be walked several
						// places in a row; the Menu list page's numbers follow (they read the same order).
						// the submenus get the context menu's tight padding too - the theme's frame padding left an empty band
						// round the two arrows and the separator names (seen in the 2.0.0 release captures)
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
						const bool reorderOpen = ImGui::BeginMenu(TR("AMF_Reorder", "Reorder"));
						ImGui::PopStyleVar();
						if (reorderOpen)
						{
							if (ImGui::ArrowButton("##nudgeup", ImGuiDir_Up))
							{
								if (personalization::Nudge(entries, row.modName, -1)) { settings::Save(); }
							}
							if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_MoveUp", "Move up one place")); }
							ImGui::SameLine();
							if (ImGui::ArrowButton("##nudgedown", ImGuiDir_Down))
							{
								if (personalization::Nudge(entries, row.modName, 1)) { settings::Save(); }
							}
							if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_MoveDown", "Move down one place")); }
							ImGui::EndMenu();
						}
						// SEPARATORS (the owner, 2026-10-02): make one above this mod, or send this mod into one.
						if (ImGui::MenuItem(TR("AMF_NewSeparatorAbove", "New separator above")))
						{
							BeginNewSeparator(entries, row.modName);
						}
						const auto separators = personalization::Separators();
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
						const bool sendToOpen = ImGui::BeginMenu(TR("AMF_SendTo", "Send to"), !separators.empty() || row.depth > 0);
						ImGui::PopStyleVar();
						if (sendToOpen)
						{
							for (const auto& sep : separators)
							{
								ImGui::PushID(sep.id.c_str());
								if (ImGui::MenuItem(sep.name.c_str()))
								{
									personalization::SendTo(entries, row.modName, sep.id);
									settings::Save();
								}
								ImGui::PopID();
							}
							if (row.depth > 0)
							{
								ImGui::Separator();
								if (ImGui::MenuItem(TR("AMF_SendToNone", "No separator")))
								{
									personalization::SendTo(entries, row.modName, std::string());
									settings::Save();
								}
							}
							ImGui::EndMenu();
						}
						ImGui::EndPopup();
					}

					ImGui::PopID();
				}
				if (!g_grabbedMod.empty())
				{
					if (!grabbedFocused)
					{
						logger::info("menu order: put \"{}\" down (the highlight left it)", g_grabbedMod);
						g_grabbedMod.clear();
					}
					else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
					{
						logger::info("menu order: put \"{}\" down (B)", g_grabbedMod);
						g_grabbedMod.clear();
					}
					else if (grabStep != 0 && personalization::Nudge(entries, g_grabbedMod, grabStep))
					{
						settings::Save();
						logger::info("menu order: \"{}\" stepped {}", g_grabbedMod, grabStep < 0 ? "up" : "down");
					}
				}
				if (entries.empty()) { ImGui::TextDisabled("%s", TR("AMF_NoneRegistered", "none registered")); }
				else if (shown == 0) { ImGui::TextDisabled("%s", TR("AMF_NoMatch", "no mod matches that")); }
				const bool sideHasNav = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
				ImGui::EndChild();
				// Captured BEFORE the rename popup below: the knotwork is drawn around the side
				// PANE, and a popup submitted in between would leave GetItemRect* describing the
				// popup instead (the frame would jump to wherever the modal sat).
				const ImVec2 sidePaneMin = ImGui::GetItemRectMin();
				const ImVec2 sidePaneMax = ImGui::GetItemRectMax();

				// The rename modal the right-click menu asks for. Opened and drawn OUT HERE, at the
				// window's own id level, so it is one popup rather than one per row.
				if (g_renameOpenPending)
				{
					ImGui::OpenPopup("##amf_rename");
					g_renameOpenPending = false;
				}
				// No title bar (Skyrim 2.1.1): the id has no visible title, so the bar was an empty strip above the box.
				if (ImGui::BeginPopupModal("##amf_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
				{
					const bool renamingSeparator = personalization::IsSeparator(g_renameTarget);
					ImGui::TextUnformatted(renamingSeparator ? TR("AMF_SeparatorNameTitle", "Name this separator")
															 : TR("AMF_RenameTitle", "Show this menu as"));
					if (!renamingSeparator) { ImGui::TextDisabled("%s", g_renameTarget.c_str()); }
					ImGui::Spacing();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
					if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
					const bool entered = ImGui::InputTextWithHint("##renamefield",
																  renamingSeparator ? TR("AMF_SeparatorDefaultName", "New separator") : g_renameTarget.c_str(),
																  g_renameBuffer, sizeof(g_renameBuffer),
																  ImGuiInputTextFlags_EnterReturnsTrue);
					keyboard::NoteTextField(ImGui::GetItemID());
					ImGui::TextDisabled("%s", renamingSeparator ? TR("AMF_SeparatorNameHint", "A separator groups the menus below it, up to the next one.")
																: TR("AMF_RenameHint", "Leave it empty to go back to the mod's own name."));
					ImGui::Spacing();
					const bool ok = ImGui::Button(TR("AMF_RenameOk", "Rename")) || entered;
					ImGui::SameLine();
					const bool cancel = ImGui::Button(TR("AMF_RenameCancel", "Cancel"));
					if (ok && !(renamingSeparator && g_renameBuffer[0] == '\0'))   // a separator keeps a name
					{
						personalization::SetAlias(g_renameTarget, g_renameBuffer);
						settings::Save();
					}
					if (ok || cancel)
					{
						g_renameTarget.clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}

				if (knot)
				{
					DrawKnotworkAround(ImGui::GetWindowDrawList(), sidePaneMin, sidePaneMax);
				}

				// Room between the panes for both knotwork frames plus a breath of air.
				ImGui::SameLine(0.0f, knot ? kKnotOutset * 4.0f : -1.0f);

				// ---- CONTENT PANE -------------------------------------------------------------
				const bool enterContent = g_focusPane == 2;
				if (enterContent) { ImGui::SetNextWindowFocus(); g_focusPane = 0; g_frameWindowFocus = ImGui::GetFrameCount(); }
				ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), true);
				ImGuiWindow* const contentPane = ImGui::GetCurrentWindow();
				// Before any item of the page is submitted, so ImGui's init picks this frame's first control (StartContentNav).
				if (enterContent || g_contentNavReset)
				{
					StartContentNav(contentPane, enterContent ? "entered" : "the page under the highlight changed");
					g_contentNavReset = false;
				}
				// Re-measured every frame. A pane with no tab bar leaves these at zero, so left
				// falls straight back to the mod list exactly as it always did.
				g_tabCount = 0;
				g_tabIndex = 0;
				g_tabName.clear();
				g_tabBarHasNav = false;
				// A page re-declares its inner tabs every frame it draws; stale numbers must not steer nav.
				g_innerFresh = false;
				std::string curTabName;
				if (sel == "settings")      { DrawFrameworkSettingsPane(); }
				else if (sel == "controls") { DrawControlsPane(); }
				else if (sel == "help")     { DrawHelpPane(); }
				else if (sel == "mod" && !entries.empty())
				{
					const registry::Entry& entry = entries[selMod];
					{
						const std::string alias = personalization::GetAlias(entry.modName);
						ImGui::TextUnformatted(alias.empty() ? entry.modName.c_str() : alias.c_str());
					}
					ImGui::Separator();
					// Pages a mod hid with AMF_SetPageVisible (1.8.3) are left out; the rest keep their order.
					std::vector<const registry::Page*> visiblePages;
					for (const registry::Page& page : entry.pages)
					{
						if (!page.hidden) { visiblePages.push_back(&page); }
					}
					if (visiblePages.size() == 1)
					{
						visiblePages[0]->render();
					}
					else if (visiblePages.size() > 1 && ImGui::BeginTabBar("##pages", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))   // a mod with many sections keeps whole labels: the bar scrolls, and the list button on the left opens every section by name (Character Progression Control reached twelve tabs and the default policy squeezed them to "Level... Expe... Skills")
					{
						int index = 0;
						for (const registry::Page* pagePtr : visiblePages)
						{
							const registry::Page& page = *pagePtr;
							// A D-pad step asks for its tab for exactly ONE frame. Every other
							// frame the bar owns its own selection, so the D-pad, a mouse click
							// and the tab-list popup never fight over which tab is open.
							const ImGuiTabItemFlags flags =
								(index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
							const bool open = BeginPageTab(page.pageName.c_str(), flags);
							// Asked of the tab itself rather than worked out from where nav "should"
							// be (rule 30): the item just submitted is the tab button, selected or not.
							if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
							if (open)
							{
								g_tabIndex = index;
								curTabName = page.pageName;
								page.render();
								ImGui::EndTabItem();
							}
							++index;
						}
						g_tabCount = index;
						g_tabRequest = -1;
						ImGui::EndTabBar();
					}
				}
				else { DrawFrameworkSettingsPane(); }
				// The framework's own panes (Settings, Controls, Help) report their open tab too (W3 1.0.4: amf.menu op=state's
				// "page" was "" on the Settings tabs while it named a mod's page). Their tab lambdas set g_tabName.
				if (curTabName.empty() && !g_tabName.empty())
				{
					curTabName = g_tabName;
				}
				{
					static std::string s_loggedTab;   // render thread; logged when the open tab changes
					if (curTabName != s_loggedTab)
					{
						s_loggedTab = curTabName;
						logger::debug("state: page '{}' (tab {} of {}) on '{}'", curTabName, g_tabIndex + 1, g_tabCount, sel);
					}
				}
				if (!g_innerFresh) { g_innerCount = 0; g_innerIndex = 0; }
				// The page in this pane changed (a bumper, Page Up / Down, a mod's own inner tab) while the highlight was in
				// the pane, and its item is no longer drawn: ImGui would score the next press from the old page's rect, so
				// the highlight is restarted next frame. A tab chosen ON the bar keeps the highlight - the tab is still drawn.
				{
					static std::string s_lastPage;   // render thread
					const std::string page = sel + "|" + std::to_string(sel == "mod" ? selMod : -1) + "|" + std::to_string(g_tabIndex) +
											 "|" + (g_innerFresh ? std::to_string(g_innerIndex) : std::string("-"));
					if (page != s_lastPage)
					{
						if (!s_lastPage.empty() && GImGui && GImGui->NavWindow == contentPane && GImGui->NavId != 0 && !GImGui->NavIdIsAlive)
						{
							g_contentNavReset = true;
							logger::debug("nav: page {} -> {} under the highlight; restarting it next frame", s_lastPage, page);
						}
						s_lastPage = page;
					}
				}
				const bool contentHasNav = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
				ImGui::EndChild();
				if (knot)
				{
					DrawKnotworkAround(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				}

				// Draw the OUTER window's knotwork frame LAST, on the window's own draw list so it
				// sits on top of the content and exactly over ImGui's border, at the window rect.
				// The transparent centre keeps the panes fully visible.
				if (knot)
				{
					const ImVec2 wp = ImGui::GetWindowPos();
					const ImVec2 ws = ImGui::GetWindowSize();
					DrawKnotworkFrame(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
				}
				if (changed)
				{
					std::scoped_lock l(g_selLock);
					g_selNode = sel; g_selMod = selMod;
				}
				// Right out of the list, left back into it. Only when nothing is being edited, so
				// pushing left inside a slider adjusts the value instead of leaving the pane. Both
				// sticks and the D-pad and the arrow keys all count as the same "move across".
				{
					const bool wantsRight = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
											ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, false) ||
											ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
					const bool wantsLeft = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) ||
										   ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, false) ||
										   ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false);
					const bool editing = ImGui::IsAnyItemActive();
					// A driving tool's press is read here, alongside the real ones, so amf.menu
					// op=nav proves THIS decision rather than a private path around it.
					const int  driven   = g_navRequest.exchange(0);
					const bool navLeft  = wantsLeft  || driven == 1;
					const bool navRight = wantsRight || driven == 2;
					if (sideHasNav && navRight && !editing)
					{
						g_focusPane = 2;
						logger::debug("nav: list -> options");
					}
					// Inside the content pane a sideways press is ImGui's first: if there is a widget to that
					// side, the cursor moves there and nothing else happens. A press that moved nothing never
					// steps a tab (1.9.0 - tabs change only by selecting and activating them); a left press
					// that moved nothing leaves for the mod list. The
					// decision is taken one frame late, when ImGui has reported the move (NavJustMovedToId),
					// so the two never race. A driven op=nav press moves no ImGui cursor and therefore always
					// steps, which keeps the driving tool's proof of this path intact.
					else if (contentHasNav && (navRight || navLeft) && !editing && g_pendingTabStep == 0)
					{
						g_pendingTabStep = navRight ? 1 : -1;
						logger::debug("nav: sideways press noted ({}), deciding next frame", navRight ? "right" : "left");
					}
					else if (g_pendingTabStep != 0)
					{
						const int step = g_pendingTabStep;
						g_pendingTabStep = 0;
						const bool imguiMoved = GImGui && GImGui->NavJustMovedToId != 0;
						if (imguiMoved)
						{
							logger::debug("nav: ImGui moved to a widget; no tab step");
						}
						else if (!contentHasNav || editing)
						{
							logger::debug("nav: content lost focus before the step was decided; dropped");
						}
						// 1.9.0: NO tab stepping here any more (the owner, 2026-09-18: "i want the only way for the
						// dpad to switch tabs in our mods is to select said tab and activate it, im tired of switching
						// tabs by accident"). A tab - the framework's page tabs and a page's own inner bar alike - changes
						// only when the highlight is moved ONTO the tab and it is activated, which ImGui's own nav does.
						// A right press that moved nothing now does nothing; a left press that moved nothing leaves for
						// the mod list, whatever tab is open.
						else if (step < 0)
						{
							// Nothing to move to on the left: back to the list (from any tab).
							g_focusPane = 1;
							g_navToSelected = true;  // land on the open entry, not the last cursor position
							logger::debug("nav: options -> list (returning to the open entry)");
						}
					}
					// Stepping tabs must NOT depend on where ImGui's nav focus happens to be.
					//
					// This branch used to require g_tabBarHasNav - the cursor sitting literally on the tab bar - and in
					// practice it almost never is: opening a mod leaves focus in the page below the bar, so right never
					// advanced the tab and the index stayed at 0. Left then had nothing to step back through and fell
					// straight to its last branch, dropping the player out to the mod list. That is the whole of the
					// reported fault (the owner, 2026-09-16: "d-pad left, making it go all the way back to the left pane
					// instead of just scrolling the tabs ... it should only go back to the far left pane when you're
					// already done scrolling left and there's nothing left to scroll") - left was never the broken half.
					//
					// Measured, not guessed: amf.menu op=state reports page/pageIndex/pageCount, and two op=nav dir=right
					// presses on Wheeler left it at idx=0 of 3.
					//
					// !editing still guards it, so pushing right inside a slider adjusts the value rather than changing tab.

				}

				// Publish the tab bar for the DevBench state JSON, so a driving tool can assert
				// which section is open without reading pixels.
				{
					std::scoped_lock l(g_selLock);
					g_selTabName = curTabName; g_selTabIndex = g_tabIndex; g_selTabCount = g_tabCount;
				}

				// THE BUMPERS WALK THE TABS (the owner, 2026-09-19: "bumpers navigate tabs, dpad
				// doesnt"). The D-pad deliberately never steps a tab - moving the highlight onto one
				// and activating it is the other way, and the standing rule forbids stepping - so tab
				// navigation gets controls of its own. The INNERMOST bar that exists takes the press:
				// a mod's own tab bar if it drew one this frame, otherwise the framework's page bar.
				{
					const int step = (bindings::TakeTriggered(bindings::Action::kTabNext) ? 1 : 0) -
									 (bindings::TakeTriggered(bindings::Action::kTabPrev) ? 1 : 0);
					if (step != 0)
					{
						if (g_innerFresh && g_innerCount > 1)
						{
							g_innerRequest = (g_innerIndex + step + g_innerCount) % g_innerCount;
							logger::debug("nav: bumper -> inner tab {}", g_innerRequest);
						}
						else if (g_tabCount > 1)
						{
							g_tabRequest = (g_tabIndex + step + g_tabCount) % g_tabCount;
							logger::debug("nav: bumper -> tab {}", g_tabRequest);
						}
					}
				}

				// Controller scheme: while a slider/drag is ACTIVE the right stick moves it and the
				// left stick is held off, so navigation and adjustment never fight. Sampled here,
				// inside the frame, and read by the input thread's translation step.
				input::SetItemActive(ImGui::IsAnyItemActive());
			}
			ImGui::End();
		}

		// -----------------------------------------------------------------------------------
		// Every frame - called by the overlay from its Present hook, before the game presents.
		// Everything is drawn and ImGui::Render() has run when this returns; the overlay records
		// the draw data on its own command list.
		// -----------------------------------------------------------------------------------
		void Frame()
		{
			if (!g_d3dReady.load(std::memory_order_acquire))
			{
				return;
			}

			// A font or text-size change rebuilds the atlas. This MUST happen before
			// ImGui_ImplDX12_NewFrame (gfx::NewFrame): that call is where the backend recreates its
			// device objects (font texture included) when they are missing. The old order -
			// invalidating AFTER the backend NewFrame had already run - destroyed the font
			// texture with nothing left in the frame to recreate it, so the frame rendered
			// its draw data against a dead texture and crashed the moment the font or the
			// text-size slider changed (author playtest, 2026-08-31).
			if (g_fontRebuildPending.exchange(false))
			{
				gfx::InvalidateDeviceObjects();
				BuildFonts();
			}

			// While nothing on screen has the input the OS cursor is the game's: the Win32 backend must not set it (it would
			// put an arrow over the game on the frame the menu closes). Our menu, or a mod's window that holds the input
			// (last frame's reading), lets ImGui set it.
			{
				ImGuiIO& cio = ImGui::GetIO();
				if (g_windowVisible.load(std::memory_order_acquire) || g_consumerInput.load(std::memory_order_acquire)) { cio.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange; }
				else { cio.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange; }
			}

			gfx::NewFrame();
			ImGui_ImplWin32_NewFrame();

			// THE IMAGE, NOT THE WINDOW (Skyrim 2.0.8 - Soulsthat, 2026-10-04: the menu and its tooltips clipped at the right
			// and bottom when the game drew a smaller image than its window). The Win32 backend sizes the display from the
			// game WINDOW's client rect, but everything ImGui draws lands 1:1 on the swap chain's back buffer. On The Witcher 3
			// the overlay draws on the game's own DX12 swap chain (Overlay.cpp), and the game is DPI-unaware: Windows hands it
			// a scaled client rect (2133x1200 on a 3200x1800 desktop) and it creates its swap chain at that same size, so
			// the two agree and this changes nothing. It is here for the case where they do not - an image drawn smaller
			// than the window and stretched to it - and then the image's size is used, the log says so once per pair, and the
			// OS cursor's client position (WM_MOUSEMOVE) is scaled into the image's pixels (input::ProcessQueuedEvents reads
			// WindowToImageScale). A minimised or background window (client 0x0) is left as it was, so the 0x0 guards on
			// the saved window geometry still apply.
			{
				unsigned imageW = 0, imageH = 0;
				gfx::GetBackBufferSize(imageW, imageH);
				ImGuiIO& dio = ImGui::GetIO();
				const ImVec2 window = dio.DisplaySize;
				float sx = 1.0f, sy = 1.0f;
				if (imageW > 0 && imageH > 0 && window.x >= 1.0f && window.y >= 1.0f)
				{
					const ImVec2 image(static_cast<float>(imageW), static_cast<float>(imageH));
					if (window.x != image.x || window.y != image.y)
					{
						static ImVec2 s_logged{ -1.0f, -1.0f };   // render thread only; log each new pair once
						if (s_logged.x != window.x || s_logged.y != window.y)
						{
							s_logged = window;
							logger::info("display: the game window is {}x{} but draws a {}x{} image - the menu uses the image's size, "
										 "and the cursor is scaled by {:.3f} x {:.3f}", window.x, window.y, image.x, image.y,
										 image.x / window.x, image.y / window.y);
						}
						sx = image.x / window.x;
						sy = image.y / window.y;
						dio.DisplaySize = image;
					}
				}
				g_imageScaleX.store(sx, std::memory_order_relaxed);
				g_imageScaleY.store(sy, std::memory_order_relaxed);
			}

			// Give an external launcher its say before this frame's visibility is read, so a
			// menu it just asked for opens on the same frame rather than the next one.
			compat::PumpExternalWindow();

			const bool visible = g_windowVisible.load(std::memory_order_acquire);

			// TWO STATES, NOT ONE (Skyrim 2.0.4). `visible` is OUR menu: it alone takes Escape as "close", reads the pad,
			// raises the menu's own commands and draws the framework window. `interactive` is "someone on screen has the
			// player's keyboard and mouse" - our menu, or a mod's own window that is open and blocking - and it is what
			// feeds ImGui, draws the cursor and holds the game's keys and mouse. A mod's window never pauses the game and
			// never takes the pad (the pad gate is our menu's alone). The gate is the flags AND what the window really is
			// (consumer::AnyWindowOwnsInput): open, BlockUserInput, and a window it drew last frame that takes the mouse -
			// the flags alone would let a passive always-on overlay take the whole game's input during play.
			const bool consumerOwnsInput = consumer::AnyWindowOwnsInput();
			const bool interactive = visible || consumerOwnsInput;
			{
				static bool s_lastConsumer = false;   // render thread only; transition log
				if (consumerOwnsInput != s_lastConsumer)
				{
					s_lastConsumer = consumerOwnsInput;
					logger::info("input: a mod's window{} {} the keyboard and mouse (framework menu {}){}",
								 consumerOwnsInput ? " \"" + consumer::InputOwnerName() + "\"" : std::string(),
								 consumerOwnsInput ? "took" : "handed back",
								 visible ? "open" : "closed",
								 consumerOwnsInput ? " - cursor shown, game keys and mouse held; the game is not paused" : "");
				}
			}

			SyncGamePause(visible && settings::Get().pauseGameWhileOpen);

			// The controller, read once per frame (Oblivion has no engine input-event stream to hook the
			// way Skyrim's PollInputDevices was; XInput is polled directly - see Input.cpp).
			input::PollGamepad();

			// Open-transition work happens HERE, not in ToggleMainWindow - the toggle is
			// flipped on the input thread, and cursor centring touches ImGui state.
			// It runs on the rising edge of INPUT OWNERSHIP (Skyrim 2.0.4), so a mod's window that takes the input gets the
			// same cursor and clean key state as our menu. Our menu opening over a mod's window that already has the input
			// is NOT a new edge: ImGui has been fed all along, so nothing is stale and the cursor stays where the player has it.
			{
				static bool s_wasInteractive = false;   // render thread only
				const bool justOpened = g_justOpened.exchange(false, std::memory_order_acq_rel);
				if (interactive && (!s_wasInteractive || (justOpened && !consumerOwnsInput)))
				{
					input::OnMenuOpened();
				}
				s_wasInteractive = interactive;
				if (justOpened)
				{
					// Navigation starts ON the open entry of the list (the owner, 2026-09-26: "I was able to use the D-pad
					// after I selected the box, the nav box for the menu"). With nothing placed, the first D-pad press
					// landed on the list pane itself and only a second action got inside it.
					g_focusPane = 1;
					g_navToSelected = true;
				}
			}
			// Published only AFTER the rising edge has cleared the queue, so nothing the input thread queues for a mod's
			// window can be thrown away as stale by the edge that let it in.
			g_consumerInput.store(consumerOwnsInput, std::memory_order_release);
			// The pad poll frees the cursor while our menu is up; a mod's window holding the mouse needs it free too.
			if (consumerOwnsInput && !visible) { ::ClipCursor(nullptr); }

			// Translation runs after the backends' NewFrame (so our queued io.Add*Event
			// calls land after, and therefore win over, the Win32 backend's own
			// GetCursorPos-based mouse update) and before ImGui::NewFrame consumes them.
			if (interactive)
			{
				input::ProcessQueuedEvents();
			}

			ImGuiIO& io = ImGui::GetIO();

			// Software cursor while the menu (or a mod's window holding the input) is up - the game hides and recentres
			// the OS cursor at will, so ImGui draws its own at the position we integrate.
			io.MouseDrawCursor = interactive;

			// Nav mode follows the EXPLICIT setting live (the toggle sits on the settings
			// page itself). Never auto-detected - that is the nav-focus-drift bug.
			if (input::UsingController())
			{
				io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableGamepad) & ~ImGuiConfigFlags_NavEnableKeyboard;
				// REQUIRED for gamepad nav to respond at all: ImGui only processes the
				// GamepadFace*/GamepadDpad* key events we feed when the backend advertises a
				// gamepad. Without this flag NavEnableGamepad is inert - which is why toggling
				// controller mode and pressing every button did nothing (design decision, 2026-08-28).
				io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
			}
			else
			{
				io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard) & ~ImGuiConfigFlags_NavEnableGamepad;
				io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
			}

			// Does a text field hold the keyboard? Sampled here, every frame, because the input
			// hook on the game thread turns the ENGINE's text entry on and off from it: Skyrim only
			// turns WM_CHAR into a CharEvent while ControlMap's text-entry count is up, and a
			// CharEvent is the only way a letter ever reaches ImGui in this framework (there is no
			// WndProc hook). Without it the search bar and every mod's text box took clicks and
			// navigation but not a single character (phbd01, 2026-09-19).
			// A mod's own window counts too (Skyrim 2.0.4): its text boxes type through here.
			g_wantTextInput.store(interactive && io.WantTextInput, std::memory_order_release);

			// WHY THE KEYBOARD WAS LOST (1.9.5). ImGui drops ActiveId by itself when the item
			// that holds it is NOT SUBMITTED in a frame - ActiveIdIsAlive stops matching
			// ActiveId and NewFrame clears it. That is a different fault from something calling
			// SetKeyboardFocusHere or focusing another window, and from the player clicking
			// elsewhere, and the three are indistinguishable on screen. So the report names
			// which of them it was, with each suspect stamped on the frame it actually fired.
			{
				static ImGuiID s_lastActive = 0;
				static int s_lastAliveFrame = -1;
				// Which window owned the field while it was alive, and which one holds nav now.
				// A consumer mod draws its own windows every frame through the framework, and one
				// of them taking focus would look exactly like this from the player's side.
				static char s_ownerWindow[64] = "";
				const int frame = ImGui::GetFrameCount();
				const ImGuiID nowActive = GImGui ? GImGui->ActiveId : 0u;
				if (s_lastActive != 0 && nowActive != s_lastActive && keyboard::IsTextField(s_lastActive))
				{
					const ImGuiIO& dio = ImGui::GetIO();
					logger::info("input: text field {} lost the keyboard on frame {} -> active now {} | "
								 "searchDrawnFrame={} (age {}), focusHereFrame={} (age {}), "
								 "windowFocusFrame={} (age {}), navConsumedFrame={} (age {}) | "
								 "mouseClicked={} mouseDown={} mousePos=({:.0f},{:.0f}) | "
								 "navId={} hoveredWindowMatters={}",
								 s_lastActive, frame, nowActive,
								 g_frameSearchDrawn, frame - g_frameSearchDrawn,
								 g_frameFocusHere, frame - g_frameFocusHere,
								 g_frameWindowFocus, frame - g_frameWindowFocus,
								 g_frameNavConsumed, frame - g_frameNavConsumed,
								 dio.MouseClicked[0], dio.MouseDown[0],
								 dio.MousePos.x, dio.MousePos.y,
								 GImGui ? GImGui->NavId : 0u,
								 s_lastAliveFrame);
					// EVERY key ImGui saw on the frame it died. An InputText deactivates itself on
					// Enter, on Escape, on Tab and on a nav CANCEL (B on a pad) - and from the
					// player's side all of those look like the box simply going dead. Listing the
					// keys is the only way to tell which, and whether the press was even real.
					{
						std::string keys;
						for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
						{
							const auto key = static_cast<ImGuiKey>(k);
							if (ImGui::IsKeyPressed(key, false)) { keys += std::string(ImGui::GetKeyName(key)) + "(p) "; }
							else if (ImGui::IsKeyDown(key)) { keys += std::string(ImGui::GetKeyName(key)) + "(d) "; }
						}
						if (keys.empty()) { keys = "(none)"; }
						logger::info("input:   keys this frame: {} | navActive={} navActivateId={} navJustMovedTo={} wantCaptureKeyboard={}",
									 keys, ImGui::GetIO().NavActive,
									 GImGui ? GImGui->NavActivateId : 0u,
									 GImGui ? GImGui->NavJustMovedToId : 0u,
									 ImGui::GetIO().WantCaptureKeyboard);
					}
					logger::info("input:   THE FIELD WAS {} on the frame it died - so this is {}",
								 keyboard::WasSubmittedLastFrame(s_lastActive) ? "STILL DRAWN" : "NOT DRAWN",
								 keyboard::WasSubmittedLastFrame(s_lastActive)
									 ? "something taking the focus, not the widget disappearing"
									 : "the widget not being submitted - its page stopped drawing it");
					logger::info("input:   owner window was \"{}\"; nav window now \"{}\"; hovered \"{}\"",
								 s_ownerWindow,
								 (GImGui && GImGui->NavWindow) ? GImGui->NavWindow->Name : "(none)",
								 (GImGui && GImGui->HoveredWindow) ? GImGui->HoveredWindow->Name : "(none)");
				}
				s_lastActive = nowActive;
				if (GImGui && GImGui->ActiveId != 0 && GImGui->ActiveIdWindow)
				{
					std::snprintf(s_ownerWindow, sizeof(s_ownerWindow), "%s", GImGui->ActiveIdWindow->Name);
				}
				s_lastAliveFrame = (GImGui && GImGui->ActiveIdIsAlive == GImGui->ActiveId) ? frame : s_lastAliveFrame;
			}

			// B / CIRCLE LETS GO OF A TEXT BOX (the owner, 2026-09-19: "the text is still
			// highlighted in yellow, which requires me to press Y on controller to exit before I
			// can move out of the box with the D-pad ... we need to make it so that it doesn't
			// default to having the text highlighted after exiting the keyboard or search").
			//
			// This is the other half of a text field owning the D-pad while it is active: with
			// the D-pad no longer able to navigate away, there has to be a deliberate way OUT of
			// the box, and B is the one the whole menu already uses for "back". Done here rather
			// than inside the field so it works for every mod's text box as well as ours.
			// Asked of the input layer, not of ImGui: the B press never reaches ImGui while a text
			// field is active, precisely so ImGui cannot revert the text with it.
			// In a MODAL (the rename box) the same B closes the box as well (W3 1.0.4: one B or Escape closes it, edited or
			// not); it is closed below, after NewFrame, with the Escape that does the same.
			bool backFromFieldInModal = false;
			if (visible && GImGui && GImGui->ActiveId != 0 && keyboard::IsTextField(GImGui->ActiveId) &&
				input::TakeTextFieldCancel())
			{
				const ImGuiContext& g = *GImGui;
				backFromFieldInModal = g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window &&
									   (g.OpenPopupStack.back().Window->Flags & ImGuiWindowFlags_Modal);
				logger::debug("input: B released text field {} - {}", GImGui->ActiveId,
							  backFromFieldInModal ? "it is in a modal, which B closes too" : "navigation is free again");
				ImGui::ClearActiveID();
				keyboard::Hide();
			}

			// A CLICK GIVES THE CLICKED CONTROL THE PAD AND THE KEYS TOO (W3 1.0.3 test, 2026-10-05: a slider clicked with the
			// mouse ignored A until the D-pad had walked onto it). The click already makes it ImGui's nav item (SetFocusID),
			// but marks the highlight as hidden for a mouse user, and ImGui reads A / Space / Enter only while it is shown.
			// So when this frame's input carries one of those presses and the highlight is hidden on an item, it is shown
			// first: the press then acts on the clicked control - A takes hold of a slider, as after a D-pad walk. Read
			// from the queued events, before NewFrame, because NewFrame is where ImGui decides. Directions already move
			// from the clicked item.
			if (visible && GImGui)
			{
				ImGuiContext& g = *GImGui;
				if (g.NavDisableHighlight && g.NavId != 0 && g.ActiveId == 0 && g.NavWindow)
				{
					bool activate = false;
					for (const ImGuiInputEvent& e : g.InputEventsQueue)
					{
						if (e.Type == ImGuiInputEventType_Key && e.Key.Down &&
							(e.Key.Key == ImGuiKey_Space || e.Key.Key == ImGuiKey_Enter || e.Key.Key == ImGuiKey_KeypadEnter ||
							 e.Key.Key == ImGuiKey_GamepadFaceDown))
						{
							activate = true;
						}
					}
					if (activate)
					{
						g.NavDisableHighlight = false;
						g.NavDisableMouseHover = true;
						logger::debug("nav: activate press after a mouse click - highlight shown on item {} in '{}', the press acts on it",
									  g.NavId, g.NavWindow->Name);
					}
				}
			}

			// A MENU CLOSED FROM THE PAD KEEPS THE HIGHLIGHT (W3 1.0.5 run: after B closed a side-list row's Y menu, the
			// highlight vanished until the next D-pad press). When the last popup has just closed and the controller is in
			// use, the highlight is shown again on the item it went back to.
			{
				static bool s_popupWasOpen = false;
				const bool popupOpen = GImGui && GImGui->OpenPopupStack.Size > 0;
				if (visible && GImGui && s_popupWasOpen && !popupOpen && input::UsingController() && GImGui->NavId != 0 &&
					GImGui->NavDisableHighlight)
				{
					GImGui->NavDisableHighlight = false;
					logger::debug("nav: popup closed from the controller - highlight shown again on item {}", GImGui->NavId);
				}
				s_popupWasOpen = popupOpen;
			}

			// Popups, combo lists and tooltips keep out of the screen's edge band too (EdgeMargin) - ImGui's own rule for them.
			{
				const float edge = EdgeMargin(ImGui::GetIO().DisplaySize);
				ImGui::GetStyle().DisplaySafeAreaPadding = ImVec2(edge, edge);
			}

			// Escape closes the menu only when nothing was open over it (below). Read BEFORE NewFrame: NewFrame is where
			// ImGui's own Escape / B closes an open popup, so afterwards the popup would already look closed.
			const int popupsBefore = GImGui ? GImGui->OpenPopupStack.Size : 0;
			const ImGuiID activeBefore = GImGui ? GImGui->ActiveId : 0u;

			watchdog::Tick();  // liveness signal for the hang watchdog
			g_gameFrames.clear();                    // a frame that never reached Render leaves no stale marks
			GImGuiAMFHighlight = &RecordGameFrameHook;
			ImGui::NewFrame();

			// The game's own HUD opacity, re-read every frame so the options slider is
			// followed live (theme spec point 3), applied as the ONE global multiplier.
			ImGui::GetStyle().Alpha = theme::GetGameHUDOpacity();

			// The consumer surface draws EVERY frame, whether or not our own menu is up.
			// A HUD element that only appeared while the framework menu was open would not
			// be a HUD element, and a consumer window's visibility is the consumer's to
			// decide through the IsOpen flag it was handed - not ours.
			consumer::DrawHudElements();
			consumer::DrawWindows();

			if (visible)
			{
				// Escape closes. The keypress was consumed input-side, so the game does
				// not also react to the same stroke.
				// ...unless a popup was open (W3 1.0.3 test, 2026-10-05: Escape on the open Theme list closed the whole
				// menu). ImGui's nav cancel has already closed a dropdown, context menu or submenu in NewFrame; a modal (the
				// rename box) it leaves open, so that one is closed here. B does the same for a modal; for the other popups
				// ImGui's nav cancel is B already.
				// ONE PRESS CLOSES THE RENAME BOX, EDITED OR NOT (W3 1.0.4 - the tester: the first Escape only left the text
				// field and a second closed the box). The modal is closed before its text field is drawn again, so the field
				// is released, nothing typed is applied (only Rename / Enter / A applies the name), and the press does not
				// also close the menu. B reaches here as the text field's cancel (input layer) when the field was active.
				const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
				const bool padBack = (input::UsingController() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) || backFromFieldInModal;
				if ((escape || padBack) && popupsBefore > 0)
				{
					ImGuiContext& g = *GImGui;
					const char* what = "closed by ImGui";
					if (g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window &&
						(g.OpenPopupStack.back().Window->Flags & ImGuiWindowFlags_Modal))
					{
						if (g.ActiveId != 0 && keyboard::IsTextField(g.ActiveId)) { ImGui::ClearActiveID(); }
						keyboard::Hide();
						ImGui::ClosePopupToLevel(g.OpenPopupStack.Size - 1, true);
						g_renameTarget.clear();   // the rename box is the only modal; its edit is dropped with it
						what = activeBefore != 0 ? "modal closed here with its text edit discarded" : "modal closed here";
					}
					else if (activeBefore != 0)
					{
						what = "ended a text edit first";
					}
					logger::debug("input: {} with {} popup(s) open - {}; the menu stays open", escape ? "Escape" : "B", popupsBefore, what);
					DrawFrameworkWindow();
				}
				else if (escape)
				{
					ToggleMainWindow();
				}
				else
				{
					DrawFrameworkWindow();
				}
			}
			// The on-screen keyboard, after the window so this frame's text fields are known. It
			// closes itself when the window is not up.
			keyboard::Draw();

			// LAST thing in the frame: the curtain covers the framework's own window and every
			// consumer HUD element rather than being interleaved with them.
			curtain::Draw();

			FlushGameFrames();   // the game's frame round this frame's hovered and nav-highlighted items, over everything
			ImGui::Render();
			ClipDrawDataToSafeRect();   // after Render, before the overlay records the draw data: nothing lands in the edge band
		}
	}

	bool OnDeviceReady(void* a_hwnd, unsigned a_width, unsigned a_height)
	{
		return DeviceReady(static_cast<HWND>(a_hwnd), a_width, a_height);
	}

	void OnFrame()
	{
		Frame();
	}

	void ToggleMainWindow()
	{
		const bool now = !g_windowVisible.load(std::memory_order_relaxed);
		g_nested.store(false, std::memory_order_release);   // the hotkey opens our own window
		g_windowVisible.store(now, std::memory_order_release);

		if (now)
		{
			g_justOpened.store(true, std::memory_order_release);
			g_applyGeometry.store(true, std::memory_order_release);
		}
		else
		{
			// A mod's bind button capture ends with the window: left armed, a keyboard-side capture closed from
			// the pad would take the next key in the game as the binding and hide it from the game.
			bindings::CancelConsumerCapture();
		}

		logger::info("Framework window {}", now ? "shown" : "hidden");
	}

	void* GetGameWindow()
	{
		return g_gameWindow.load(std::memory_order_acquire);
	}

	bool WantsTextInput()
	{
		return g_wantTextInput.load(std::memory_order_acquire);
	}

	bool IsMainWindowVisible()
	{
		return g_windowVisible.load(std::memory_order_relaxed);
	}

	float UiScale()
	{
		return g_uiScale;
	}

	void SetGameMenuOpen(bool a_open)
	{
		if (g_gameMenuOpen.exchange(a_open) != a_open) {
			logger::debug("window: the game's menu is {}", a_open ? "open" : "closed");
		}
	}

	void RecordGameFrame(ImDrawList* a_drawList, const ImVec2& a_min, const ImVec2& a_max, const ImVec2& a_clipMin, const ImVec2& a_clipMax)
	{
		g_gameFrames.push_back({ a_drawList, ImRect(a_min, a_max), ImRect(a_clipMin, a_clipMax), true });
	}

	bool ConsumerWindowOwnsInput()
	{
		return g_consumerInput.load(std::memory_order_acquire);
	}

	void WindowToImageScale(float& a_x, float& a_y)
	{
		a_x = g_imageScaleX.load(std::memory_order_relaxed);
		a_y = g_imageScaleY.load(std::memory_order_relaxed);
	}

	// A page declares its own tab bar, and takes back the tab the D-pad asked for (-1 = nothing asked).
	// Called from the page's render function, so it is already on the render thread inside the frame.
	int DeclareInnerTabs(int a_count, int a_current)
	{
		g_innerCount = a_count > 0 ? a_count : 0;
		g_innerIndex = (a_current >= 0 && a_current < g_innerCount) ? a_current : 0;
		g_innerFresh = true;
		const int request = g_innerRequest;
		g_innerRequest = -1;  // handed over once, exactly like the framework's own tab request
		return (request >= 0 && request < g_innerCount) ? request : -1;
	}

	void SetMenuVisible(bool a_visible, bool a_nested)
	{
		g_nested.store(a_visible && a_nested, std::memory_order_release);
		g_windowVisible.store(a_visible, std::memory_order_release);
		if (a_visible)
		{
			g_justOpened.store(true, std::memory_order_release);
			g_applyGeometry.store(true, std::memory_order_release);
		}
		else
		{
			bindings::CancelConsumerCapture();   // as in ToggleMainWindow: a capture never outlives the window
		}
		logger::info("Framework window {} ({})", a_visible ? "shown" : "hidden",
			a_nested ? "nested in the game's System menu" : "external/DevBench");
	}

	void SetSelectedNode(const std::string& a_node)
	{
		std::scoped_lock l(g_selLock);
		g_selExternal = true;
		if (a_node.rfind("mod:", 0) == 0)
		{
			g_selNode = "mod";
			try { g_selMod = std::stoi(a_node.substr(4)); } catch (...) {}
			return;
		}
		std::string n = a_node;
		if (n.rfind("system/", 0) == 0) { n = n.substr(7); }  // pre-1.4.4 paths still accepted
		if (n == "settings" || n == "controls" || n == "help" || n == "mod") { g_selNode = n; }
	}

	std::string GetSelectedNode()
	{
		std::scoped_lock l(g_selLock);
		return g_selNode;
	}

	// Runs the action bound to the currently selected node (Save/Quit); categories and mods have
	// no direct action - selecting them IS the interaction. Safe from any thread (RunConsoleCommand
	// marshals to the main thread).
	void ActivateSelectedNode()
	{
		// SMF shape: no node carries an action - selecting a mod or a framework page IS the interaction.
	}

	// JSON snapshot of the menu for DevBench: visibility, the selected node, and every registered
	// mod + its pages. Read-only; safe from the listener thread (registry::Snapshot is thread-safe).
	std::string CaptureBlocking(const std::wstring& a_path, unsigned a_timeoutMs)
	{
		if (!g_d3dReady.load(std::memory_order_acquire)) { return "renderer not ready"; }
		// Skyrim saved the D3D11 back buffer with DirectXTK's ScreenGrab. On D3D12 that is a readback copy on the
		// presenting queue, not written yet; GameLink / GameWatch capture the window from outside in the meantime.
		if (a_timeoutMs != 0xFFFFFFFFu) { return "in-process capture is not implemented on D3D12 yet - use GameLink's see / gamewatch snap"; }
		std::unique_lock l(g_captureLock);
		if (!g_capturePath.empty()) { return "a capture is already pending"; }
		g_capturePath = a_path; g_captureDone = false; g_captureError.clear();
		const bool ok = g_captureCv.wait_for(l, std::chrono::milliseconds(a_timeoutMs), [] { return g_captureDone; });
		if (!ok) { g_capturePath.clear(); return "timed out waiting for a frame (is the game presenting?)"; }
		return g_captureError;
	}

	bool SetTheme(const std::string& a_themeId)
	{
		for (const theme::Palette& palette : theme::ListThemes())
		{
			if (palette.id == a_themeId)
			{
				theme::SetActiveTheme(a_themeId);
				theme::Apply();
				skin::Reload();
				settings::Get().themeId = a_themeId;
				settings::Save();
				logger::info("theme switched to \"{}\" (DevBench)", a_themeId);
				return true;
			}
		}
		logger::warn("theme \"{}\" is not registered", a_themeId);
		return false;
	}

	bool SetModAlias(const std::string& a_modName, const std::string& a_alias)
	{
		if (personalization::IsSeparator(a_modName))
		{
			if (a_alias.empty()) { return false; }   // a separator keeps a name
			personalization::SetAlias(a_modName, a_alias);
			settings::Save();
			return true;
		}
		const auto entries = registry::Snapshot();
		for (const registry::Entry& entry : entries)
		{
			if (entry.modName == a_modName)
			{
				personalization::SetAlias(a_modName, a_alias);
				settings::Save();
				return true;
			}
		}
		return false;
	}

	bool MoveModTo(const std::string& a_modName, int a_position)
	{
		const auto entries = registry::Snapshot();
		if (personalization::IsSeparator(a_modName))
		{
			personalization::MoveTo(entries, a_modName, a_position);
			settings::Save();
			return true;
		}
		for (const registry::Entry& entry : entries)
		{
			if (entry.modName == a_modName)
			{
				personalization::MoveTo(entries, a_modName, a_position);
				settings::Save();
				return true;
			}
		}
		return false;
	}

	void ResetModOrder()
	{
		personalization::ResetToAlphabetical();
		settings::Save();
	}

	std::string SeparatorOp(const std::string& a_action, const std::string& a_name, const std::string& a_mod, const std::string& a_separator)
	{
		const auto entries = registry::Snapshot();
		bool ok = false;
		std::string id;
		if (a_action == "add")
		{
			id = personalization::AddSeparator(entries, a_mod, a_name.empty() ? std::string("New separator") : a_name);
			ok = true;
		}
		else if (a_action == "remove") { ok = personalization::RemoveSeparator(a_separator); }
		else if (a_action == "send") { ok = personalization::SendTo(entries, a_mod, a_separator); }
		else if (a_action == "collapse")
		{
			ok = personalization::IsSeparator(a_separator);
			if (ok) { personalization::ToggleCollapsed(a_separator); }
		}
		else if (a_action == "favourite")
		{
			ok = personalization::IsSeparator(a_separator);
			if (ok) { personalization::ToggleFavourite(a_separator); }
		}
		if (ok) { settings::Save(); }
		return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"separator\",\"action\":\"" + a_action + "\"" +
			   (id.empty() ? std::string() : ",\"id\":\"" + id + "\"") + "}";
	}

	bool QueueNav(const std::string& a_direction)
	{
		if (a_direction == "left")  { g_navRequest.store(1, std::memory_order_release); return true; }
		if (a_direction == "right") { g_navRequest.store(2, std::memory_order_release); return true; }
		logger::warn("amf.menu nav: unknown direction \"{}\" (expected left or right)", a_direction);
		return false;
	}

	bool FocusPane(const std::string& a_pane)
	{
		if (a_pane == "list")    { g_focusPane = 1; g_navToSelected = true; return true; }
		if (a_pane == "options") { g_focusPane = 2; return true; }
		logger::warn("amf.menu focus: unknown pane \"{}\" (expected list or options)", a_pane);
		return false;
	}

	// 1.8.9: the active theme's frame, for a consumer's own box (Item Explorer's 3D preview first): the
	// Skyrim theme's knotwork, a UI author's frame art when configured, nothing under a theme without a
	// frame. Drawn just outside the rect like the window's own. Returns whether anything was drawn, so
	// the consumer can fall back to a plain line.
	bool DrawThemeFrameAround(ImDrawList* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1)
	{
		if (!a_drawList || a_x1 <= a_x0 || a_y1 <= a_y0) { return false; }
		const bool knot = theme::GetActiveTheme().knotwork || skin::HasFrame();
		if (!knot) { return false; }
		DrawKnotworkAround(a_drawList, ImVec2(a_x0, a_y0), ImVec2(a_x1, a_y1));
		return true;
	}

	std::string GetMenuStateJson()
	{
		std::string node, tab, tabName; int selMod, tabIndex, tabCount;
		{
			std::scoped_lock l(g_selLock);
			node = g_selNode; tab = g_selTab; selMod = g_selMod;
			tabName = g_selTabName; tabIndex = g_selTabIndex; tabCount = g_selTabCount;
		}
		const bool visible = g_windowVisible.load(std::memory_order_relaxed);
		const auto entries = registry::Snapshot();
		std::string searchText; { std::scoped_lock l(g_searchTextLock); searchText = g_searchText; }
		const std::string searchJson = "\"search\":{\"text\":\"" + [&]{ std::string o; for (char c : searchText) { if (c == '"' || c == '\\') { o += '\\'; } o += c; } return o; }() +
			"\",\"len\":" + std::to_string(g_searchLen.load()) + ",\"active\":" + (g_searchActive.load() ? "true" : "false") +
			",\"rect\":[" + std::to_string(g_searchRect[0].load()) + "," + std::to_string(g_searchRect[1].load()) + "," + std::to_string(g_searchRect[2].load()) + "," + std::to_string(g_searchRect[3].load()) + "]}," +
			"\"wantTextInput\":" + (g_wantTextInput.load() ? "true" : "false") + ",\"backspaceDown\":" + (g_backspaceDown.load() ? "true" : "false") +
			",\"activeId\":" + std::to_string(g_activeIdMirror.load()) + ",\"keyCtrl\":" + (g_modCtrl.load() ? "true" : "false") + ",\"keyShift\":" + (g_modShift.load() ? "true" : "false") + ",\"keyAlt\":" + (g_modAlt.load() ? "true" : "false") + ",";
		auto esc = [](const std::string& v) { std::string o; for (char c : v) { if (c == '"' || c == '\x5C') { o += '\x5C'; } o += c; } return o; };
		std::string mods;
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			if (i) { mods += ","; }
			std::string pages;
			for (std::size_t j = 0; j < entries[i].pages.size(); ++j)
			{
				if (j) { pages += ","; }
				pages += "\"" + esc(entries[i].pages[j].pageName) + "\"";
			}
				std::string hiddenPages;
				for (const registry::Page& page : entries[i].pages)
				{
					if (page.hidden) { hiddenPages += (hiddenPages.empty() ? "\"" : ",\"") + esc(page.pageName) + "\""; }
				}
				mods += "{\"index\":" + std::to_string(i) + ",\"name\":\"" + esc(entries[i].modName) + "\",\"pages\":[" + pages + "],\"hiddenPages\":[" + hiddenPages + "]}";
		}
		// Menu-shell personalization: the list AS THE PLAYER SEES IT (position, identity, shown
		// name), so a driving tool can assert the order and the aliases without reading pixels.
		std::string order;
		{
			// The rows the side list draws (ShownOrder): a mod whose every page is hidden has no row there, so none here.
			// Its pages stay listed under mods[].hiddenPages. A folded separator's mods are kept, marked "hidden": true.
			const auto rows = ShownOrder(entries);
			{
				static std::atomic<int> s_leftOut{ -1 };   // listener thread; logged when the number changes
				const int leftOut = static_cast<int>(std::count_if(entries.begin(), entries.end(), [](const registry::Entry& e) {
					return !e.pages.empty() && std::all_of(e.pages.begin(), e.pages.end(), [](const registry::Page& p) { return p.hidden; });
				}));
				if (s_leftOut.exchange(leftOut) != leftOut)
				{
					logger::debug("state: displayOrder leaves out {} mod(s) whose pages are all hidden, as the side list does", leftOut);
				}
			}
			for (std::size_t i = 0; i < rows.size(); ++i)
			{
				if (i) { order += ","; }
				order += "{\"pos\":" + std::to_string(i + 1) + ",\"index\":" + std::to_string(rows[i].registryIndex) +
						 ",\"mod\":\"" + esc(rows[i].modName) + "\",\"shows\":\"" + esc(rows[i].displayName) + "\"" +
						 (rows[i].separator ? std::string(",\"separator\":true,\"collapsed\":") + (rows[i].collapsed ? "true" : "false") +
											  ",\"children\":" + std::to_string(rows[i].children)
											: std::string(",\"depth\":") + std::to_string(rows[i].depth) + (rows[i].hidden ? ",\"hidden\":true" : "")) +
						 "}";
			}
		}
		// Consumer-window diagnostic (2026-09-12). Mods gate their own hotkeys on
		// IsAnyBlockingWindowOpened(), which is `visible || any consumer window open AND blocking`.
		// Two users reported hotkeys dead under AMF but working on SKSE Menu Framework, and the
		// aggregate on its own would not say WHICH window was latched - so each is listed.
		// blockingWindowOpen is computed from the same copied snapshot, never by calling
		// consumer::AnyBlockingWindowOpen() here, because that takes the lock WindowStates() holds.
		std::string windows;
		bool anyBlocking = false;
		{
			const auto states = consumer::WindowStates();
			for (std::size_t i = 0; i < states.size(); ++i)
			{
				if (states[i].open && states[i].blocking) { anyBlocking = true; }
				if (i) { windows += ","; }
				// Skyrim 2.0.4: whether it takes the mouse, and the top-level windows it drew last frame - the input gate's inputs.
				std::string submitted;
				for (const consumer::SubmittedWindow& w : states[i].submitted)
				{
					if (!submitted.empty()) { submitted += ","; }
					submitted += "{\"name\":\"" + esc(w.name) + "\",\"flags\":\"0x" + std::format("{:X}", static_cast<unsigned>(w.flags)) +
								 "\",\"noMouseInputs\":" + (w.noMouseInputs ? "true" : "false") + ",\"noInputs\":" + (w.noInputs ? "true" : "false") +
								 ",\"pos\":[" + std::to_string(static_cast<int>(w.x)) + "," + std::to_string(static_cast<int>(w.y)) + "],\"size\":[" +
								 std::to_string(static_cast<int>(w.w)) + "," + std::to_string(static_cast<int>(w.h)) + "]}";
				}
				windows += "{\"open\":" + std::string(states[i].open ? "true" : "false") +
						   ",\"blocking\":" + (states[i].blocking ? "true" : "false") +
						   ",\"acceptsMouse\":" + (states[i].acceptsMouse ? "true" : "false") +
						   ",\"view\":\"" + esc(states[i].view) + "\",\"submitted\":[" + submitted + "]}";
			}
		}

		// The window's real rect on the last drawn frame and its two window switches (Skyrim 2.1.1), so a top-row drag or
		// an edge drag driven with op=mouse / op=cursor can be measured.
		std::string mainWindow;
		{
			std::scoped_lock l(g_selLock);
			mainWindow = ",\"mainWindow\":{\"pos\":[" + std::to_string(static_cast<int>(g_mainX)) + "," + std::to_string(static_cast<int>(g_mainY)) +
						 "],\"size\":[" + std::to_string(static_cast<int>(g_mainW)) + "," + std::to_string(static_cast<int>(g_mainH)) + "]" +
						 ",\"movable\":" + (settings::Get().movableWindow ? "true" : "false") +
						 ",\"freeResize\":" + (settings::Get().freeResize ? "true" : "false") + "}";
		}
		float imageScaleX = 1.0f, imageScaleY = 1.0f;
		WindowToImageScale(imageScaleX, imageScaleY);

		float cursorX = 0.0f, cursorY = 0.0f;
		input::GetCursor(cursorX, cursorY);
		return std::string("{" + searchJson + "\"cursor\":{\"x\":") + std::to_string(static_cast<int>(cursorX)) + ",\"y\":" + std::to_string(static_cast<int>(cursorY)) + "}" +
			   ",\"visible\":" + (visible ? "true" : "false") +
			   ",\"blockingWindowOpen\":" + ((visible || anyBlocking) ? "true" : "false") +
			   ",\"consumerWindows\":[" + windows + "]" +
			   ",\"consumerInput\":" + (g_consumerInput.load(std::memory_order_acquire) ? "true" : "false") +
			   mainWindow +
			   ",\"imageScale\":[" + std::format("{:.3f},{:.3f}", imageScaleX, imageScaleY) + "]" +
			   ",\"tab\":\"" + esc(tab) + "\",\"selected\":\"" + esc(node) + "\",\"selectedMod\":" + std::to_string(selMod) +
			   ",\"page\":\"" + esc(tabName) + "\",\"pageIndex\":" + std::to_string(tabIndex) +
			   ",\"pageCount\":" + std::to_string(tabCount) +
			   // The menu key (Skyrim 2.0.5): uToggleKey, the key the input hook really opens on, and which file it came
			   // from - user (User.ini's uToggleKey), user-controls (its [Bindings] sToggleMenu), shipped, default, or
			   // fallback (the value given was not a usable key).
			   ",\"menuKey\":{\"uToggleKey\":" + std::to_string(settings::Get().toggleKey) +
			   ",\"bound\":" + std::to_string(bindings::ToggleKeyboardCode()) +
			   ",\"name\":\"" + esc(settings::Get().toggleKey > 0 ? bindings::KeyName(static_cast<std::uint32_t>(settings::Get().toggleKey)) : std::string("none")) + "\"" +
			   ",\"source\":\"" + settings::ToggleKeySourceName(settings::GetToggleKeySource()) + "\"}" +
			   // See-through window (Skyrim 2.1.1): the switch, the percentage, and the window background's alpha as drawn.
			   ",\"seeThrough\":" + (settings::Get().seeThrough ? "true" : "false") +
			   ",\"windowOpacity\":" + std::to_string(settings::Get().windowOpacity) +
			   ",\"windowBgAlpha\":" + std::to_string(GImGui ? ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w : 0.0f) +
			   ",\"controllerMode\":" + (input::UsingController() ? "true" : "false") +
			   ",\"lastDevice\":\"" + (input::LastDevice() == input::Device::kGamepad ? "gamepad" :
										   input::LastDevice() == input::Device::kKeyboardMouse ? "keyboard" : "none") + "\"" +
			   ",\"customOrder\":" + (personalization::IsCustomOrder() ? "true" : "false") +
			   // 1.7.9: ImGui's navigation cursor, so a "the cursor jumps to the top of the list" report
			   // (housem3, 2026-09-13) can be measured by a driving script rather than described.
			   ",\"navId\":" + std::to_string(GImGui ? GImGui->NavId : 0u) +
			   ",\"navWindow\":\"" + esc(GImGui && GImGui->NavWindow && GImGui->NavWindow->Name ? GImGui->NavWindow->Name : "") + "\"" +
			   ",\"displayOrder\":[" + order + "]" +
			   ",\"mods\":[" + mods + "]" +
			   ",\"keyboard\":" + keyboard::StateJson() + "}";
	}
}
