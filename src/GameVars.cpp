// ============================================================================================================
// GAME SETTING VALUES FOR CONSUMER MODS (Witcher 3, 2026-10-06 - for Item Explorer on Witcher 3, the owner: "Start porting
// item explorer to The Witcher 3"). A native mod cannot call WitcherScript (no script FFI - PLAN.md, RESEARCH-web 2), so it
// talks to its own script through a user_config_matrix value, as AMF's own game-menu entry does (OpenRequest). These calls
// lend a consumer the framework's engine bridge (Red3) instead of each mod finding the settings natives again and hooking the
// game's frame a second time.
//   AMF_SetGameVar    QUEUES the write; the framework makes it on the game thread once the game is settled (below). false
//                     when the bridge is off (then nothing is queued).
//   AMF_WatchGameVar  the framework reads that value on every frame from now on (cheap: one settings lookup).
//   AMF_GetGameVar    the last value read for a watched pair, copied into the caller's buffer (always NUL-terminated);
//                     returns its length, or -1 while nothing has been read yet (not watched, first frame not run, bridge
//                     off, or the var does not exist in any user_config_matrix file).
// The group and var must exist in a user_config_matrix XML the consumer ships, or the game ignores them.
//
// WRITES WAIT FOR A SETTLED GAME (Item Explorer's first run, 2026-10-06): a write made two seconds into start-up faulted
// inside the game's SetVarValue; the guard caught it, but the engine was left mid-call and the game hung before its window
// came up. AMF's own writes already waited 30 s for the same reason (ModMenus.cpp, the 1.0.0 run). So queued writes go out
// only when the bridge has been up for 30 s AND the game's menu has been shown once (the framework's script sets
// ApocryphaMenuFramework.GameMenuOpen from the title screen - proof that scripts and settings are live), or 120 s after the
// bridge came up without that signal; and only for a var the game already reads back (its XML is loaded).
// ============================================================================================================
#include "AMF/API.h"
#include "Logger.h"
#include "Red3.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>

namespace
{
	struct Watched
	{
		std::string group;
		std::string var;
		std::string value;
		bool        read = false;
	};

	struct Write
	{
		std::string group;
		std::string var;
		std::string value;
	};

	constexpr ULONGLONG kSettleMs = 30000;     // after the bridge is up, as AMF's own writes
	constexpr ULONGLONG kNoMenuMs = 120000;    // when the game's menu signal never comes (the framework's script missing)

	std::mutex                     g_lock;
	std::map<std::string, Watched> g_watched;   // key: group + '\x1f' + var
	std::deque<Write>              g_writes;
	bool                           g_hooked = false;

	std::string Key(const char* a_group, const char* a_var) { return std::string(a_group) + '\x1f' + a_var; }

	// game thread: may queued writes go out yet?
	bool Settled()
	{
		static ULONGLONG s_readySince = 0;
		static bool      s_menuSeen = false;
		static bool      s_logged = false;
		const ULONGLONG  now = ::GetTickCount64();
		if (s_readySince == 0) {
			s_readySince = now;
		}
		if (!s_menuSeen) {
			std::string open;
			if (red3::GetVar("ApocryphaMenuFramework", "GameMenuOpen", open) && (_stricmp(open.c_str(), "true") == 0 || open == "1")) {
				s_menuSeen = true;
			}
		}
		const ULONGLONG up = now - s_readySince;
		const bool      settled = up >= kSettleMs && (s_menuSeen || up >= kNoMenuMs);
		if (settled && !s_logged) {
			s_logged = true;
			logger::info("game vars: writes for consumer mods may go out now ({} s after the bridge came up, the game's menu {})", up / 1000,
				s_menuSeen ? "shown" : "never seen");
		}
		return settled;
	}

