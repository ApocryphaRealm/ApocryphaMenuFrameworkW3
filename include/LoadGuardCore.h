#pragma once

// ============================================================================================================
// THE LOADING GUARD - the part with no game in it, so tools\load_guard_check.cpp runs the same code offline (1.0.6, the
// Skyrim AMF 2.2.0's guard brought over; the owner, 2026-10-12: "Honestly amf shouldn't work during loading in as that can
// cause issues so build that in").
//
// One question, "is the game loading, or not yet at its main menu?", and every AMF input path, its open-menu paths and the
// engine bridge's queue ask it (LoadGuard.h). It is answered from EVENTS, never from a frame count.
//
// THE WITCHER 3 HAS NO MENU EVENT FOR A NATIVE MOD (Skyrim's guard watches the Loading Menu's MenuOpenCloseEvent). The
// events here are the game's own script events, which the framework's script wraps (annotations only - Mods\
// modApocryphaMenuFramework\content\scripts\local\ApocryphaMenuFramework.ws, "THE LOADING GUARD"). At each one it writes the
// hidden setting ApocryphaMenuFramework.LoadGuard ("loading", "game" or "menu") and then raises LoadGuardSeq by one; a
// begin also raises LoadGuardBegins. The C++ side reads them on the game thread (LoadGuard.cpp) and feeds OnSignal.
// Checked against the game's 5.0 scripts (content0 r4Game.ws, commonMainMenuBase.ws, ingameMenu.ws, commonMapManager.ws,
// mapMenu.ws, 2026-10-13):
//   a load BEGINS  CR4Game.OnGameLoadInitFinishedSuccess (a save load is going ahead - load page, Continue, death screen,
//                  the engine's own quick load; the game closes its menus right after), CR4Game.OnGameEnded (a session
//                  ends: quit to the main menu, or the start of a load or world change from in game),
//                  CR4Game.OnGameStarting (a new game, a load or a world change is loading), CR4Game.OnBeforeWorldChange
//                  (fast travel to another region, a boat or quest crossing), CR4CommonMainMenuBase.OnClosingMenu (the title
//                  screen closes to start or load a game), CCommonMapManager.PerformLocalFastTravelTeleport (fast travel
//                  inside the region - SetSingleShotLoadingScreen, then a teleport behind a loading screen);
//   it ENDS        CR4Game.OnAfterLoadingScreenGameStart (the loading screen after a game start has gone) -> "game";
//                  CR4CommonMainMenuBase.OnConfigUI (the title screen is up) and CR4IngameMenu.OnConfigUI (the title's or
//                  the pause menu's own list is up - nothing loads behind either) -> "menu"; after a local fast travel,
//                  a player timer that runs once the world ticks again and the screen is no longer black or fading -> "game".
// The settings are kept with the game's own (the main menu itself saves them), so the first values read in a session are a
// BASELINE only, never a signal - as the per-save signals (SaveSignals.cpp). A changed LoadGuardSeq is the event.
//
// IT HOLDS:
//   * from the moment the plugin loads until a script event in THIS session says a menu is up or a game has started;
//   * from a "loading" event until the next "menu" or "game" event.
// A load that began and ended between two reads (LoadGuardBegins moved) still closes the menu once.
//
// IT NEVER TRAPS THE PLAYER (the failure mode, chosen): a stretch with no ending event fails OPEN - the start-up hold after
// kStartupLimitMs from plugin load, a load after kLoadingLimitMs from its first event - and the guard says so in the log.
// A missed end event would otherwise leave AMF dead in game; a missed begin only means one load without the guard, which is
// how every earlier version behaved. The limits are long next to a real load (a save on a hard disk is well under a minute)
// so a slow load is never cut short in practice, and short enough that a player who finds the menu dead has it back soon.
// With no engine bridge at all (FailOpen) there is no signal to wait for, and nothing AMF does needs the engine then.
// ============================================================================================================

#include <cstdint>
#include <string>
#include <string_view>

namespace loadguard::core
{
	inline constexpr std::uint64_t kStartupLimitMs = 120000;   // plugin load -> a menu or a game, at most
	inline constexpr std::uint64_t kLoadingLimitMs = 120000;   // a load's first event -> its end, at most

	enum class Reason : std::uint8_t
	{
		kNone,      // not holding
		kStartup,   // no menu or game seen yet this session
		kLoading    // a load has begun and not ended
	};

