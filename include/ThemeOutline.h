#pragma once

// ============================================================================================
// EVERY CONTROL HAS A VISIBLE OUTLINE (1.0.6, from Skyrim 2.2.0 - the owner, 2026-10-12, on Norden - Black: "the button
// faces don't have any kind of outline at all ... everything should have a basic minimal outline so that it's at least
// visible. And then the detailed art styled frame that goes around it when it's focused or hovered").
//
// A control's face is the panel colour at rest (Theme.cpp: kRoleBoxes follows kRoleBackground), so a box, button or tick box
// is told from the panel by its outline alone. The outline is the theme's Border colour, at least kMinOutlineWidth wide;
// theme::Apply uses the Text colour instead when the Border colour is under kMinOutlineContrast against the panel or a face.
//
// kMinOutlineContrast is 4.5 (Skyrim's v4 floor; it began at 2.0). WCAG's 3:1 (non-text contrast) is the floor for a
// component's boundary, but this outline is a one-pixel hairline that does not grow with the display, and a stroke that thin
// reads like text, so it is held to text's 4.5:1.
//
// Only ImGui's types here, so the offline check (tools/theme_style_check.cpp) runs the same sums theme::Apply does.
// ============================================================================================

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace themeoutline
{
	inline constexpr float kMinOutlineWidth = 1.0f;
	inline constexpr float kMinOutlineContrast = 4.5f;

	// the outline width for a theme's line width: never thinner than one pixel
	inline float OutlineWidth(float a_borderThickness) { return std::max(kMinOutlineWidth, a_borderThickness); }

	// WCAG relative luminance of an sRGB colour (alpha ignored)
	inline float Luminance(const ImVec4& a_c)
	{
		auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
		return 0.2126f * lin(a_c.x) + 0.7152f * lin(a_c.y) + 0.0722f * lin(a_c.z);
	}

	// a colour as it shows over an opaque one
	inline ImVec4 Over(const ImVec4& a_top, const ImVec4& a_under)
	{
		const float a = std::clamp(a_top.w, 0.0f, 1.0f);
		return ImVec4{ a_top.x * a + a_under.x * (1.0f - a), a_top.y * a + a_under.y * (1.0f - a), a_top.z * a + a_under.z * (1.0f - a), 1.0f };
	}

	// The contrast (1 = identical, 21 = black on white) of a line drawn on a face that sits on the panel; the panel is taken
	// as drawn over black.
	inline float OutlineContrast(const ImVec4& a_line, const ImVec4& a_face, const ImVec4& a_panel)
	{
		const ImVec4 panel = Over(a_panel, ImVec4{ 0.0f, 0.0f, 0.0f, 1.0f });
		const ImVec4 face = Over(a_face, panel);
		const float l1 = Luminance(Over(a_line, face)), l2 = Luminance(face);
		return (std::max(l1, l2) + 0.05f) / (std::min(l1, l2) + 0.05f);
	}

	// The worst contrast of the line against the panel and the two control faces (boxes, buttons).
	inline float WorstContrast(const ImVec4& a_line, const ImVec4& a_panel, const ImVec4& a_boxFace, const ImVec4& a_buttonFace)
	{
		return std::min({ OutlineContrast(a_line, a_panel, a_panel), OutlineContrast(a_line, a_boxFace, a_panel),
						  OutlineContrast(a_line, a_buttonFace, a_panel) });
	}
}
