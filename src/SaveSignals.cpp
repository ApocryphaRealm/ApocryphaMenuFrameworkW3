// ============================================================================================================
// PER-SAVE SIGNALS (1.0.5, plan row 64 - the Witcher 3 side of Skyrim's per-save settings). Skyrim's SKSE tells a plugin
// when the game saves and loads, with the save's name; The Witcher 3 has no such message for a native mod. The
// framework's script (Mods\modApocryphaMenuFramework\content\scripts\local\ApocryphaMenuFramework.ws) wraps the game's own
// events instead and leaves the signal in hidden settings of the group ApocryphaMenuFramework (declared in
// bin\config\r4game\user_config_matrix\pc\ApocryphaMenuFramework.xml):
//   CR4Game.OnSaveCompleted(type, succeeded) - succeeded only: SaveName = the newest save's file name, then SaveSeq + 1
//   CR4Game.OnGameStarted(restored)          - LoadName = the save that was loaded ("" for a new game), then LoadSeq + 1
// Read here a few times a second on the game thread (reads only - nothing is written back), and a changed counter calls
// persistence's OnSaveGame / OnLoadGame, so per-save values follow the save that was made or loaded. The counters are kept
// with the game's settings, so the first values read in a session are only the baseline, never a signal.
// ============================================================================================================
#include "PCH.h"

#include "Persistence.h"
#include "Logger.h"
#include "Red3.h"

#include <mutex>
#include <string>

namespace persistence
{
	namespace
	{
		std::mutex  g_lock;
		std::string g_saveSeq, g_loadSeq;     // the counters as last read ("" until read)
		std::string g_saveName, g_loadName;   // the names that came with the last signals seen
		int         g_saves = 0, g_loads = 0; // signals acted on this session
		bool        g_started = false;

		bool Read(const char* a_var, std::string& a_out)
		{
			return red3::GetVar("ApocryphaMenuFramework", a_var, a_out);
		}

		// "-1" is what the game returns for a var never set (ModMenus.cpp, MenuEntry): no name.
		std::string Clean(const std::string& a_name)
		{
			return a_name == "-1" ? std::string() : a_name;
		}

		void OnFrame()
		{
			static ULONGLONG s_next = 0;
			const ULONGLONG  now = ::GetTickCount64();
			if (now < s_next || !red3::ConfigReady()) {
				return;
			}
			s_next = now + 250;
			std::string saveSeq, loadSeq;
			if (!Read("SaveSeq", saveSeq) || !Read("LoadSeq", loadSeq)) {
				return;   // the XML is not loaded yet (or missing - the OpenRequest hook warns about that)
			}
			std::string lastSave, lastLoad;
			bool        baseline = false;
			{
				std::scoped_lock l(g_lock);
				if (!g_started) {
					g_started = true;
					baseline = true;
					g_saveSeq = saveSeq;
					g_loadSeq = loadSeq;
				}
				lastSave = g_saveSeq;
				lastLoad = g_loadSeq;
			}
			if (baseline) {
				logger::debug("per-save signals: baseline SaveSeq '{}', LoadSeq '{}' (the game's kept values, not a signal)", saveSeq, loadSeq);
				return;
			}
			if (saveSeq != lastSave) {
				std::string name;
				Read("SaveName", name);
				name = Clean(name);
				{
					std::scoped_lock l(g_lock);
					g_saveSeq = saveSeq;
					g_saveName = name;
					++g_saves;
				}
				if (name.empty()) {
					logger::warn("per-save signals: the game saved (SaveSeq '{}') but the script found no save name - nothing written", saveSeq);
				} else {
					logger::debug("per-save signals: the game saved '{}' (SaveSeq '{}') - writing its per-save file", name, saveSeq);
					OnSaveGame(name);
				}
			}
			if (loadSeq != lastLoad) {
				std::string name;
				Read("LoadName", name);
				name = Clean(name);
				{
					std::scoped_lock l(g_lock);
					g_loadSeq = loadSeq;
					g_loadName = name;
					++g_loads;
				}
				if (name.empty()) {
					logger::debug("per-save signals: a new game started (LoadSeq '{}') - per-save values start empty", loadSeq);
					OnPostLoadGame(true);   // no captured name: starts empty, as Skyrim's new game
				} else {
					logger::debug("per-save signals: the game loaded '{}' (LoadSeq '{}') - reading its per-save file", name, loadSeq);
					OnLoadGame(name);
				}
			}
		}
	}

	void StartSaveSignals()
	{
		static std::once_flag s_once;
		std::call_once(s_once, [] {
			red3::AddFrameHook(&OnFrame);
			logger::info("per-save signals: watching ApocryphaMenuFramework.SaveSeq / LoadSeq (the framework's script)");
		});
	}

	std::string SaveSignalsJson()
	{
		const auto esc = [](const std::string& a_s) {
			std::string o;
			for (const char c : a_s) {
				if (c == '"' || c == '\\') { o += '\\'; }
				if (static_cast<unsigned char>(c) >= 0x20) { o += c; }
			}
			return o;
		};
		std::string written, read;
		LastFiles(written, read);
		std::scoped_lock l(g_lock);
		return std::string("{\"saveSeq\":\"") + esc(g_saveSeq) + "\",\"loadSeq\":\"" + esc(g_loadSeq) +
			   "\",\"saves\":" + std::to_string(g_saves) + ",\"loads\":" + std::to_string(g_loads) +
			   ",\"saveName\":\"" + esc(g_saveName) + "\",\"loadName\":\"" + esc(g_loadName) +
			   "\",\"lastWritten\":\"" + esc(written) + "\",\"lastRead\":\"" + esc(read) + "\"}";
	}
}
