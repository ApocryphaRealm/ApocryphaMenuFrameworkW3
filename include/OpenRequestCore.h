#pragma once

// ============================================================================================================
// THE GAME-MENU ENTRY'S REQUEST - the part with no game in it, so tools\open_request_check.cpp runs the same code offline
// (1.0.6, after the 2026-10-14 00:23 start-up crash).
//
// The framework's entry in the game's own menu sets the hidden setting ApocryphaMenuFramework.OpenRequest to "true"
// (ApocryphaMenuFramework.ws, CR4IngameMenu.OnItemActivated); AMF reads it a few times a second on the game thread
// (ModMenus.cpp), opens its window and sets it back to "false".
//
// The game keeps that setting WITH ITS OWN (dx12user.settings), so a "true" can survive a session: the game started at
// 00:23 with OpenRequest=true saved, AMF read it 2.7 s in, wrote "false" through SetVarValue at once, and the engine faulted
// inside SetVarValue (witcher3.exe+0x1F4C036 reading 0x180) - the game's crash reporter came up and the game hung at the
// splash. So:
//   * the hook runs only once the loading guard has lifted (red3::Pump is held until then), so the player cannot have picked
//     the entry before the first read - the FIRST value read in a session is a BASELINE, never a pick;
//   * a baseline of "true" is stale: it is cleared quietly once the game is settled (red3::Settled) and the window does
//     not open;
//   * only a "true" seen after that baseline opens the window.
// ============================================================================================================

namespace openrequest
{
	enum class Act
	{
		kNone,         // nothing to do
		kOpen,         // a pick in this session: set the request back to "false" and open the window
		kClearStale    // the left-over request: set it back to "false" and save, without opening anything
	};

	struct Result
	{
		Act  act = Act::kNone;
		bool staleFound = false;   // this read was the baseline and it was "true" (log it once)
	};

	struct State
	{
		int stale = -1;   // -1 no baseline yet, 1 a stale "true" waits to be cleared, 0 none

		// One read of OpenRequest (a_on: it read "true" or "1"); a_settled: red3::Settled() now.
		Result OnRead(bool a_on, bool a_settled)
		{
			Result r;
			if (stale < 0) {
				stale = a_on ? 1 : 0;
				r.staleFound = a_on;
			}
			if (stale == 1) {
				if (!a_on) {
					stale = 0;   // cleared some other way: nothing left to clear
				} else if (a_settled) {
					stale = 0;   // once only, whether or not the game takes it
					r.act = Act::kClearStale;
				}
				return r;
			}
			if (a_on) {
				r.act = Act::kOpen;
			}
			return r;
		}
	};
}
