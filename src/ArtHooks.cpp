#include "PCH.h"

// THE ART HOOK (1.0.5, ported from Skyrim AMF 2.1.6's ArtHooks.cpp). See ArtHooks.h.

#include "ArtHooks.h"

#include "Skin.h"
#include "Theme.h"

#include "Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace arthooks
{
	namespace
	{
		using skin::ArtKind;

		// The resolution scale, read from the text size the framework set (16 px text at 1080p), as the highlight
		// frame reads it. Parts are drawn at twice their 1080p size, so they stay sharp at 4K.
		float Unit()
		{
			return std::max(0.5f, ImGui::GetFontSize() / 16.0f);
		}

		// A part's corner on screen: uDrawCorner (or uCorner) at 1080p, times the scale, never more than half the rect.
		float ScreenCorner(const skin::ArtPart& a_part, const ImRect& a_bb, float a_extra = 1.0f)
		{
			const float base = a_part.drawCorner > 0.0f ? a_part.drawCorner : a_part.corner * 0.5f;
			return std::min({ base * Unit() * a_extra, a_bb.GetWidth() * 0.5f, a_bb.GetHeight() * 0.5f });
		}

		// Nine-slice: corners at a_dcs on screen, edges stretched (or repeated, a_tile), centre stretched - for a plate
		// the centre is the plate itself, so unlike the window frame it is drawn.
		void NineSlice(ImDrawList* a_dl, const skin::ArtImage& a_img, float a_cs, float a_dcs, const ImRect& a_bb, ImU32 a_col,
					   bool a_centre = true, bool a_tile = false)
		{
			if (!a_img.srv || a_img.w <= 0.0f || a_img.h <= 0.0f || a_cs <= 0.0f || a_dcs <= 0.0f) { return; }
			const float W = a_img.w, H = a_img.h, cs = a_cs, dcs = a_dcs;
			const float u1 = cs / W, u2 = (W - cs) / W, v1 = cs / H, v2 = (H - cs) / H;
			const float x0 = a_bb.Min.x, x1 = x0 + dcs, x3 = a_bb.Max.x, x2 = x3 - dcs;
			const float y0 = a_bb.Min.y, y1 = y0 + dcs, y3 = a_bb.Max.y, y2 = y3 - dcs;
			if (x2 < x1 || y2 < y1) { return; }
			const auto tex = reinterpret_cast<ImTextureID>(a_img.srv);
			auto slice = [&](float ax, float ay, float bx, float by, float au, float av, float bu, float bv) {
				if (bx - ax <= 0.0f || by - ay <= 0.0f) { return; }
				a_dl->AddImage(tex, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), a_col);
			};
			slice(x0, y0, x1, y1, 0.0f, 0.0f, u1, v1);
			slice(x2, y0, x3, y1, u2, 0.0f, 1.0f, v1);
			slice(x0, y2, x1, y3, 0.0f, v2, u1, 1.0f);
			slice(x2, y2, x3, y3, u2, v2, 1.0f, 1.0f);
			if (!a_tile)
			{
				slice(x1, y0, x2, y1, u1, 0.0f, u2, v1);
				slice(x1, y2, x2, y3, u1, v2, u2, 1.0f);
				slice(x0, y1, x1, y2, 0.0f, v1, u1, v2);
				slice(x2, y1, x3, y2, u2, v1, 1.0f, v2);
			}
			else
			{
				// 2.1.6: bTileEdges on a part - the sides repeat at the art's own scale instead of stretching (a rope's twist, a
				// stitch); the last repeat is cut to fit
				const float k = dcs / cs;
				const float stepX = std::max(1.0f, (W - 2.0f * cs) * k), stepY = std::max(1.0f, (H - 2.0f * cs) * k);
				for (float x = x1; x < x2 - 0.5f; x += stepX)
				{
					const float e = std::min(x + stepX, x2);
					const float ue = u1 + (u2 - u1) * ((e - x) / stepX);
					slice(x, y0, e, y1, u1, 0.0f, ue, v1);
					slice(x, y2, e, y3, u1, v2, ue, 1.0f);
				}
				for (float y = y1; y < y2 - 0.5f; y += stepY)
				{
					const float e = std::min(y + stepY, y2);
					const float ve = v1 + (v2 - v1) * ((e - y) / stepY);
					slice(x0, y, x1, e, 0.0f, v1, u1, ve);
					slice(x2, y, x3, e, u2, v1, 1.0f, ve);
				}
			}
			if (a_centre) { slice(x1, y1, x2, y2, u1, v1, u2, v2); }
		}

		// A plate (box, button, tick box, slider grab, scroll grab, tab): the shape tinted with the colour the control has
		// right now - so the theme's colours, the player's Colours picks and the hover / held states all still show - and
		// its edge layer, when it has one, in the theme's line colour.
		// 2.1.6 (the owner: the Oblivion slider and scroll grabs "should just be a single line of the frame art with its ending
		// circles art at either end of it"): a cap the art's width tall at each end of the long side, the middle between them.
		void ThreeSlice(ImDrawList* a_dl, const skin::ArtImage& a_img, const ImRect& a_bb, ImU32 a_col, bool a_tile)
		{
			if (!a_img.srv || a_img.w <= 0.0f || a_img.h <= a_img.w) { return; }
			const auto tex = reinterpret_cast<ImTextureID>(a_img.srv);
			const bool vertical = a_bb.GetHeight() >= a_bb.GetWidth();
			const float across = vertical ? a_bb.GetWidth() : a_bb.GetHeight();
			const float along = vertical ? a_bb.GetHeight() : a_bb.GetWidth();
			const float cap = std::min(across, along * 0.5f);
			const float v1 = a_img.w / a_img.h, v2 = 1.0f - v1;   // the caps are the art's width tall
			// the art is drawn standing up; a horizontal grab lies it on its side
			auto quad = [&](float s0, float s1, float t0, float t1) {
				if (s1 - s0 <= 0.0f) { return; }
				if (vertical)
				{
					a_dl->AddImage(tex, ImVec2(a_bb.Min.x, a_bb.Min.y + s0), ImVec2(a_bb.Max.x, a_bb.Min.y + s1), ImVec2(0.0f, t0), ImVec2(1.0f, t1), a_col);
				}
				else
				{
					const ImVec2 a(a_bb.Min.x + s0, a_bb.Min.y), b(a_bb.Min.x + s1, a_bb.Min.y), c(a_bb.Min.x + s1, a_bb.Max.y), d(a_bb.Min.x + s0, a_bb.Max.y);
					a_dl->AddImageQuad(tex, a, b, c, d, ImVec2(1.0f, t0), ImVec2(1.0f, t1), ImVec2(0.0f, t1), ImVec2(0.0f, t0), a_col);
				}
			};
			quad(0.0f, cap, 0.0f, v1);
			quad(along - cap, along, v2, 1.0f);
			const float midLen = along - 2.0f * cap;
			if (midLen <= 0.0f) { return; }
			if (!a_tile) { quad(cap, along - cap, v1, v2); return; }
			const float step = std::max(1.0f, (a_img.h - 2.0f * a_img.w) * (across / a_img.w));
			for (float s = cap; s < along - cap - 0.5f; s += step)
			{
				const float e = std::min(s + step, along - cap);
				quad(s, e, v1, v1 + (v2 - v1) * ((e - s) / step));
			}
		}

		// The colour role that recolours a kind's own-colour art when the player picks it.
		int OwnRole(ArtKind a_kind)
		{
			switch (a_kind)
			{
			case ArtKind::kBox:         return theme::kRoleBoxes;
			case ArtKind::kButton:      return theme::kRoleButtons;
			case ArtKind::kTickBox:     return theme::kRoleBoxes;
			case ArtKind::kSlider:      return theme::kRoleSlider;
			case ArtKind::kScrollbar:   return theme::kRoleScrollbar;
			case ArtKind::kTab:         return theme::kRoleTabs;
			case ArtKind::kSliderTrack: return theme::kRoleSliderTrack;
			case ArtKind::kScrollTrack: return theme::kRoleScrollTrack;
			default:                    return -1;
			}
		}

		// a_outline: the control's basic outline (1.0.6, Skyrim 2.2.0 - the owner 2026-10-12: "everything should have a basic
		// minimal outline so that it's at least visible. And then the detailed art styled frame ... when it's focused or
		// hovered"). A part with no edge layer of its own - every shipped box, button and tick box - covered the rect without
		// one, so a face in the panel's colour (the default) vanished. The outline is ImGui's own border line, in the Border
		// colour (which theme::Apply keeps visible), at least one pixel wide; the hover frame still draws over it.
		bool DrawPlate(ImDrawList* a_dl, ArtKind a_kind, const ImRect& a_bb, ImU32 a_col, bool a_outline = false)
		{
			const skin::ArtPart* part = skin::Part(a_kind);
			if (!part || a_bb.GetWidth() < 2.0f || a_bb.GetHeight() < 2.0f) { return false; }
			const float dcs = ScreenCorner(*part, a_bb);
			if (!part->threeSlice) { NineSlice(a_dl, part->main, part->corner, dcs, a_bb, a_col); }
			else { ThreeSlice(a_dl, part->main, a_bb, a_col, false); }
			if (a_outline && !part->edge.srv)
			{
				const ImGuiStyle& st = ImGui::GetStyle();
				a_dl->AddRect(a_bb.Min, a_bb.Max, ImGui::GetColorU32(ImGuiCol_Border), st.FrameRounding, 0, std::max(1.0f, st.FrameBorderSize));
			}
			if (part->edge.srv)
			{
				ImU32 edgeCol = ImGui::GetColorU32(ImGuiCol_Border);
				if (part->ownColours)
				{
					// art in its own colours: as painted (the Frame art tint), or the kind's own colour once picked
					const int role = OwnRole(a_kind);
					const std::uint32_t c = role >= 0 && theme::RolePicked(role) ? theme::RoleColor(role) : theme::RoleColor(theme::kRoleArt);
					edgeCol = c ? static_cast<ImU32>(c) : IM_COL32_WHITE;
				}
				if (part->threeSlice) { ThreeSlice(a_dl, part->edge, a_bb, edgeCol, part->tile); }
				else { NineSlice(a_dl, part->edge, part->corner, dcs, a_bb, edgeCol, true, part->tile); }
			}
			return true;
		}

		// RenderFrame is every widget's field and every button: told apart by the colour ImGui asked for.
		// 2.1.6 (the owner: Buttons art "doesn't actually seem to apply to ... the filter or sort buttons, or where mods keep
		// their save, reload ... and restore defaults buttons"): told apart by WHAT the item is first, the colour second. A
		// theme whose buttons and fields share a colour sent every button to Box, and a mod's button in its own colours
		// matched neither. The item RenderFrame draws for was just added: text fields, sliders and drags are "inputable".
		int FrameKind(ImU32 a_col)
		{
			for (const ImGuiCol c : { ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return -1; }   // rows and headers keep ImGui's own look
			}
			const ImGuiContext& g = *GImGui;
			if (g.LastItemData.InFlags & ImGuiItemFlags_Inputable) { return static_cast<int>(ArtKind::kBox); }
			for (const ImGuiCol c : { ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return static_cast<int>(ArtKind::kButton); }
			}
			for (const ImGuiCol c : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return static_cast<int>(ArtKind::kBox); }
			}
			return static_cast<int>(ArtKind::kButton);   // a mod's button in colours of its own
		}

		// The section line: a strip drawn as end caps and a stretched middle, centred on the line ImGui would draw, its
		// height the part's own at 1080p. Tinted with the separator colour.
		bool DrawSection(ImDrawList* a_dl, const ImRect& a_bb, ImU32 a_col)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kSection);
			if (!part || a_bb.GetWidth() < 4.0f) { return false; }
			float k = (part->drawCorner > 0.0f ? part->drawCorner / std::max(1.0f, part->corner) : 0.5f) * Unit();
			// 2.1.6 (the owner's screenshot: the caps "end up poking into the things above and below them when the rows are too
			// close together"): never taller than the gap between rows allows
			const float maxH = std::max(4.0f, ImGui::GetStyle().ItemSpacing.y * 2.0f + a_bb.GetHeight());
			if (part->main.h * k > maxH) { k = maxH / part->main.h; }
			const float h = std::max(2.0f, std::round(part->main.h * k));
			const float cap = part->corner * k;
			const float cy = std::round((a_bb.Min.y + a_bb.Max.y) * 0.5f);
			const float y0 = cy - h * 0.5f, y1 = y0 + h;
			const float x0 = a_bb.Min.x, x3 = a_bb.Max.x;
			const float u1 = part->corner / part->main.w, u2 = 1.0f - u1;
			const auto tex = reinterpret_cast<ImTextureID>(part->main.srv);
			// a short piece (the bit before a heading's text) is the rail alone - two squeezed caps read as a smudge
			if (a_bb.GetWidth() < cap * 3.0f)
			{
				a_dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x3, y1), ImVec2(u1, 0.0f), ImVec2(u2, 1.0f), a_col);
				return true;
			}
			const float x1 = x0 + cap, x2 = x3 - cap;
			a_dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(0.0f, 0.0f), ImVec2(u1, 1.0f), a_col);
			if (x2 > x1) { a_dl->AddImage(tex, ImVec2(x1, y0), ImVec2(x2, y1), ImVec2(u1, 0.0f), ImVec2(u2, 1.0f), a_col); }
			a_dl->AddImage(tex, ImVec2(x2, y0), ImVec2(x3, y1), ImVec2(u2, 0.0f), ImVec2(1.0f, 1.0f), a_col);
			return true;
		}

		// An arrow: the part points right and is turned to the direction asked, filling the square ImGui would fill.
		bool DrawArrow(ImDrawList* a_dl, const ImRect& a_bb, ImU32 a_col, int a_dir)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kArrow);
			// 2.1.6: the Arrows colour (Text until picked). Only the text-coloured arrows take it - a disabled row's dim one stays.
			const bool picked = theme::RolePicked(theme::kRoleArrows) && a_col == ImGui::GetColorU32(ImGuiCol_Text);
			if (picked) { a_col = static_cast<ImU32>(theme::RoleColor(theme::kRoleArrows)); }
			if (!part)
			{
				if (!picked) { return false; }
				// the built-in triangle, in the picked colour (ImGui's own RenderArrow, scale 1)
				const ImVec2 c((a_bb.Min.x + a_bb.Max.x) * 0.5f, (a_bb.Min.y + a_bb.Max.y) * 0.5f);
				const float r = (a_bb.Max.x - a_bb.Min.x) * 0.5f * 0.40f;
				ImVec2 a, b, d;
				switch (a_dir)
				{
				case ImGuiDir_Up:    a = ImVec2(0, -0.75f); b = ImVec2(-0.866f, 0.75f); d = ImVec2(0.866f, 0.75f); break;
				case ImGuiDir_Down:  a = ImVec2(0, 0.75f); b = ImVec2(-0.866f, -0.75f); d = ImVec2(0.866f, -0.75f); break;
				case ImGuiDir_Left:  a = ImVec2(-0.75f, 0); b = ImVec2(0.75f, 0.866f); d = ImVec2(0.75f, -0.866f); break;
				default:             a = ImVec2(0.75f, 0); b = ImVec2(-0.75f, -0.866f); d = ImVec2(-0.75f, 0.866f); break;
				}
				a_dl->AddTriangleFilled(ImVec2(c.x + a.x * r, c.y + a.y * r), ImVec2(c.x + b.x * r, c.y + b.y * r), ImVec2(c.x + d.x * r, c.y + d.y * r), a_col);
				return true;
			}
			const ImVec2 a = a_bb.Min, b(a_bb.Max.x, a_bb.Min.y), c = a_bb.Max, d(a_bb.Min.x, a_bb.Max.y);
			// UVs at the four screen corners (top-left, top-right, bottom-right, bottom-left) for each turn
			ImVec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };   // right: as drawn
			switch (a_dir)
			{
			case ImGuiDir_Down: uv[0] = { 0, 1 }; uv[1] = { 0, 0 }; uv[2] = { 1, 0 }; uv[3] = { 1, 1 }; break;
			case ImGuiDir_Left: uv[0] = { 1, 1 }; uv[1] = { 0, 1 }; uv[2] = { 0, 0 }; uv[3] = { 1, 0 }; break;
			case ImGuiDir_Up:   uv[0] = { 1, 0 }; uv[1] = { 1, 1 }; uv[2] = { 0, 1 }; uv[3] = { 0, 0 }; break;
			default: break;
			}
			a_dl->AddImageQuad(reinterpret_cast<ImTextureID>(part->main.srv), a, b, c, d, uv[0], uv[1], uv[2], uv[3], a_col);
			return true;
		}

		// The frame round a popup, a dropdown list or a tooltip: a frame part, smaller than round the window.
		void DrawPopup(ImDrawList* a_dl, const ImRect& a_bb)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kPopup);
			if (!part) { return; }
			const float base = part->drawCorner > 0.0f ? part->drawCorner : part->corner;
			const float dcs = std::min({ base * Unit() * 0.6f, a_bb.GetWidth() * 0.3f, a_bb.GetHeight() * 0.3f });
			const std::uint32_t tint = theme::RoleColor(theme::kRolePopups);   // 2.1.6: its own role (Frame art until picked)
			NineSlice(a_dl, part->main, part->corner, dcs, a_bb, tint ? static_cast<ImU32>(tint) : IM_COL32_WHITE, false);
		}

		// (Skyrim's NoteIfHovered is not ported: on Witcher 3 the hovered and nav items are framed by the [AMF] highlight
		// hook in imgui.cpp - RecordGameFrame in Renderer.cpp - which already sees every item ImGui hovers.)

		bool Hook(ImDrawList* a_dl, int a_part, const ImRect& a_bb, ImU32 a_col, int a_arg)
		{
			if (!a_dl) { return false; }
			switch (a_part)
			{
			case ImGuiArtPart_Frame:
				{
					const int kind = FrameKind(a_col);
					// a_arg = RenderFrame's border flag: a box or button asks for one, a bare fill (a progress bar's) does not
					return kind >= 0 && DrawPlate(a_dl, static_cast<ArtKind>(kind), a_bb, a_col, a_arg != 0);
				}
			case ImGuiArtPart_TickBox:    return DrawPlate(a_dl, ArtKind::kTickBox, a_bb, a_col, true);
			case ImGuiArtPart_TickMark:
				{
					const skin::ArtPart* part = skin::Part(ArtKind::kTickBox);
					if (!part || !part->extra.srv) { return false; }
					const float pad = std::max(1.0f, std::floor(a_bb.GetWidth() / 8.0f));
					a_dl->AddImage(reinterpret_cast<ImTextureID>(part->extra.srv), ImVec2(a_bb.Min.x + pad, a_bb.Min.y + pad),
								   ImVec2(a_bb.Max.x - pad, a_bb.Max.y - pad), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), a_col);
					return true;
				}
			case ImGuiArtPart_SliderGrab: return a_arg == 0 && DrawPlate(a_dl, ArtKind::kSlider, a_bb, a_col);
			case ImGuiArtPart_ScrollGrab: return a_arg == ImGuiAxis_Y && DrawPlate(a_dl, ArtKind::kScrollbar, a_bb, a_col);
			case ImGuiArtPart_ScrollTrack:   // 2.1.6: its own kind (Scroll bar tracks), in the Scroll bar track colour
				return a_arg == ImGuiAxis_Y && DrawPlate(a_dl, ArtKind::kScrollTrack, a_bb, a_col);
			case ImGuiArtPart_SliderTrack:   // 2.1.6: a slider's track, its own kind and colour (the owner)
				{
					const bool picked = theme::RolePicked(theme::kRoleSliderTrack);
					if (picked)
					{
						// the track's own colour, with ImGui's hover / active shading kept as a lighter step
						const ImVec4 base = ImGui::ColorConvertU32ToFloat4(static_cast<ImU32>(theme::RoleColor(theme::kRoleSliderTrack)));
						const float lift = a_col == ImGui::GetColorU32(ImGuiCol_FrameBg) ? 0.0f : 0.08f;
						a_col = ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(base.x + lift, 1.0f), std::min(base.y + lift, 1.0f), std::min(base.z + lift, 1.0f), base.w));
					}
					if (DrawPlate(a_dl, ArtKind::kSliderTrack, a_bb, a_col, true)) { return true; }
					if (!picked) { return false; }
					const ImGuiStyle& st = ImGui::GetStyle();
					a_dl->AddRectFilled(a_bb.Min, a_bb.Max, a_col, st.FrameRounding);
					a_dl->AddRect(a_bb.Min, a_bb.Max, ImGui::GetColorU32(ImGuiCol_Border), st.FrameRounding, 0, std::max(1.0f, st.FrameBorderSize));
					return true;
				}
			case ImGuiArtPart_Separator:  return DrawSection(a_dl, a_bb, a_col);
			case ImGuiArtPart_Tab:        return DrawPlate(a_dl, ArtKind::kTab, a_bb, a_col, true);
			case ImGuiArtPart_Arrow:      return DrawArrow(a_dl, a_bb, a_col, a_arg);
			case ImGuiArtPart_Popup:      DrawPopup(a_dl, a_bb); return false;
			default:                      return false;
			}
		}
	}

	void Install()
	{
		GImGuiArtHook = &Hook;
		logger::info("art: ImGui art hook installed (boxes, buttons, tick boxes, sliders, scroll bars, section lines, tabs, "
					 "arrows, popups)");
	}

	bool DrawCursor(bool a_wanted)
	{
		const skin::ArtPart* part = skin::Part(ArtKind::kCursor);
		if (!a_wanted || !part || part->main.h <= 0.0f) { return false; }
		ImGuiIO& io = ImGui::GetIO();
		if (!ImGui::IsMousePosValid(&io.MousePos)) { return true; }
		// uDrawCorner is the pointer's height at 1080p; without it, half the art's own height (drawn at twice the size)
		const float h = (part->drawCorner > 0.0f ? part->drawCorner : part->main.h * 0.5f) * Unit();
		const float k = h / part->main.h;
		const ImVec2 at(io.MousePos.x - part->hotX * k, io.MousePos.y - part->hotY * k);
		const std::uint32_t tint = theme::RoleColor(theme::kRolePointer);   // 2.1.6: the Mouse pointer colour (white = as drawn)
		ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(part->main.srv), at,
												 ImVec2(at.x + part->main.w * k, at.y + h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
												 tint ? static_cast<ImU32>(tint) : IM_COL32_WHITE);
		return true;
	}
}
