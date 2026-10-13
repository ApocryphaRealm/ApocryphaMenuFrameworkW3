// Off-game check of the input drain's one-transition-per-frame rule (include/InputFrames.h - the function
// input::ProcessQueuedEvents runs) on AMF's own vendored Dear ImGui (extern/imgui, 1.90.8 docking), headless.
//
// The Witcher 3 framework takes the keyboard and mouse from window messages: WM_LBUTTONDOWN / WM_LBUTTONUP (or raw input's
// make / break for keys) become one record each on the window thread, and the render thread drains them all once per frame
// into io.AddMouseButtonEvent / io.AddKeyEvent, with io.ConfigInputTrickleEventQueue = false (Renderer.cpp). This check feeds
// those same records, timed in milliseconds against frame boundaries (60, 144 and 30 frames a second, every phase of the
// frame), through the drain - and through the old drain (everything at once) beside it - into a real ImGui frame with a
// real button under the pointer, and counts what ImGui reports: Button() returning true, IsMouseClicked, IsKeyPressed.
//   xmake build input_frames_check && xmake run input_frames_check
// Prints one line per check and "input_frames_check: N of N passed"; exits 1 on any failure.
#include "InputFrames.h"

#include <imgui.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	// Input.cpp's Record, as far as the drain and ImGui need it.
	struct Record
	{
		enum class Kind : std::uint8_t { kMouseMove, kMouseButton, kKeyboard };
		Kind          kind{};
		std::uint32_t code = 0;
		bool          down = false;
	};

	// The drain's key, exactly as ProcessQueuedEvents writes it.
	std::uint64_t ButtonKey(const Record& a_r)
	{
		const bool button = a_r.kind == Record::Kind::kMouseButton || a_r.kind == Record::Kind::kKeyboard;
		return button ? ((static_cast<std::uint64_t>(a_r.kind) + 1) << 32) | a_r.code : 0;
	}

	struct Timed
	{
		double ms;
		Record r;
	};

	struct Result
	{
		int buttonClicks = 0;   // Button() returned true
		int mouseClicks = 0;    // IsMouseClicked(0) frames
		int keyPresses = 0;     // IsKeyPressed(Enter, false) frames
		int carried = 0;        // records the drain moved to a later frame
	};

	constexpr ImVec2 kButtonPos{ 100.0f, 100.0f }, kButtonSize{ 200.0f, 60.0f };
	constexpr ImVec2 kPointer{ 200.0f, 130.0f };   // on the button

	// Runs one timeline through ImGui. a_newDrain: InputFrames.h's rule (carry first); false: the old drain - every record
	// queued since the last frame applied in one NewFrame.
	Result Run(const std::vector<Timed>& a_events, double a_frameMs, bool a_newDrain)
	{
		ImGui::GetIO().ClearInputKeys();
		ImGui::GetIO().ClearEventsQueue();
		Result out;
		std::vector<Record> carry;
		std::size_t next = 0;
		const double end = (a_events.empty() ? 0.0 : a_events.back().ms) + 400.0;   // past ImGui's double-click time
		for (double t = 0.0; t < end; t += a_frameMs) {
			// what the window thread queued before this frame's drain
			std::vector<Record> drain;
			if (a_newDrain) { drain.swap(carry); }
			while (next < a_events.size() && a_events[next].ms < t) {
				drain.push_back(a_events[next++].r);
			}
			if (a_newDrain) {
				inputframes::OneTransitionPerFrame(drain, carry, ButtonKey);
				out.carried += static_cast<int>(carry.size());
			}

			ImGuiIO& io = ImGui::GetIO();
			io.DeltaTime = static_cast<float>(a_frameMs / 1000.0);
			for (const Record& r : drain) {
				if (r.kind == Record::Kind::kMouseButton) { io.AddMouseButtonEvent(static_cast<int>(r.code), r.down); }
				else if (r.kind == Record::Kind::kKeyboard) { io.AddKeyEvent(ImGuiKey_Enter, r.down); }
			}
			io.AddMousePosEvent(kPointer.x, kPointer.y);   // the software cursor, last, as ProcessQueuedEvents does

			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f));
			ImGui::Begin("check", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			ImGui::SetCursorPos(kButtonPos);
			if (ImGui::Button("Click", kButtonSize)) { ++out.buttonClicks; }
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { ++out.mouseClicks; }
			if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) { ++out.keyPresses; }
			ImGui::End();
			ImGui::Render();
		}
		return out;
	}

	Timed Mouse(double a_ms, bool a_down) { return { a_ms, { Record::Kind::kMouseButton, 0, a_down } }; }
	Timed Key(double a_ms, bool a_down) { return { a_ms, { Record::Kind::kKeyboard, 0x1C, a_down } }; }
	Timed Move(double a_ms) { return { a_ms, { Record::Kind::kMouseMove, 0, false } }; }

	std::string Fmt(double a_v)
	{
		std::string s = std::to_string(a_v);
		s.erase(s.find_last_not_of('0') + 1);
		if (!s.empty() && s.back() == '.') { s.pop_back(); }
		return s;
	}
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1280.0f, 720.0f);
	io.ConfigInputTrickleEventQueue = false;   // as AMF sets it (Renderer.cpp)
	unsigned char* pixels = nullptr;
	int w = 0, h = 0;
	io.Fonts->AddFontDefault();
	io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
	for (int i = 0; i < 4; ++i) { Run({}, 16.6667, true); }   // a few idle frames so the pointer hovers the button

	// ---- one click / one key tap at 1, 5, 10, 20 and 30 ms holds, at every phase of the frame, 144 / 60 / 30 fps ----
	const double holds[] = { 1.0, 5.0, 10.0, 20.0, 30.0 };
	const double frames[] = { 1000.0 / 144.0, 1000.0 / 60.0, 1000.0 / 30.0 };
	for (const double frame : frames) {
		for (const double hold : holds) {
			int runs = 0, newButton = 0, newMouse = 0, newKey = 0, oldButton = 0, oldKey = 0;
			for (double phase = 0.25; phase < frame; phase += 1.0) {
				const double t0 = 50.0 + phase;
				const Result nm = Run({ Mouse(t0, true), Move(t0 + hold / 2.0), Mouse(t0 + hold, false) }, frame, true);
				const Result om = Run({ Mouse(t0, true), Move(t0 + hold / 2.0), Mouse(t0 + hold, false) }, frame, false);
				const Result nk = Run({ Key(t0, true), Key(t0 + hold, false) }, frame, true);
				const Result ok = Run({ Key(t0, true), Key(t0 + hold, false) }, frame, false);
				++runs;
				newButton += nm.buttonClicks == 1;
				newMouse += nm.mouseClicks == 1;
				newKey += nk.keyPresses == 1;
				oldButton += om.buttonClicks == 1;
				oldKey += ok.keyPresses == 1;
			}
			const std::string what = Fmt(hold) + " ms hold at " + Fmt(std::round(1000.0 / frame)) + " fps, " + std::to_string(runs) + " phases";
			Check(newButton == runs && newMouse == runs, what + ": every click registers (Button " + std::to_string(newButton) + "/" +
				std::to_string(runs) + ", IsMouseClicked " + std::to_string(newMouse) + "/" + std::to_string(runs) + "; the old drain: " +
				std::to_string(oldButton) + "/" + std::to_string(runs) + ")");
			Check(newKey == runs, what + ": every key tap registers (IsKeyPressed " + std::to_string(newKey) + "/" + std::to_string(runs) +
				"; the old drain: " + std::to_string(oldKey) + "/" + std::to_string(runs) + ")");
		}
	}

	// ---- the old drain really lost them (the defect this fixes, on this ImGui) ----
	{
		const Result om = Run({ Mouse(50.3, true), Mouse(51.3, false) }, 1000.0 / 60.0, false);
		const Result nm = Run({ Mouse(50.3, true), Mouse(51.3, false) }, 1000.0 / 60.0, true);
		Check(om.buttonClicks == 0 && om.mouseClicks == 0, "press and release in one frame, old drain: ImGui saw no click (the defect)");
		Check(nm.buttonClicks == 1 && nm.mouseClicks == 1, "press and release in one frame, new drain: one click");
	}
	// ---- two clicks inside one frame are two clicks, not one or none ----
	{
		const std::vector<Timed> two{ Mouse(50.2, true), Mouse(51.0, false), Mouse(53.0, true), Mouse(54.0, false) };
		const Result nm = Run(two, 1000.0 / 60.0, true);
		const Result om = Run(two, 1000.0 / 60.0, false);
		Check(nm.buttonClicks == 2 && nm.mouseClicks == 2, "two clicks in one frame: two clicks (Button " + std::to_string(nm.buttonClicks) +
			"; the old drain " + std::to_string(om.buttonClicks) + ")");
		const std::vector<Timed> keys{ Key(50.2, true), Key(51.0, false), Key(53.0, true), Key(54.0, false) };
		Check(Run(keys, 1000.0 / 60.0, true).keyPresses == 2, "two key taps in one frame: two presses");
	}
	// ---- a click arriving the frame after another is not merged with it ----
	{
		const double f = 1000.0 / 60.0;
		const std::vector<Timed> seq{ Mouse(50.0 + 1.0, true), Mouse(50.0 + 2.0, false),         // inside frame N
		                              Mouse(50.0 + f + 1.0, true), Mouse(50.0 + f + 2.0, false) };   // inside frame N+1
		const Result nm = Run(seq, f, true);
		Check(nm.buttonClicks == 2 && nm.mouseClicks == 2, "a click the frame after another: two clicks, not merged (Button " +
			std::to_string(nm.buttonClicks) + ")");
		// a fast run of five clicks, 6 ms apart, 3 ms holds - some frames carry two whole clicks
		std::vector<Timed> run;
		for (int i = 0; i < 5; ++i) { run.push_back(Mouse(50.5 + i * 6.0, true)); run.push_back(Mouse(53.5 + i * 6.0, false)); }
		const Result r5 = Run(run, f, true);
		Check(r5.buttonClicks == 5 && r5.mouseClicks == 5, "five clicks 6 ms apart: five clicks (Button " + std::to_string(r5.buttonClicks) + ")");
	}
	// ---- other input is never held: a move and another button go through on their own frame ----
	{
		std::vector<Record> drain{ { Record::Kind::kMouseButton, 0, true }, { Record::Kind::kMouseMove, 0, false },
		                           { Record::Kind::kMouseButton, 0, false }, { Record::Kind::kMouseButton, 1, true },
		                           { Record::Kind::kKeyboard, 0x1C, true }, { Record::Kind::kMouseMove, 0, false } };
		std::vector<Record> carry;
		inputframes::OneTransitionPerFrame(drain, carry, ButtonKey);
		Check(drain.size() == 5 && carry.size() == 1 && !carry[0].down && carry[0].code == 0,
			"only the left button's second change waits; the moves, the right button and the key go through this frame");
	}
	// ---- a long hold is untouched: nothing is carried, the click lands on release as always ----
	{
		const Result nm = Run({ Mouse(50.0, true), Mouse(250.0, false) }, 1000.0 / 60.0, true);
		Check(nm.buttonClicks == 1 && nm.carried == 0, "a 200 ms hold: one click, nothing carried");
	}

	ImGui::DestroyContext();
	std::cout << "input_frames_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
