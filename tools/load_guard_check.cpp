// Off-game check of include/LoadGuardCore.h - the same rules AMF's input paths, its open-menu paths, the render thread's
// close and the engine bridge's queue ask (LoadGuard.h). The framework's script is modelled as it is written
// (ApocryphaMenuFramework.ws, "THE LOADING GUARD"): each game event writes LoadGuard, raises LoadGuardBegins for a begin, then
// raises LoadGuardSeq; the counters start at the values the game kept from the last session.
//   xmake build load_guard_check && xmake run load_guard_check
// Prints one line per check and "load_guard_check: N of N passed"; exits 1 on any failure.
#include "LoadGuardCore.h"

#include <cstdint>
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

	using loadguard::core::State;

	// The framework's script: its three hidden settings, as the game holds them.
	struct Script
	{
		int         seq = 0, begins = 0;
		std::string edge;
		void Edge(const char* a_edge, bool a_begins)
		{
			edge = a_edge;
			if (a_begins) { ++begins; }
			++seq;
		}
		// the game's events, as the wrappers turn them into edges
		void TitleUp() { Edge("menu", false); }                  // CR4CommonMainMenuBase.OnConfigUI
		void GameMenuUp() { Edge("menu", false); }               // CR4IngameMenu.OnConfigUI (title list or pause menu)
		void TitleCloses() { Edge("loading", true); }            // CR4CommonMainMenuBase.OnClosingMenu
		void LoadGoesAhead() { Edge("loading", true); }          // CR4Game.OnGameLoadInitFinishedSuccess
		void SessionEnds() { Edge("loading", true); }            // CR4Game.OnGameEnded
		void SessionStarting() { Edge("loading", true); }        // CR4Game.OnGameStarting
		void WorldChange() { Edge("loading", true); }            // CR4Game.OnBeforeWorldChange
		void LoadingScreenGone() { Edge("game", false); }        // CR4Game.OnAfterLoadingScreenGameStart
		void LocalTravel() { Edge("loading", true); }            // CCommonMapManager.PerformLocalFastTravelTeleport
		void LocalTravelDone() { Edge("game", false); }          // CR4Player.AMF_LocalTravelDone
	};

	// LoadGuard.cpp's game-thread read
	void Read(State& a_g, const Script& a_s, std::uint64_t a_now)
	{
		a_g.OnSignal(a_now, std::to_string(a_s.seq), a_s.edge, std::to_string(a_s.begins));
	}

	// What AMF's input path and render thread do with the guard (Input.cpp DecideButton, Renderer.cpp's frame).
	struct Amf
	{
		State& guard;
		bool   menuOpen = false;
		int    logged = 0;     // "ignored" debug lines
		int    taken = 0;      // edges kept from the game
		int    closedByGuard = 0;
		int    enginePumps = 0;   // red3::Pump runs that were allowed (SetHold)
		void Press(bool a_menuKey)
		{
			if (guard.Blocked()) {
				if (guard.TakeFirstIgnored()) { ++logged; }
				return;   // the game gets it untouched
			}
			if (a_menuKey) { menuOpen = !menuOpen; ++taken; }
			else if (menuOpen) { ++taken; }
		}
		bool DevBenchOpen()   // renderer::SetMenuVisible(true) - refused while blocked
		{
			if (guard.Blocked()) {
				if (guard.TakeFirstIgnored()) { ++logged; }
				return false;
			}
			menuOpen = true;
			return true;
		}
		void Frame()
		{
			if (guard.TakeClose() && menuOpen) { menuOpen = false; ++closedByGuard; }
			if (!guard.Blocked()) { ++enginePumps; }
		}
	};
}