	// game thread, every frame
	void OnFrame()
	{
		if (!red3::ConfigReady()) {
			return;
		}
		std::map<std::string, Watched> copy;
		bool                           writesWaiting = false;
		{
			std::scoped_lock l(g_lock);
			copy = g_watched;
			writesWaiting = !g_writes.empty();
		}
		for (auto& [key, w] : copy) {
			std::string value;
			const bool  ok = red3::GetVar(w.group, w.var, value);
			std::scoped_lock l(g_lock);
			auto it = g_watched.find(key);
			if (it == g_watched.end()) {
				continue;
			}
			if (ok && (!it->second.read || it->second.value != value)) {
				logger::debug("game var {}/{} now '{}'", w.group, w.var, value);
			}
			it->second.read = ok;
			if (ok) {
				it->second.value = value;
			}
		}
		if (!writesWaiting || !Settled()) {
			return;
		}
		while (red3::ConfigReady()) {
			Write w;
			{
				std::scoped_lock l(g_lock);
				if (g_writes.empty()) {
					break;
				}
				w = g_writes.front();
			}
			std::string current;
			if (!red3::GetVar(w.group, w.var, current)) {
				// not loaded (yet) - its XML may be missing; keep it and try again next frame
				static ULONGLONG s_warned = 0;
				const ULONGLONG  now = ::GetTickCount64();
				if (now - s_warned > 60000) {
					s_warned = now;
					logger::warn("AMF_SetGameVar {}/{}: the game does not know this var yet (is it in a user_config_matrix file?) - waiting",
						w.group, w.var);
				}
				return;
			}
			{
				std::scoped_lock l(g_lock);
				g_writes.pop_front();
			}
			if (current == w.value) {
				continue;
			}
			if (!red3::SetVar(w.group, w.var, w.value)) {
				logger::warn("AMF_SetGameVar {}/{}: the game did not take '{}'", w.group, w.var, w.value);
			}
		}
	}

	// once, on the first call of either export
	void HookOnce()
	{
		bool hook = false;
		{
			std::scoped_lock l(g_lock);
			if (!g_hooked) {
				g_hooked = true;
				hook = true;
			}
		}
		if (hook) {
			red3::AddFrameHook(&OnFrame);
		}
	}
}

AMF_API bool AMF_SetGameVar(const char* a_group, const char* a_var, const char* a_value)
{
	if (!a_group || !a_var || !a_value || !*a_group || !*a_var) {
		return false;
	}
	if (!red3::ConfigReady()) {
		logger::debug("AMF_SetGameVar {}/{}: the engine bridge is not up - nothing queued", a_group, a_var);
		return false;
	}
	HookOnce();
	{
		std::scoped_lock l(g_lock);
		g_writes.push_back({ a_group, a_var, a_value });
	}
	logger::debug("AMF_SetGameVar {}/{} = '{}' queued", a_group, a_var, a_value);
	return true;
}

AMF_API void AMF_WatchGameVar(const char* a_group, const char* a_var)
{
	if (!a_group || !a_var || !*a_group || !*a_var) {
		return;
	}
	{
		std::scoped_lock l(g_lock);
		auto [it, inserted] = g_watched.try_emplace(Key(a_group, a_var));
		if (inserted) {
			it->second.group = a_group;
			it->second.var = a_var;
			logger::info("watching game var {}/{} for a consumer mod", a_group, a_var);
		}
	}
	HookOnce();
}

AMF_API std::int32_t AMF_GetGameVar(const char* a_group, const char* a_var, char* a_buffer, std::uint32_t a_capacity)
{
	if (!a_group || !a_var) {
		return -1;
	}
	std::scoped_lock l(g_lock);
	auto it = g_watched.find(Key(a_group, a_var));
	if (it == g_watched.end() || !it->second.read) {
		if (a_buffer && a_capacity) {
			a_buffer[0] = '\0';
		}
		return -1;
	}
	if (a_buffer && a_capacity) {
		std::snprintf(a_buffer, a_capacity, "%s", it->second.value.c_str());
	}
	return static_cast<std::int32_t>(it->second.value.size());
}
