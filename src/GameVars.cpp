// ============================================================================================================
// GAME SETTING VALUES FOR CONSUMER MODS (Witcher 3, 2026-10-06 - for Item Explorer on Witcher 3, the owner: "Start porting
// item explorer to The Witcher 3"). A native mod cannot call WitcherScript (no script FFI - PLAN.md, RESEARCH-web 2), so it
// talks to its own script through a user_config_matrix value, as AMF's own game-menu entry does (OpenRequest). These calls
// lend a consumer the framework's engine bridge (Red3) instead of each mod finding the settings natives again and hooking the
// game's frame a second time.
//   AMF_SetGameVar    queues the write for the game thread (the settings natives run there only); false when the bridge is
//                     off (then nothing is queued).
//   AMF_WatchGameVar  the framework reads that value on every frame from now on (cheap: one settings lookup).
//   AMF_GetGameVar    the last value read for a watched pair, copied into the caller's buffer (always NUL-terminated);
//                     returns its length, or -1 while nothing has been read yet (not watched, first frame not run, bridge
//                     off, or the var does not exist in any user_config_matrix file).
// The group and var must exist in a user_config_matrix XML the consumer ships, or the game ignores them.
// ============================================================================================================
#include "AMF/API.h"
#include "Logger.h"
#include "Red3.h"

#include <cstdio>
#include <cstring>
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

	std::mutex                     g_lock;
	std::map<std::string, Watched> g_watched;   // key: group + '\x1f' + var
	bool                           g_hooked = false;

	std::string Key(const char* a_group, const char* a_var) { return std::string(a_group) + '\x1f' + a_var; }

	// game thread, every frame
	void ReadWatched()
	{
		std::map<std::string, Watched> copy;
		{
			std::scoped_lock l(g_lock);
			copy = g_watched;
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
	}
}

AMF_API bool AMF_SetGameVar(const char* a_group, const char* a_var, const char* a_value)
{
	if (!a_group || !a_var || !a_value || !*a_group || !*a_var) {
		return false;
	}
	if (!red3::ConfigReady()) {
		logger::warn("AMF_SetGameVar {}/{}: the engine bridge is off - nothing queued", a_group, a_var);
		return false;
	}
	std::string group = a_group, var = a_var, value = a_value;
	red3::Post([group, var, value] {
		if (!red3::SetVar(group, var, value)) {
			logger::warn("AMF_SetGameVar {}/{}: the game did not take '{}' (is the var in a user_config_matrix file?)", group, var, value);
		}
	});
	return true;
}

AMF_API void AMF_WatchGameVar(const char* a_group, const char* a_var)
{
	if (!a_group || !a_var || !*a_group || !*a_var) {
		return;
	}
	bool hook = false;
	{
		std::scoped_lock l(g_lock);
		auto [it, inserted] = g_watched.try_emplace(Key(a_group, a_var));
		if (inserted) {
			it->second.group = a_group;
			it->second.var = a_var;
			logger::info("watching game var {}/{} for a consumer mod", a_group, a_var);
		}
		if (!g_hooked) {
			g_hooked = true;
			hook = true;
		}
	}
	if (hook) {
		red3::AddFrameHook(&ReadWatched);
	}
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