int main()
{
	// ---- start-up: plugin load -> main menu, with the values the game kept from a session that ended in game ----
	{
		State  g(0);
		Script s{ 41, 17, "game" };   // kept from last time: "game" must not lift the hold
		Amf    a{ g };
		Check(g.Blocked() && g.Why() == loadguard::core::Reason::kStartup, "plugin load: the guard holds before anything has happened (no frame count)");
		a.Press(true);
		a.Press(true);
		a.Press(false);
		a.Frame();
		Check(!a.menuOpen && a.taken == 0, "F1 three times during start-up: the menu stays closed and nothing is taken from the game");
		Check(a.logged == 1, "the ignored input is logged once for the whole start-up, not once per key");
		Check(!a.DevBenchOpen(), "DevBench op=open during start-up is refused");
		Check(a.enginePumps == 0, "no engine call runs during start-up (red3::Pump holds)");
		Read(g, s, 2000);
		Check(g.Blocked() && g.baseline, "the first read is a baseline: the kept \"game\" does not lift the start-up hold");
		Read(g, s, 2010);
		Check(g.Blocked(), "reading the same kept values again changes nothing");
		g.Expire(119000);
		Check(g.Blocked(), "119 s in, no menu yet: still holding (the fail-open limit is 120 s)");
		s.TitleUp();
		Read(g, s, 119500);
		Check(!g.Blocked() && g.Why() == loadguard::core::Reason::kNone, "the title screen opened (a live event this session): the guard lifts");
		a.Press(true);
		Check(a.menuOpen && a.taken == 1, "F1 at the main menu opens the menu");
		a.Frame();
		Check(a.menuOpen && a.enginePumps == 1, "the menu stays open and the engine queue runs again");
		s.GameMenuUp();
		Read(g, s, 119600);
		Check(!g.Blocked() && a.menuOpen, "the title's own list opening is another menu event: nothing changes");
		a.Press(true);
	}
	// ---- main menu -> load -> in game -> fast travel -> in game -> world change -> quit to the main menu ----
	{
		State  g(0);
		Script s{ 3, 1, "loading" };   // a session that was quit mid-load
		Amf    a{ g };
		Read(g, s, 1000);
		Check(g.Blocked(), "kept \"loading\" from last time: a baseline, still the start-up hold");
		s.TitleUp();
		Read(g, s, 9000);
		Check(!g.Blocked(), "title screen: free");
		a.Press(true);
		Check(a.menuOpen, "the menu is open over the title screen");
		// the player picks a save (the menu was closed first; the game's menu is behind it) - here: opened again via DevBench
		s.LoadGoesAhead();
		Read(g, s, 10000);
		Check(g.Blocked() && g.Why() == loadguard::core::Reason::kLoading, "a save load goes ahead: the guard holds (loading)");
		a.Frame();
		Check(!a.menuOpen && a.closedByGuard == 1, "the menu open when the load began is closed by the guard");
		s.TitleCloses();
		s.SessionStarting();
		Read(g, s, 11000);
		Check(g.Blocked(), "the title closing and the session starting: still loading");
		const int before = a.logged;
		a.Press(true);
		a.Press(false);
		a.Press(true);
		Check(!a.menuOpen && a.taken == 1 && a.logged == before + 1, "F1 and clicks during the load are ignored, logged once for this load");
		Check(!a.DevBenchOpen(), "DevBench op=open during the load is refused");
		const int pumps = a.enginePumps;
		a.Frame();
		a.Frame();
		Check(a.enginePumps == pumps, "no engine call runs during the load (queued jobs wait)");
		s.LoadingScreenGone();
		Read(g, s, 25000);
		Check(!g.Blocked(), "the loading screen after the game start has gone: the guard lifts");
		a.Frame();
		Check(a.enginePumps == pumps + 1, "the engine queue runs again on the first frame after the load");
		a.Press(true);
		Check(a.menuOpen, "F1 in game opens the menu");
		a.Press(true);
		// fast travel inside the region
		s.LocalTravel();
		Read(g, s, 60000);
		Check(g.Blocked() && g.since == 60000, "local fast travel: the guard holds from its first event");
		a.Press(true);
		Check(!a.menuOpen && a.logged == before + 2, "F1 during the fast travel does nothing, logged once for this load");
		s.LocalTravelDone();
		Read(g, s, 66000);
		Check(!g.Blocked() && g.since == 66000, "the player's timer after the fast travel: the guard lifts");
		a.Press(true);
		Check(a.menuOpen, "F1 after the fast travel opens the menu");
		// fast travel to another region with the menu open behind it (a quest crossing while the game runs unpaused)
		s.WorldChange();
		s.SessionEnds();
		Read(g, s, 90000);
		a.Frame();
		Check(g.Blocked() && !a.menuOpen && a.closedByGuard == 2, "a world change: the guard holds and closes the open menu");
		s.SessionStarting();
		Read(g, s, 95000);
		Check(g.Blocked() && g.since == 90000, "more begin events in the same load keep the guard on from the first (since unchanged)");
		s.LoadingScreenGone();
		Read(g, s, 110000);
		Check(!g.Blocked(), "the new region's loading screen has gone: free");
		// quit to the main menu
		s.SessionEnds();
		Read(g, s, 200000);
		Check(g.Blocked(), "quit to the main menu: holding while the session ends");
		s.TitleUp();
		Read(g, s, 205000);
		Check(!g.Blocked(), "the title screen is up again: free");
		Check(a.logged == before + 2, "nothing more was ignored while free");
	}
	// ---- a whole load between two reads (the game thread did not read during it) ----
	{
		State  g(0);
		Script s{ 0, 0, "" };
		Amf    a{ g };
		Read(g, s, 1000);
		s.TitleUp();
		Read(g, s, 5000);
		a.Press(true);
		s.LoadGoesAhead();
		s.SessionStarting();
		s.LoadingScreenGone();
		Read(g, s, 30000);
		Check(!g.Blocked(), "begin and end both came between two reads: not loading now");
		a.Frame();
		Check(!a.menuOpen && a.closedByGuard == 1, "but a load began since the last read: the menu that was open closes once");
		a.Press(true);
		a.Frame();
		Check(a.menuOpen, "and the next open is not closed by it again");
	}
	// ---- missed events: the guard fails open, never traps the player ----
	{
		State  g(0);
		Script s{ 7, 2, "menu" };
		Amf    a{ g };
		Read(g, s, 1500);
		s.TitleUp();
		Read(g, s, 9000);
		s.LoadGoesAhead();
		Read(g, s, 10000);
		Check(g.Blocked(), "a load began");
		Check(!g.Expire(10000 + 119999), "119.999 s with no end event: still holding");
		Check(g.Expire(10000 + 120000) && !g.Blocked() && g.failOpens == 1, "120 s with no end event (missed): the guard fails open, counted");
		a.Press(true);
		Check(a.menuOpen, "F1 works after the fail-open");
		s.LoadingScreenGone();
		Read(g, s, 140000);
		Check(!g.Blocked() && g.failOpens == 1, "the late end event changes nothing");
	}
	{
		State g(0);
		Check(!g.Expire(119999) && g.Blocked(), "no script signal at all (script missing): holding before 120 s");
		Check(g.Expire(120000) && !g.Blocked(), "and failing open at 120 s - AMF is never left dead");
		Script s{ 5, 5, "game" };
		Read(g, s, 121000);
		s.LoadGoesAhead();
		Read(g, s, 130000);
		Check(g.Blocked(), "after a start-up fail-open, a later load is still guarded");
	}
	{
		State g(0);
		g.FailOpen(500);
		Check(!g.Blocked() && g.failOpens == 1, "no engine bridge (FailOpen): the guard stands down at once");
	}
	{
		State  g(0);
		Script s{ 1, 1, "menu" };
		Read(g, s, 1000);
		s.TitleUp();
		Read(g, s, 2000);
		s.Edge("something-new", false);
		Read(g, s, 3000);
		Check(!g.Blocked() && g.edges == 2, "an event value this build does not know changes nothing");
		Check(!g.TakeFirstIgnored(), "nothing is logged while the guard is off");
	}

	std::cout << "load_guard_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
