#pragma once

// ============================================================================================================
// THE LOADING GUARD (1.0.6, the Skyrim AMF 2.2.0's; the owner, 2026-10-12: "Honestly amf shouldn't work during loading in as
// that can cause issues so build that in"). While the game is loading - start-up until a menu is really up, a save load, a
// new game, a world change, a fast travel - AMF does nothing: the menu does not open (F1, the game-menu entry, DevBench
// op=open, AMF_OpenMenu, an external launcher), no AMF key is handled (the menu key, Start, a rebind or key capture), nothing
// is taken from the game's input (keys, mouse, raw input and the pad all reach the game untouched), and the engine bridge
// runs nothing it has queued (SetVarValue, SaveUserSettings, the pause - they wait for the guard to lift). If the menu is
// open when a load starts it closes ("Framework window hidden (closed by the loading guard)"). The rules and the Witcher 3
// events that drive them are in LoadGuardCore.h.
// ============================================================================================================

#include <string>

namespace loadguard
{
	// The game thread, every frame tick (main.cpp, before red3::Pump): reads the framework script's signal through the engine
	// bridge and moves the guard. Reads only - nothing is written to the game.
	void OnGameThreadFrame();

	// True while the game is loading or has not reached a menu yet this session. Any thread; a lock and a few fields. Also
	// applies the fail-open rule, so a stretch with no ending event lets go even if the game thread stops reading.
	bool Blocked();

	// The render thread, every frame: true when an open menu must close now (the guard holds, or a load began and ended
	// between two reads). Consumed each call.
	bool TakeClose();

	// An input or an open request was ignored because of the guard: one debug line per stretch of loading, naming the first.
	void NoteIgnored(const char* a_what);

	// DevBench amf.menu op=state: {"active", "reason", "since" (seconds in this state), "edges", "failOpens", "lastEdge",
	// "signal" ("waiting" until the script's settings are read, "script", or "off" with no engine bridge)}.
	std::string StateJson();
}
