#pragma once

// M1 probes (4. plans\amf-witcher3\PLAN.md, open questions 3 and 4): which input paths The Witcher 3 5.0 really uses on
// this PC. The exe carries DirectInput8 AND RawInput keyboard/mouse classes and three pad classes (XInput9_1_0, GameInput,
// Sony HID) - RESEARCH-web 4. Each probe wraps the exe's own import slot, passes every call straight through, and logs
// the first call (and what it asked for) once. Nothing is blocked or changed here; M3 decides what to gate from this log.

namespace probe
{
	// Before the game's WinMain (from DllMain). Patches the exe's import slots for DINPUT8!DirectInput8Create,
	// USER32!RegisterRawInputDevices and XINPUT9_1_0!XInputGetState; the DirectInput8 object's CreateDevice is wrapped
	// when the game creates it, so the keyboard/mouse/joystick devices it opens are logged by GUID.
	void Install();

	// Game thread, about once a second (Tick.cpp): logs, once, when GameInput.dll or the HID pad stack appears.
	void Poll();
}
