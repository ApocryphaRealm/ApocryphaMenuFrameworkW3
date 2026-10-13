#pragma once

// ============================================================================================================
// ONE TRANSITION PER BUTTON PER FRAME - the input drain's rule (1.0.6, the Skyrim AMF 2.2.0's), with no game or ImGui code
// so the offline check (tools\input_frames_check.cpp) runs the same function the drain does (Input.cpp ProcessQueuedEvents).
//
// ImGui runs here with input trickling OFF (Renderer.cpp - the cursor's one-position-per-frame rule), and with trickling off
// it applies a whole frame's events at once: "successive down/up/down/up will be lost" (imgui.cpp, UpdateInputEvents). The
// keyboard and mouse reach AMF as window messages (WM_LBUTTONDOWN / UP, WM_KEYDOWN / UP, or raw input's make / break), each
// queued as a record on the window thread and drained once per frame on the render thread - so a click or a key tap whose
// down and up both arrive between two frames (a hold shorter than a frame, or a press just before the frame's drain and the
// release just after the previous one) reached ImGui as a press and a release in the same NewFrame: the button ended the
// frame up and ImGui saw no click at all. Two clicks inside one frame were one click or none.
//
// So a frame may carry at most ONE press or release of each mouse button, key or pad button. Anything more of that same
// button waits for the next frame, in order, AHEAD of what arrives later - every press is seen down for at least one frame
// and every release up for one, however short the click and however close the next (the Skyrim 2.2.0 fix as 8842a99 left
// it: its first form promoted the carry behind newer input, and a click arriving a frame later merged with it).
// ============================================================================================================

#include <algorithm>
#include <cstdint>
#include <vector>

namespace inputframes
{
	// a_drain: this frame's records, oldest first, last frame's a_carry already at the front. Keeps in a_drain the first
	// record of each button (a_key(r) != 0 names the button; 0 = not a button - a move, a wheel step, a character) and
	// everything that is not a button; moves every later record of a button already seen into a_carry, in order.
	template <class R, class KeyFn>
	void OneTransitionPerFrame(std::vector<R>& a_drain, std::vector<R>& a_carry, KeyFn a_key)
	{
		a_carry.clear();
		std::vector<std::uint64_t> seen;
		std::vector<R>             kept;
		kept.reserve(a_drain.size());
		for (R& r : a_drain) {
			const std::uint64_t k = a_key(r);
			if (k != 0) {
				if (std::find(seen.begin(), seen.end(), k) != seen.end()) {
					a_carry.push_back(std::move(r));
					continue;
				}
				seen.push_back(k);
			}
			kept.push_back(std::move(r));
		}
		a_drain.swap(kept);
	}
}
