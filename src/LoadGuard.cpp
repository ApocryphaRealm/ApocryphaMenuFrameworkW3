// THE LOADING GUARD - the game side (LoadGuard.h; the rules and the Witcher 3 events are in LoadGuardCore.h).
#include "PCH.h"

#include "LoadGuard.h"

#include "LoadGuardCore.h"
#include "Red3.h"

#include <mutex>
#include <string>

namespace loadguard
{
	namespace
	{
		constexpr const char* kGroup = "ApocryphaMenuFramework";

		std::mutex  g_lock;
		core::State g_state{ ::GetTickCount64() };   // guarded by g_lock: the game thread moves it, every input path reads it
		int         g_signal = 0;                     // 0 waiting for the script's settings, 1 reading them, 2 no bridge (g_lock)

		// The on / off edges at info (the caller holds g_lock).
		void LogEdge(bool a_wasBlocked, const char* a_why)
		{
			const bool now = g_state.Blocked();
			if (now == a_wasBlocked) {
				return;
			}
			if (now) {
				logger::info("loading guard: on ({}) - AMF stays closed, ignores input and runs no engine call until the game is up", a_why);
			} else {
				logger::info("loading guard: off ({}) - AMF answers input again", a_why);
			}
		}

		// The fail-open rule, under g_lock; logs when it lets go.
		void ExpireLocked(ULONGLONG a_now)
		{
			const core::Reason why = g_state.Why();
			const std::uint64_t held = a_now >= g_state.since ? (a_now - g_state.since) / 1000 : 0;
			if (g_state.Expire(a_now)) {
				logger::warn("loading guard: no {} event from the framework's script in {} s - AMF answers input again rather than stay "
							 "dead (is Mods\\modApocryphaMenuFramework installed and its script compiled?)",
					why == core::Reason::kStartup ? "menu or game-start" : "end-of-load", held);
			}
		}
	}

	void OnGameThreadFrame()
	{
		static ULONGLONG s_next = 0;   // game thread only
		const ULONGLONG  now = ::GetTickCount64();
		if (now < s_next) {
			return;
		}
		s_next = now + 10;   // the tick can run every 2 ms; a hundred reads a second is plenty

		if (red3::Unavailable()) {
			std::scoped_lock l(g_lock);
			if (g_signal != 2) {
				g_signal = 2;
				const bool was = g_state.Blocked();
				g_state.FailOpen(now);
				logger::warn("loading guard: the engine bridge is off, so the framework's script cannot be read - the guard stands down "
							 "(AMF makes no engine call this session either)");
				LogEdge(was, "no engine bridge");
			}
			return;
		}
		if (!red3::ConfigReady()) {
			std::scoped_lock l(g_lock);
			ExpireLocked(now);
			return;
		}

		std::string seq;
		if (!red3::GetVar(kGroup, "LoadGuardSeq", seq)) {
			// the game loads its config matrix after the first frames (rule 17: retry quietly); the fail-open rule covers a
			// missing XML
			static bool s_noted = false;
			if (!s_noted) {
				s_noted = true;
				logger::debug("loading guard: ApocryphaMenuFramework.LoadGuardSeq not loaded yet - asking again");
			}
			std::scoped_lock l(g_lock);
			ExpireLocked(now);
			return;
		}
		bool changed = false;
		{
			std::scoped_lock l(g_lock);
			changed = !g_state.baseline || g_state.seq != seq;
		}
		std::string edge, begins;
		if (changed) {   // the other two are read only when the counter moved
			red3::GetVar(kGroup, "LoadGuard", edge);
			red3::GetVar(kGroup, "LoadGuardBegins", begins);
		}

		std::scoped_lock l(g_lock);
		if (g_signal == 0) {
			g_signal = 1;
		}
		const bool       was = g_state.Blocked();
		const core::Read r = changed ? g_state.OnSignal(now, seq, edge, begins) : core::Read::kSame;
		if (r == core::Read::kBaseline) {
			logger::info("loading guard: the framework's script signal is readable (baseline LoadGuardSeq '{}', LoadGuard '{}' - the game's kept "
						 "values, not a signal); AMF waits for a menu or a game in this session",
				seq, edge);
		} else if (r == core::Read::kEdge) {
			logger::debug("loading guard: script event '{}' (LoadGuardSeq '{}', LoadGuardBegins '{}'){}", edge, seq, begins,
				g_state.closePending ? " - a load began since the last read" : "");
			const core::Edge e = core::ParseEdge(edge);
			LogEdge(was, e == core::Edge::kLoading ? "a load began" : e == core::Edge::kMenu ? "a menu is up" : e == core::Edge::kGame ? "the game is up" : "an unknown event");
		}
		ExpireLocked(now);
	}

	bool Blocked()
	{
		std::scoped_lock l(g_lock);
		ExpireLocked(::GetTickCount64());
		return g_state.Blocked();
	}

	bool TakeClose()
	{
		std::scoped_lock l(g_lock);
		ExpireLocked(::GetTickCount64());
		return g_state.TakeClose();
	}

	void NoteIgnored(const char* a_what)
	{
		bool        first = false;
		core::Reason why = core::Reason::kNone;
		{
			std::scoped_lock l(g_lock);
			first = g_state.TakeFirstIgnored();
			why = g_state.Why();
		}
		if (first) {
			logger::debug("loading guard: {} ignored - the game is {} (logged once per load)", a_what ? a_what : "input",
				why == core::Reason::kStartup ? "not at a menu yet" : "loading");
		}
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		const ULONGLONG now = ::GetTickCount64();
		const double    since = now >= g_state.since ? static_cast<double>(now - g_state.since) / 1000.0 : 0.0;
		const char*     edge = g_state.lastEdge == core::Edge::kLoading ? "loading" :
		                       g_state.lastEdge == core::Edge::kGame    ? "game" :
		                       g_state.lastEdge == core::Edge::kMenu    ? "menu" : "";
		return std::format(R"({{"active":{},"reason":"{}","since":{:.1f},"edges":{},"failOpens":{},"lastEdge":"{}","signal":"{}"}})",
			g_state.Blocked() ? "true" : "false", core::ReasonName(g_state.Why()), since, g_state.edges, g_state.failOpens, edge,
			g_signal == 2 ? "off" : g_signal == 1 ? "script" : "waiting");
	}
}
