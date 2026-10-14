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
// Engine calls are made ONLY on the game's main thread (Pump, from the PeekMessageA tick) and each is SEH-guarded. The
// guard is a log line, NOT protection (past-mistakes library 9869, and the 2026-10-14 00:23 start-up crash): the game's
// own crash handling (and TestBench's crash record) are vectored handlers, which Windows runs BEFORE any SEH frame, so a
// fault inside the engine still brings up RED's crash reporter and hangs the game although AMF's __except catches it.
// What keeps the game up is never making a call at a moment it can fault: SetVar refuses while the loading guard holds
// (start-up until a menu is up, and every load), and a write AMF makes on its own waits for Settled().
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
	// True once the bridge is known to be off for this session: resolution was tried and failed, or a call faulted.
	bool Unavailable();

	// 1.0.6 - THE LOADING GUARD (LoadGuard.h): while a_hold() answers true, Pump runs nothing - no queued job, no frame
	// hook, no settings save; all of it waits, in order, for the first Pump after it answers false. Set once at start-up.
	void SetHold(bool (*a_hold)());

	// 1.0.6 - the same hold refuses SetVar itself, whoever calls it and from whatever thread: no engine write while the game
	// is starting up or loading (a SetVarValue made 2.7 s into start-up faulted inside the engine and hung the game,
	// 2026-10-14). Settled() is the stricter gate for writes AMF makes on its own rather than for the player: the hold has
	// lifted and the first lift (the game's menu up, or the guard failing open) was at least kSettleMs ago.
	inline constexpr unsigned long long kSettleMs = 30000;
	bool Settled();

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
	// Sets it the way Options > Mods does (the game notifies its listeners). False when the bridge is off, the var is missing,
	// or the loading guard holds (nothing is written then - the caller keeps its own state and may ask again later).
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