	inline const char* ReasonName(Reason a_r)
	{
		return a_r == Reason::kStartup ? "start-up" : a_r == Reason::kLoading ? "loading" : "none";
	}

	enum class Edge : std::uint8_t
	{
		kUnknown,
		kLoading,
		kGame,
		kMenu
	};

	inline Edge ParseEdge(std::string_view a_s)
	{
		if (a_s == "loading") { return Edge::kLoading; }
		if (a_s == "game") { return Edge::kGame; }
		if (a_s == "menu") { return Edge::kMenu; }
		return Edge::kUnknown;
	}

	// What one read of the script's signal was.
	enum class Read : std::uint8_t
	{
		kBaseline,   // the first read this session: kept, not acted on
		kSame,       // nothing new
		kEdge        // a new event (or several) since the last read
	};

	struct State
	{
		bool          started = false;   // a menu or a game has been seen this session (or the start-up hold failed open)
		bool          loading = false;   // a load has begun and not ended
		bool          baseline = false;  // the first read has been taken
		std::string   seq, begins;       // LoadGuardSeq / LoadGuardBegins as last read
		Edge          lastEdge = Edge::kUnknown;
		std::uint64_t since = 0;         // when the guard last went on or off (ms, the caller's clock)
		std::uint32_t period = 1;        // counts each stretch the guard holds, so "ignored" is logged once per stretch
		std::uint32_t loggedPeriod = 0;
		bool          closePending = false;   // a load began since the last read: the menu closes once
		std::uint32_t edges = 0, failOpens = 0;
		std::uint64_t startupLimit = kStartupLimitMs, loadingLimit = kLoadingLimitMs;

		explicit State(std::uint64_t a_nowMs = 0) :
			since(a_nowMs)
		{}

		bool Blocked() const { return !started || loading; }
		Reason Why() const { return !started ? Reason::kStartup : loading ? Reason::kLoading : Reason::kNone; }

		// One read of LoadGuardSeq, LoadGuard and LoadGuardBegins (the game thread, a few times a second or more).
		Read OnSignal(std::uint64_t a_now, std::string_view a_seq, std::string_view a_state, std::string_view a_begins)
		{
			if (!baseline) {
				baseline = true;
				seq = a_seq;
				begins = a_begins;
				return Read::kBaseline;
			}
			if (a_seq == seq) {
				return Read::kSame;
			}
			seq = a_seq;
			++edges;
			if (a_begins != begins) {
				begins = a_begins;
				closePending = true;   // a load began since the last read, whatever the state is now
			}
			lastEdge = ParseEdge(a_state);
			switch (lastEdge) {
			case Edge::kLoading: Set(a_now, started, true); break;
			case Edge::kGame:
			case Edge::kMenu:    Set(a_now, true, false); break;
			case Edge::kUnknown: break;   // a value this build does not know: no change
			}
			return Read::kEdge;
		}

		// The fail-open rule (any thread, under the caller's lock). True when it fired just now.
		bool Expire(std::uint64_t a_now)
		{
			if (!Blocked()) {
				return false;
			}
			const std::uint64_t limit = started ? loadingLimit : startupLimit;
			if (a_now < since || a_now - since < limit) {
				return false;
			}
			++failOpens;
			Set(a_now, true, false);
			return true;
		}

		// No signal can ever come (the engine bridge is off): stop holding now.
		void FailOpen(std::uint64_t a_now)
		{
			if (Blocked()) {
				++failOpens;
			}
			Set(a_now, true, false);
		}

		// The render thread, every frame: true when an open menu must close now - the guard holds, or a load began since the
		// last read (then once). Consumed whether or not the menu is open, so it never closes a menu opened later.
		bool TakeClose()
		{
			const bool close = Blocked() || closePending;
			closePending = false;
			return close;
		}

		// True the first time an input is ignored in this stretch of the guard (the caller logs it once), false after.
		bool TakeFirstIgnored()
		{
			if (!Blocked() || loggedPeriod == period) {
				return false;
			}
			loggedPeriod = period;
			return true;
		}

	private:
		void Set(std::uint64_t a_now, bool a_started, bool a_loading)
		{
			const bool was = Blocked();
			started = a_started;
			loading = a_loading;
			if (was != Blocked()) {
				since = a_now;
				if (Blocked()) {
					++period;   // a new stretch begins: the next ignored input is logged again
				}
			}
		}
	};
}
