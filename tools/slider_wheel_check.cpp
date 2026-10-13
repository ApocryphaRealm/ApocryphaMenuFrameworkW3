// Off-game check of the mouse wheel on a slider (AMF-W3 1.0.6, from Skyrim AMF 2.2.0's tools/slider_wheel_check - the owner,
// testing a mod's menu there: the wheel did not move a slider after clicking it). Runs AMF-W3's own vendored Dear ImGui
// (extern/imgui, 1.90.8 docking with the [AMF] patches - the xmake "imgui" target) headless, with the framework's input rules
// (trickling off, the pointer published once per frame).
//   xmake build slider_wheel_check && xmake run slider_wheel_check
// One line per check and "slider_wheel_check: N of N passed"; exits 1 on any failure.
// What it proves: the step a consumer's format gives (precise::FormatStep - what amf_ConsumerSliderWheel uses for every
// igSlider* export), the step-and-clamp arithmetic (precise::WheelStepped), and on live ImGui frames that one notch over
// an AMF slider (precise::SliderFloat / SliderInt) and over consumer-shaped sliders (ImGui::SliderScalar, SliderAngle and
// VSliderFloat followed by the wheel helper, exactly what the hand-edited cimgui exports do) moves it one step, clamps at the
// ends, returns true and marks the item edited, and that the page does not scroll while the pointer is on a slider but still
// scrolls when it is not. Section 6: a slider that applies on IsItemDeactivatedAfterEdit - AMF's Text size, a consumer
// through igIsItemDeactivatedAfterEdit - applies a wheel change exactly once per settled run of notches. Section 7 (W3): a
// mod-menu page's slider (src/ModMenus.cpp - precise::SliderFloat with "%.<decimals>f", written to the game on change) gets
// one write per notch with the stepped value, and none while the page is read-only (the engine bridge not up). Section 2b
// (the value outline, HadToRegister): precise::SliderFloat's draw list holds eight copies of the value in the window colour,
// measured against a plain slider's value text, and the value in the text colour on top of them.
// What it cannot: the game's own wheel messages (Input.cpp turns WM_MOUSEWHEEL into io.AddMouseWheelEvent, and keeps the
// wheel from ImGui over a mod's tab bar when [Menu] bWheelSwitchesTabs is on - no slider is ever on that bar).
#include "PreciseSlider.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iostream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}
	bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

	float  g_text = 1.5f;      // AMF's Text size: precise::SliderFloat 1..2 "%.2f"
	int    g_opacity = 50;     // AMF's Window opacity: precise::SliderInt 5..100 "%d%%"
	int    g_count = 3;        // a consumer's igSliderInt 0..10 "%d"
	double g_scale = 0.5;      // a consumer's igSliderScalar (double) 0..1 "%.1f"
	float  g_angle = 45.0f * (2.0f * IM_PI) / 360.0f;   // a consumer's igSliderAngle -180..180, "%.0f deg"
	float  g_vert = 0.30f;     // a consumer's igVSliderFloat 0..1 "%.2f"
	bool   g_retText = false, g_retCount = false, g_retScale = false, g_retAngle = false, g_retVert = false, g_editedText = false;
	ImRect g_rectText, g_rectCount, g_rectScale, g_rectAngle, g_rectVert, g_rectMod;
	float  g_scrollY = 0.0f;
	ImVec2 g_pointer{ 5, 5 };
	bool   g_mouseDown = false;
	int    g_appliedText = 0, g_appliedCount = 0;   // release-applies: IsItemDeactivatedAfterEdit after the slider, as Renderer.cpp's Text size
	int    g_outlineVerts = 0, g_outlineEdgeVerts = 0, g_outlineTextVerts = 0;   // what precise::SliderFloat added after ImGui's slider
	int    g_plainTextVerts = 0;                                                  // ImGui's own value text in a plain slider
	int    g_labelTextVerts = 0;                                                  // the label "Text size" as text
	std::string g_lastValueText;                                                  // the value text the outline drew
	int    g_textAfterOutline = 0;                                                // text-colour vertices after the last outline copy

	// a mod-menu page's slider (src/ModMenus.cpp): the value read from the live text each frame, written back on change
	std::string g_modLive = "0.50";
	int         g_modWrites = 0;
	bool        g_modReadOnly = false;
	bool        g_modMoved = false;   // precise::SliderFloat's result, read-only or not
	float       g_modShown = 0.0f;    // the value the slider holds after the call
	constexpr int kModDecimals = 2;

	void BuildFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg;
		cfg.SizePixels = 16.0f;
		io.Fonts->AddFontDefault(&cfg);
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}

	void Frame(float a_wheel = 0.0f)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (a_wheel != 0.0f) { io.AddMouseWheelEvent(0.0f, a_wheel); }
		io.AddMousePosEvent(g_pointer.x, g_pointer.y);
		io.AddMouseButtonEvent(0, g_mouseDown);
		io.DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(100, 100));
		ImGui::SetNextWindowSize(ImVec2(1600, 900));
		ImGui::Begin("###amf-main", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextUnformatted("Appearance");
		ImGui::SetNextItemWidth(600);
		ImDrawList* const dl = ImGui::GetWindowDrawList();
		{
			// a plain ImGui slider with the same value and format: the vertices its value text takes, the yardstick below
			float same = g_text;
			ImGui::SetNextItemWidth(600);
			ImGui::PushID("plain");
			const int before = dl->VtxBuffer.Size;
			ImGui::SliderFloat("##plain", &same, 1.0f, 2.0f, "%.2f");   // the yardstick only - AMF's pages draw through precise::
			const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
			g_plainTextVerts = 0;
			for (int i = before; i < dl->VtxBuffer.Size; ++i) { if (dl->VtxBuffer[i].col == text) { ++g_plainTextVerts; } }
			// and the label "Text size" as ImGui draws it beside a slider (precise::SliderFloat's item has one)
			const int labelBefore = dl->VtxBuffer.Size;
			ImGui::TextUnformatted("Text size");
			g_labelTextVerts = 0;
			for (int i = labelBefore; i < dl->VtxBuffer.Size; ++i) { if (dl->VtxBuffer[i].col == text) { ++g_labelTextVerts; } }
			ImGui::PopID();
		}
		ImGui::SetNextItemWidth(600);
		const int textBefore = dl->VtxBuffer.Size;
		g_retText = precise::SliderFloat("Text size", &g_text, 1.0f, 2.0f, "%.2f");
		{
			// the value outline: precise::SliderFloat's own draw counted - ImGui's value text in the text colour, then eight
			// outline copies in the window colour, then the value again in the text colour
			ImVec4 edgeC = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			edgeC.w = ImGui::GetStyle().Colors[ImGuiCol_Text].w;
			const ImU32 edge = ImGui::GetColorU32(edgeC), text = ImGui::GetColorU32(ImGuiCol_Text);
			g_outlineVerts = dl->VtxBuffer.Size - textBefore;
			g_outlineEdgeVerts = g_outlineTextVerts = 0;
			int lastEdge = -1;
			for (int i = textBefore; i < dl->VtxBuffer.Size; ++i)
			{
				if (dl->VtxBuffer[i].col == edge) { ++g_outlineEdgeVerts; lastEdge = i; }
				else if (dl->VtxBuffer[i].col == text) { ++g_outlineTextVerts; }
			}
			g_textAfterOutline = 0;
			for (int i = lastEdge + 1; lastEdge >= 0 && i < dl->VtxBuffer.Size; ++i) { if (dl->VtxBuffer[i].col == text) { ++g_textAfterOutline; } }
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%.2f", g_text);
			g_lastValueText = buf;
		}
		g_editedText = ImGui::IsItemEdited();
		if (ImGui::IsItemDeactivatedAfterEdit()) { ++g_appliedText; }   // Renderer.cpp: save + rebuild the font
		g_rectText = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		ImGui::SetNextItemWidth(600);
		precise::SliderInt("Window opacity", &g_opacity, 5, 100, "%d%%");
		// the hand-edited igSliderInt / igSliderScalar bodies: ImGui's slider, then the wheel with the format's step
		{
			int lo = 0, hi = 10;
			ImGui::SetNextItemWidth(600);
			const bool r = ImGui::SliderScalar("Count", ImGuiDataType_S32, &g_count, &lo, &hi, "%d");   // what igSliderInt reaches
			const bool w = precise::WheelNudge(ImGuiDataType_S32, &g_count, &lo, &hi, precise::FormatStep("%d", true));
			g_retCount = r || w;
			if (ImGui::IsItemDeactivatedAfterEdit()) { ++g_appliedCount; }   // a consumer's igIsItemDeactivatedAfterEdit
			g_rectCount = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		{
			const double lo = 0.0, hi = 1.0;
			ImGui::SetNextItemWidth(600);
			const bool r = ImGui::SliderScalar("Scale", ImGuiDataType_Double, &g_scale, &lo, &hi, "%.1f");
			const bool w = precise::WheelNudge(ImGuiDataType_Double, &g_scale, &lo, &hi, precise::FormatStep("%.1f", false));
			g_retScale = r || w;
			g_rectScale = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		{
			// igSliderAngle: radians held, degrees shown
			ImGui::SetNextItemWidth(600);
			const bool r = ImGui::SliderAngle("Angle", &g_angle, -180.0f, 180.0f, "%.0f deg");
			const bool w = precise::WheelNudgeAngle(&g_angle, -180.0f, 180.0f, "%.0f deg");
			g_retAngle = r || w;
			g_rectAngle = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		{
			// src/ModMenus.cpp's SLIDER: read the live value, precise::SliderFloat, write on change unless read-only
			float v = static_cast<float>(std::strtod(g_modLive.c_str(), nullptr));
			const std::string format = std::format("%.{}f", kModDecimals);
			ImGui::SetNextItemWidth(600);
			ImGui::PushItemFlag(ImGuiItemFlags_ReadOnly, g_modReadOnly);
			const bool moved = precise::SliderFloat("##Mods.Var", &v, 0.0f, 1.0f, format.c_str());
			ImGui::PopItemFlag();
			g_modMoved = moved;
			g_modShown = v;
			if (moved && !g_modReadOnly) { g_modLive = std::format("{:.{}f}", v, kModDecimals); ++g_modWrites; }
			g_rectMod = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		{
			float lo = 0.0f, hi = 1.0f;
			const bool r = ImGui::VSliderFloat("##vert", ImVec2(40, 160), &g_vert, lo, hi, "%.2f");
			const bool w = precise::WheelNudge(ImGuiDataType_Float, &g_vert, &lo, &hi, precise::FormatStep("%.2f", false));
			g_retVert = r || w;
			g_rectVert = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		ImGui::Dummy(ImVec2(10, 4000));   // a page long enough to scroll
		g_scrollY = ImGui::GetScrollY();
		ImGui::End();
		ImGui::Render();
	}

	ImVec2 Mid(const ImRect& r) { return ImVec2((std::min)(r.Min.x + 100.0f, (r.Min.x + r.Max.x) * 0.5f), (r.Min.y + r.Max.y) * 0.5f); }
	void Idle(int n) { for (int i = 0; i < n; ++i) Frame(); }
	void ScrollTop()
	{
		Idle(30);
		g_pointer = ImVec2(1500, 900);
		for (int i = 0; i < 40 && g_scrollY > 0.0f; ++i) { Frame(+1.0f); Frame(); }
	}
}

int main()
{
	// 1. the step from a consumer's format
	Check(Near(precise::FormatStep("%.2f", false), 0.01), "\"%.2f\" steps 0.01");
	Check(Near(precise::FormatStep("%d", false), 1.0), "\"%d\" steps 1");
	Check(Near(precise::FormatStep("%.0f", false), 1.0), "\"%.0f\" steps 1");
	Check(Near(precise::FormatStep("%.3f deg", false), 0.001), "\"%.3f deg\" steps 0.001");
	Check(Near(precise::FormatStep("%5.1f cm", false), 0.1), "\"%5.1f cm\" (a width) steps 0.1");
	Check(Near(precise::FormatStep("100%% at %.2f", false), 0.01), "a literal %% before the number is skipped");
	Check(Near(precise::FormatStep("%d%%", true), 1.0), "an integer slider steps 1 whatever its format");
	Check(Near(precise::FormatStep("%f", false), 0.001) && Near(precise::FormatStep(nullptr, false), 0.001), "no precision or no format: 0.001, ImGui's own %.3f");
	Check(Near(precise::Step("%.2f"), 0.01) && Near(precise::Step("%d%%"), 1.0), "AMF's own sliders keep the key-nudge step (precise::Step)");

	// 2. one step, on the shown grid, clamped
	Check(Near(precise::WheelStepped(1.50, 0.01, +1, 1.0, 2.0), 1.51), "1.50 up one step is 1.51");
	Check(Near(precise::WheelStepped(1.50, 0.01, -1, 1.0, 2.0), 1.49), "1.50 down one step is 1.49");
	Check(Near(precise::WheelStepped(0.126, 0.01, +1, 0.0, 1.0), 0.14), "0.126 (shown 0.13) up is 0.14 - onto the shown grid");
	Check(Near(precise::WheelStepped(2.0, 0.01, +1, 1.0, 2.0), 2.0), "up at the maximum stays at the maximum");
	Check(Near(precise::WheelStepped(1.0, 0.01, -1, 1.0, 2.0), 1.0), "down at the minimum stays at the minimum");
	Check(Near(precise::WheelStepped(5.0, 1.0, +1, 10.0, 0.0), 6.0) && Near(precise::WheelStepped(10.0, 1.0, +1, 10.0, 0.0), 10.0), "a range given max-first clamps the same");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(3200, 1800);
	io.IniFilename = nullptr;
	io.ConfigInputTrickleEventQueue = false;   // Renderer.cpp
	BuildFonts();
	Frame();
	Frame();

	// 2b. the value outline over the grab (HadToRegister): eight copies in the window colour under the value in the text colour
	Check(g_plainTextVerts > 0, "a plain slider's value text: " + std::to_string(g_plainTextVerts) + " vertices in the text colour");
	Check(g_outlineEdgeVerts == 8 * g_plainTextVerts,
		  "precise::SliderFloat draws eight outline copies of the value in the window colour: " + std::to_string(g_outlineEdgeVerts) +
			  " vertices = 8 x " + std::to_string(g_plainTextVerts));
	Check(g_labelTextVerts > 0 && g_outlineTextVerts == 2 * g_plainTextVerts + g_labelTextVerts,
		  "and the value in the text colour twice - ImGui's own, then on top of the outline - beside its label: " +
			  std::to_string(g_outlineTextVerts) + " vertices = 2 x " + std::to_string(g_plainTextVerts) + " + " + std::to_string(g_labelTextVerts));
	Check(g_textAfterOutline == g_plainTextVerts,
		  "the value in the text colour is drawn after (over) the outline: " + std::to_string(g_textAfterOutline) +
			  " vertices after the last outline copy (value " + g_lastValueText + ")");

	// 3. AMF's own slider: hover, then a notch each way
	g_pointer = Mid(g_rectText);
	Frame();
	Frame(+1.0f);
	Check(std::fabs(g_text - 1.51f) < 1e-5f, "wheel up over Text size: 1.50 -> " + std::to_string(g_text));
	Check(g_retText && g_editedText, "the slider call returns true and the item is edited, as a drag");
	Check(g_scrollY == 0.0f, "the page did not scroll under the slider");
	Frame(-1.0f);
	Frame(-1.0f);
	Check(std::fabs(g_text - 1.49f) < 1e-5f, "two notches down: 1.51 -> " + std::to_string(g_text));
	g_text = 2.0f;
	Frame(+1.0f);
	Check(g_text == 2.0f && !g_retText, "up at 2.00 stays 2.00 and reports no change");
	Frame();
	Check(!g_retText, "no notch, no change");

	// 4. the consumer shapes: igSliderInt "%d", igSliderScalar double "%.1f", igSliderAngle "%.0f deg", igVSliderFloat "%.2f"
	g_pointer = Mid(g_rectCount);
	Frame();
	Frame(+1.0f);
	Check(g_count == 4 && g_retCount, "consumer igSliderInt: 3 -> " + std::to_string(g_count) + ", returns true");
	g_count = 10;
	Frame(+1.0f);
	Check(g_count == 10 && !g_retCount, "consumer igSliderInt clamps at 10");
	g_pointer = Mid(g_rectScale);
	Frame();
	Frame(-1.0f);
	Check(std::fabs(g_scale - 0.4) < 1e-9 && g_retScale, "consumer igSliderScalar (double, \"%.1f\"): 0.5 -> " + std::to_string(g_scale));
	g_pointer = Mid(g_rectAngle);
	Frame();
	Frame(+1.0f);
	const float deg = g_angle * 360.0f / (2.0f * IM_PI);
	Check(std::fabs(deg - 46.0f) < 1e-3f && g_retAngle, "consumer igSliderAngle (\"%.0f deg\"): 45 -> " + std::to_string(deg) + " degrees");
	g_pointer = Mid(g_rectVert);
	Frame();
	Frame(+1.0f);
	Check(std::fabs(g_vert - 0.31f) < 1e-5f && g_retVert, "consumer igVSliderFloat (\"%.2f\"): 0.30 -> " + std::to_string(g_vert));
	Check(g_scrollY == 0.0f, "still no page scroll over the sliders");

	// 5. off the sliders the wheel scrolls the page as before, and moves no slider
	const float textBefore = g_text;
	const int countBefore = g_count;
	g_pointer = ImVec2(1500, 900);   // inside the window, right of every slider
	Frame();
	Frame();
	Frame(-1.0f);
	Frame();
	Check(g_scrollY > 0.0f, "wheel over the page (not a slider) scrolls it: scrollY " + std::to_string(g_scrollY));
	Check(g_text == textBefore && g_count == countBefore, "and no slider moved");

	// 6. a release-applying slider applies a wheel change once the wheel rests (precise::WheelSettle, 0.25 s)
	ScrollTop();
	Check(g_scrollY == 0.0f, "page back at the top for section 6");
	Idle(2);
	g_appliedText = g_appliedCount = 0;
	g_text = 1.5f;
	g_pointer = Mid(g_rectText);
	Idle(2);
	Frame(+1.0f);
	Check(g_retText && g_appliedText == 0, "one notch on Text size: the call returns true; no release yet");
	Idle(10);   // 0.17 s
	Check(g_appliedText == 0, "still settling at 0.17 s: not applied yet");
	Idle(10);   // 0.33 s
	Check(g_appliedText == 1, "rested 0.25 s: IsItemDeactivatedAfterEdit fired, applied " + std::to_string(g_appliedText) + " time(s)");
	Idle(60);
	Check(g_appliedText == 1, "and only once (" + std::to_string(g_appliedText) + " after another second)");

	g_appliedText = 0;
	for (int i = 0; i < 5; ++i) Frame(+1.0f);   // a fast spin: five notches on five frames
	Check(std::fabs(g_text - 1.56f) < 1e-5f && g_appliedText == 0, "five fast notches: 1.51 -> " + std::to_string(g_text) + ", nothing applied mid-spin");
	Idle(20);
	Check(g_appliedText == 1, "the spin settles into ONE apply, not five: " + std::to_string(g_appliedText));

	g_appliedText = 0;
	for (int i = 0; i < 3; ++i) { Frame(-1.0f); Idle(10); }   // three notches 0.18 s apart
	Idle(20);
	Check(g_appliedText == 1 && std::fabs(g_text - 1.53f) < 1e-5f, "three slow notches (each under 0.25 s apart): one apply at " + std::to_string(g_text) + ", applied " + std::to_string(g_appliedText));

	g_appliedText = 0;
	g_text = 2.0f;
	Frame(+1.0f);
	Idle(30);
	Check(g_appliedText == 0, "a notch that changes nothing (at the maximum) applies nothing");

	g_appliedText = 0;
	g_text = 1.5f;
	Frame(+1.0f);
	Idle(3);
	g_pointer.x = g_rectText.Min.x + 596.0f;   // grabbed before it settles: press at the far end of the 600 px frame, then let go
	g_mouseDown = true;
	Frame();
	Frame();
	g_mouseDown = false;
	Frame();
	Idle(30);
	Check(g_appliedText == 1 && g_text > 1.9f, "a notch then a grab before it settles: one apply, by the release (" + std::to_string(g_appliedText) + ", " + std::to_string(g_text) + ")");

	// the consumer shape: igSliderInt then igIsItemDeactivatedAfterEdit
	g_count = 3;
	g_pointer = Mid(g_rectCount);
	Idle(2);
	Frame(+1.0f);
	Frame(+1.0f);
	Check(g_count == 5 && g_appliedCount == 0, "consumer igSliderInt: two notches, 3 -> " + std::to_string(g_count) + ", not applied mid-spin");
	Idle(20);
	Check(g_appliedCount == 1, "consumer igIsItemDeactivatedAfterEdit fires once when the wheel rests: " + std::to_string(g_appliedCount));
	Idle(30);
	Check(g_appliedCount == 1, "and not again");

	// 7. (W3) a mod-menu page's slider: written to the game once per notch, as a drag writes; never while read-only
	ScrollTop();
	g_pointer = Mid(g_rectMod);
	Idle(2);
	g_modWrites = 0;
	g_modLive = "0.50";
	Frame(+1.0f);
	Check(g_modWrites == 1 && g_modLive == "0.51", "mod page slider: one notch up writes once, \"0.50\" -> \"" + g_modLive + "\"");
	Frame(-1.0f);
	Frame(-1.0f);
	Check(g_modWrites == 3 && g_modLive == "0.49", "two notches down: two more writes, now \"" + g_modLive + "\"");
	Check(g_scrollY == 0.0f, "the page did not scroll under the mod page's slider");
	g_modReadOnly = true;
	Idle(2);
	Frame(+1.0f);
	const bool roMoved = g_modMoved;
	const float roShown = g_modShown;
	Frame();
	Check(!roMoved && std::fabs(roShown - 0.49f) < 1e-5f, "read-only (the engine bridge not up): a notch does not move the slider (" + std::to_string(roShown) + ")");
	Check(g_modWrites == 3 && g_modLive == "0.49", "read-only (the engine bridge not up): a notch writes nothing, still \"" + g_modLive + "\"");
	g_modReadOnly = false;

	ImGui::DestroyContext();
	std::printf("slider_wheel_check: %d of %d passed\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
