// Off-game check of include/OpenRequestCore.h - what the game-menu entry's hook (ModMenus.cpp) does with each read of
// ApocryphaMenuFramework.OpenRequest, including the 2026-10-14 00:23 case: the game started with "true" saved from an
// earlier session.
//   xmake build open_request_check && xmake run open_request_check
// Prints one line per check and "open_request_check: N of N passed"; exits 1 on any failure.
#include "OpenRequestCore.h"

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

	using openrequest::Act;
	using openrequest::State;
}

int main()
{
	{   // the crash case: true saved from the last session, read at the first chance
		State s;
		auto r = s.OnRead(true, false);
		Check(r.staleFound && r.act == Act::kNone, "a saved true read first (not settled) is stale: logged, no open, no write");
		int writes = 0, opens = 0, finds = 0;
		for (int i = 0; i < 150; ++i) {   // 30 s of reads at 5 a second, still not settled
			r = s.OnRead(true, false);
			writes += r.act == Act::kClearStale;
			opens += r.act == Act::kOpen;
			finds += r.staleFound;
		}
		Check(finds == 0, "the stale find is logged once only");
		Check(writes == 0 && opens == 0, "before the game is settled the stale true is neither written nor opened");
		r = s.OnRead(true, true);
		Check(r.act == Act::kClearStale, "once settled the stale true is cleared (quietly: kClearStale, not kOpen)");
		r = s.OnRead(true, true);
		Check(r.act == Act::kOpen, "a true read after the clear is a pick in this session and opens");
		r = s.OnRead(false, true);
		Check(r.act == Act::kNone, "false after that does nothing");
	}
	{   // the stale clear is attempted once only, even if the game did not take it
		State s;
		s.OnRead(true, false);
		auto r = s.OnRead(true, true);
		Check(r.act == Act::kClearStale, "stale cleared at settle");
		r = s.OnRead(false, true);
		Check(r.act == Act::kNone, "the cleared value reads false: nothing");
	}
	{   // the normal case: false saved, the player picks the entry later
		State s;
		auto r = s.OnRead(false, false);
		Check(!r.staleFound && r.act == Act::kNone, "a saved false read first is a quiet baseline");
		r = s.OnRead(true, false);
		Check(r.act == Act::kOpen, "a pick before the game is settled still opens (a player's action, at the menu)");
		r = s.OnRead(false, false);
		Check(r.act == Act::kNone, "the cleared request reads false: nothing");
		r = s.OnRead(true, true);
		Check(r.act == Act::kOpen, "a second pick opens again");
	}
	{   // the stale true cleared by someone else before the settle
		State s;
		s.OnRead(true, false);
		auto r = s.OnRead(false, false);
		Check(r.act == Act::kNone, "a stale true that turns false by itself needs no write");
		r = s.OnRead(true, false);
		Check(r.act == Act::kOpen, "and a true after that is a pick");
	}
	{   // never a write before the baseline is taken, and the first read never opens
		for (int settled = 0; settled < 2; ++settled) {
			State s;
			auto r = s.OnRead(true, settled != 0);
			Check(r.act != Act::kOpen, std::string("the first read never opens the window (settled ") + (settled ? "yes" : "no") + ")");
		}
	}
	std::cout << "open_request_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
