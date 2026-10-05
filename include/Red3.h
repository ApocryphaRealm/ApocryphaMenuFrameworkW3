#pragma once

// ============================================================================================================
// The engine side for The Witcher 3 5.0 (M3): the game's own mod-settings natives, called from C++ so a change made in AMF
// behaves exactly like one made in Options > Mods. Nothing is at a fixed address. Each function is found at run time:
// - a script native from its registration code: the name string, then `lea rdx,[name]` between CNamePool::Get and
//   CNamePool::Add, with the native's `lea rax,[fn]` just before;
// - what the natives call, by byte pattern inside them.
// Every result must be the start of a function in the exe's own unwind table (.pdata); otherwise the bridge stays off and
// AMF's mod pages stay read-only. Found and checked against witcher3.exe 5.0.0.1044392 on 2026-10-05
// (scratchpad re\resolve_check.py).
//
// Engine calls are made ONLY on the game's main thread (Pump, from the PeekMessageA tick) and each is SEH-guarded: a
// fault turns the bridge off for the session instead of taking the game down.
// ============================================================================================================

#include <functional>
#include <string>

namespace red3
{
	// Finds the functions in the running exe. Any thread, once, at start-up; cheap to call again.
	bool Resolve();
	// The same against a given module - tools\red3_test.cpp maps witcher3.exe as an image (no code runs) and resolves in it.
	bool ResolveIn(void* a_module);

	// True when every function was found and no engine call has faulted.
	bool ConfigReady();

	// Runs the queued jobs and any due settings save. Game main thread only (the frame tick).
	void Pump();

	// Queue a job for the game thread (any thread).
	void Post(std::function<void()> a_job);

	// Run a_hook on the game thread from every Pump (register once, at start-up). Used for work that must poll the
	// game each frame, e.g. the menu-entry request (ModMenus).
	void AddFrameHook(std::function<void()> a_hook);

	// ---- game thread only ----
	// A mod-menu var's current value as the game holds it. False when the var does not exist or the bridge is off.
	bool GetVar(const std::string& a_group, const std::string& a_var, std::string& a_out);
	// Sets it the way Options > Mods does (the game notifies its listeners). False when the bridge is off or the var is missing.
	bool SetVar(const std::string& a_group, const std::string& a_var, const std::string& a_value);
	// The game's own pause (CGame::Pause / Unpause with the reason "ApocryphaMenuFramework"), held while the menu is open
	// when bPauseGame is on. Game thread only. PauseAvailable: found, and the bridge is up.
	bool PauseAvailable();
	bool SetGamePaused(bool a_paused);

	// Ask for the settings to be written (dx12user.settings) once edits stop for a moment, as the game does when its menu closes.
	void RequestSave();

	// For amf.process op=modmenus: what was found where, and the calls made so far.
	std::string StatusJson();
}
