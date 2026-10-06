#pragma once

// The on/off toggle switch every boolean in this project renders as instead of a tick-box
// (CLAUDE.md rule 32). Same visual design as the vendored SMF-page version in the mod repos
// (utils/Toggle.h there): red/green pill track, sliding circular knob, label to the right -
// but written against the real embedded Dear ImGui API rather than SMF's cimgui exports,
// because this framework owns its ImGui.

#include "Skin.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGui::RenderNavHighlight, ImRect

#include <string_view>

namespace widgets
{
	// a_readOnly: drawn and reachable like any toggle, but a press only reports itself - the value never flips (a setting
	// AMF shows but cannot change yet, e.g. a Witcher 3 mod menu before the engine layer).
	inline bool Toggle(const char* a_label, bool* a_value, bool a_readOnly = false)
	{
		ImGui::PushID(a_label);

		const float height = ImGui::GetFrameHeight();
		const float width = height * 2.0f;
		const float radius = height * 0.5f;

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const bool changed = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();
		const ImGuiID navId = ImGui::GetItemID();

		if (changed && a_value && !a_readOnly)
		{
			*a_value = !*a_value;
		}

		const bool isOn = a_value && *a_value;

		// GREYED WHEN DISABLED (W3 1.0.0 - Main Agent's run: the Mods row's A-Z/Z-A switch, inside BeginDisabled while the
		// tick box is off, still drew bright green with a white knob and looked live). The colours here are fixed, so ImGui's
		// disabled alpha never reached them: a disabled switch draws a grey track and a dimmed knob, faded like other
		// greyed controls, with its position still showing which way it is set.
		const bool  disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
		const float fade = disabled ? ImGui::GetStyle().DisabledAlpha : 1.0f;
		const ImU32 trackColor = disabled ? IM_COL32(110, 110, 110, static_cast<int>(255 * fade))
		                       : isOn ? (hovered ? IM_COL32(92, 191, 96, 255) : IM_COL32(76, 175, 80, 255))
		                              : (hovered ? IM_COL32(207, 84, 84, 255) : IM_COL32(191, 68, 68, 255));
		const ImU32 knobColor = disabled ? IM_COL32(170, 170, 170, static_cast<int>(255 * fade)) : IM_COL32(240, 240, 240, 255);

		const float knobX = pos.x + radius + (isOn ? (width - height) : 0.0f);

		// A UI author may supply toggle.png to restyle the track (see Skin.h). The knob still
		// draws over it, so the switch remains readable as a switch whatever the art does, and
		// the on/off tint is still applied - the plate carries the SHAPE, the state stays legible.
		if (skin::HasPlate(skin::Plate::kToggle))
		{
			drawList->AddImage(reinterpret_cast<ImTextureID>(skin::PlateTexture(skin::Plate::kToggle)),
							   pos, ImVec2(pos.x + width, pos.y + height),
							   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), trackColor);
		}
		else
		{
			drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), trackColor, radius);
		}
		drawList->AddCircleFilled(ImVec2(knobX, pos.y + radius), radius - 2.0f, knobColor, 32);

		ImGui::PopID();

		// "##" onward is an ID disambiguator, not part of the visible label.
		std::string_view visible;
		if (a_label)
		{
			const std::string_view label{ a_label };
			const size_t hashPos = label.find("##");
			visible = (hashPos == std::string_view::npos) ? label : label.substr(0, hashPos);
		}

		// THE NAV FRAME (Witcher 3 AMF, a tester, 2026-10-05: the controller highlight on a toggle row was too faint to see
		// when the first D-pad Down landed on it, while a slider shows a clear blue frame). An InvisibleButton draws no nav
		// highlight at all - only ImGui's framed widgets do - so the switch had none of its own. It now gets the slider's:
		// ImGui's own RenderNavHighlight (the theme's NavHighlight colour, 2 px, the same offset), drawn round the track
		// AND its label so the whole row reads as the one under the highlight. Drawn only when the highlight is on this
		// item and ImGui is showing nav highlights (a mouse user sees none), exactly when a slider shows its frame.
		{
			const float labelWidth = visible.empty() ? 0.0f
				: ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(visible.data(), visible.data() + visible.size()).x;
			ImGui::RenderNavHighlight(ImRect(pos, ImVec2(pos.x + width + labelWidth, pos.y + height)), navId);
		}

		if (!visible.empty())
		{
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%.*s", static_cast<int>(visible.size()), visible.data());
		}

		return changed;
	}
}
