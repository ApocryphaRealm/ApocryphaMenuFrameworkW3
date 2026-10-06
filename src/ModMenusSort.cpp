#include "PCH.h"

#include "ModMenus.h"

#include "Keyboard.h"
#include "Paths.h"
#include "Personalization.h"
#include "Registry.h"
#include "Red3.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>

#include <fstream>
#include <regex>
#include <unordered_set>

// Which mods' settings menus the Mod Control Panel lists, and the sort that puts them under a separator for their kind -
// the Witcher 3 counterparts of the Skyrim AMF's 2.1.0 MCM choice and MCM sort (source/McmSort.cpp there):
//  - xLenax on the Skyrim page, 2026-10-04: "I'd like to have an option to choose which MCMs I'd like to import instead of
//    importing all of them or None"; here a mod switched off simply stays in the game's own Options > Mods.
//  - the owner, 2026-10-05: "an auto sort function which sorted the imported menus into categories ... it can just sort by
//    name, it doesn't have to be perfect", and "if they ... sort it into the right one ... the sorter will acknowledge
//    that as a new rule automatically".
// The player's choices and the learned placements live beside User.ini (bin\x64_dx12\AMF), never in the download.

namespace modmenus
{
	namespace
	{
		namespace fs = std::filesystem;
		using strings::TR;

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Trim(std::string a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			return a_s.substr(b, a_s.find_last_not_of(" \t\r\n") - b + 1);
		}

		// ---- the import choice -----------------------------------------------------------------------------------
		// ModMenusImport.txt: "*new=1|0" (are mods not chosen by hand listed?) and "<mod key>=1|0" per mod chosen by hand.
		const fs::path& ImportPath() { static const fs::path p = paths::Data() / "ModMenusImport.txt"; return p; }

		std::mutex                            g_importLock;
		bool                                  g_importLoaded = false;
		bool                                  g_importNew = true;   // 2.1.0's default: everything comes in
		std::unordered_map<std::string, bool> g_choice;             // mod key -> listed

		void LoadImportLocked()
		{
			if (g_importLoaded) { return; }
			g_importLoaded = true;
			std::ifstream in(ImportPath());
			for (std::string line; std::getline(in, line);)
			{
				line = Trim(line);
				if (line.empty() || line[0] == ';') { continue; }
				const auto eq = line.rfind('=');
				if (eq == std::string::npos) { continue; }
				const std::string key = Trim(line.substr(0, eq));
				const bool on = Trim(line.substr(eq + 1)) != "0";
				if (key == "*new") { g_importNew = on; }
				else if (!key.empty()) { g_choice[key] = on; }
			}
			logger::info("mod menus: import choices read ({} chosen by hand; mods not chosen are {})", g_choice.size(),
				g_importNew ? "listed" : "left out");
		}

		void SaveImportLocked()
		{
			std::error_code ec;
			fs::create_directories(ImportPath().parent_path(), ec);
			std::ofstream out(ImportPath(), std::ios::trunc);
			out << "; Which mods' settings menus Apocrypha Menu Framework lists (Framework Settings > Mod menus).\n"
				   "; *new = whether mods not chosen below are listed; <mod>=1 listed, 0 left to the game's own Options > Mods.\n";
			out << "*new=" << (g_importNew ? 1 : 0) << '\n';
			std::vector<std::pair<std::string, bool>> rows(g_choice.begin(), g_choice.end());
			std::sort(rows.begin(), rows.end());
			for (const auto& [key, on] : rows) { out << key << '=' << (on ? 1 : 0) << '\n'; }
			if (!out) { logger::warn("mod menus: could not write {}", ImportPath().string()); }
		}

		bool ImportedLocked(const std::string& a_key)
		{
			LoadImportLocked();
			const auto it = g_choice.find(a_key);
			return it != g_choice.end() ? it->second : g_importNew;
		}

		void ShowMod(const ModInfo& a_mod, bool a_listed)
		{
			for (const std::string& page : a_mod.pages) { registry::SetPageVisible(a_mod.entry.c_str(), page.c_str(), a_listed); }
		}

		// ---- the category sort -----------------------------------------------------------------------------------
		struct RawRule
		{
			const char* pattern;
			const char* group;
			float       weight;
		};

		// The Witcher 3's own words, weighted above the general rules below so a Witcher name decides first. Written for the
		// Witcher's mods (2026-10-05) from the names of the menus on the owner's list: Auto Take All, Brothers In Arms,
		// Combat Resource Overhaul, Friendly HUD, Fast Travel Pack, Health Regeneration, More XP and Gold, ...
		constexpr RawRule kWitcherRules[] = {
			{ R"(\b(hud|ui|interface|menus?|radial|minimap|map|markers?|icons?|fonts?|inventory|tooltips?|notifications?|subtitles?|popups?)\b)", "Interface", 8.0f },
			{ R"(\b(controls?|keybinds?|key ?bindings?|inputs?|gamepad|controller|hotkeys?|keyboard|mouse)\b)", "Controls", 8.0f },
			{ R"(\b(camera|fov|field of view|first person|over the shoulder|zoom)\b)", "Camera", 8.0f },
			{ R"(\b(combat|stamina|damage|dodge|roll|parry|counter|crossbows?|swords?|enemy|enemies|scaling|downscaling|difficulty|armou?r|weapons?|attacks?)\b)", "Combat", 8.0f },
			{ R"(\b(animations?|anims?)\b)", "Animation", 8.0f },
			{ R"(\b(signs?|alchemy|potions?|oils?|bombs?|mutagens?|decoctions?|skills?|skill points?|perks?|abilities|toxicity|adrenaline)\b)", "MagicSkills", 8.0f },
			{ R"(\b(geralt|ciri|yennefer|triss|body|bodies|hair|beards?|faces?|appearance|outfits?|dyes?)\b)", "Characters", 8.0f },
			{ R"(\b(npcs?|companions?|followers?|monsters?|creatures?|horses?|roach|beasts?|bestiary)\b)", "NpcsCreatures", 8.0f },
			{ R"(\b(sounds?|music|voices?|audio)\b)", "Audio", 8.0f },
			{ R"(\b(quests?|contracts?|notice ?boards?|brothers in arms|treasure|hunts?|events?|places|locations)\b)", "QuestsPlaces", 8.0f },
			{ R"(\b(weather|lighting|grass|foliage|textures?|graphics|lod|shaders?|reshade|visuals?|water)\b)", "WorldVisuals", 8.0f },
			{ R"(\b(xp|experience|gold|loot|looting|take all|fast travel|travel|regen|regeneration|health|economy|prices?|weight|levels?|gwent|crafting|gameplay|survival|hunger)\b)", "Gameplay", 8.0f },
			{ R"(\b(fix|fixes|patch|patches|framework|library|debug|console|utility|utilities|tweaks?)\b)", "Utility", 8.0f },
		};

		// The general rules: MO2 Modlist Manager's, as the Skyrim AMF's MCM sort uses them.
		constexpr RawRule kGeneralRules[] = {
#include "ModMenuCategoryRules.inc"
		};

		struct Group
		{
			const char* key;
			const char* trKey;
			const char* english;
		};
		// The separators, in the order a sort makes them (the Skyrim sort's groups; magic is the Witcher's signs).
		constexpr Group kGroups[] = {
			{ "Interface", "AMF_ModCat_Interface", "Interface" },
			{ "Controls", "AMF_ModCat_Controls", "Controls" },
			{ "Camera", "AMF_ModCat_Camera", "Camera" },
			{ "Combat", "AMF_ModCat_Combat", "Combat" },
			{ "Animation", "AMF_ModCat_Animation", "Animation" },
			{ "MagicSkills", "AMF_ModCat_SignsSkills", "Signs, Alchemy and Skills" },
			{ "Characters", "AMF_ModCat_Characters", "Characters and Bodies" },
			{ "NpcsCreatures", "AMF_ModCat_NpcsCreatures", "NPCs, Companions and Creatures" },
			{ "Audio", "AMF_ModCat_Audio", "Audio" },
			{ "QuestsPlaces", "AMF_ModCat_QuestsPlaces", "Quests and Places" },
			{ "WorldVisuals", "AMF_ModCat_WorldVisuals", "World and Visuals" },
			{ "Gameplay", "AMF_ModCat_Gameplay", "Gameplay" },
			{ "Utility", "AMF_ModCat_Utility", "Utilities and Fixes" },
			{ "Other", "AMF_ModCat_Other", "Other" },
		};
		constexpr std::size_t kOther = std::size(kGroups) - 1;
		constexpr const char* kUndoPreset = "Before mod menu sort";

		struct Rule
		{
			std::regex  rx;
			std::size_t group;
			float       weight;
		};

		const std::vector<Rule>& Rules()
		{
			static std::once_flag    once;
			static std::vector<Rule> rules;
			std::call_once(once, [] {
				int  failed = 0;
				auto add = [&](const RawRule& a_raw) {
					const auto g = std::find_if(std::begin(kGroups), std::end(kGroups), [&](const Group& x) { return std::string_view(x.key) == a_raw.group; });
					if (g == std::end(kGroups)) { ++failed; return; }
					try {
						rules.push_back({ std::regex(a_raw.pattern, std::regex::ECMAScript | std::regex::icase | std::regex::optimize),
							static_cast<std::size_t>(g - std::begin(kGroups)), a_raw.weight });
					} catch (const std::regex_error& e) {
						logger::warn("mod sort: rule {} does not compile ({}) - skipped", a_raw.pattern, e.what());
						++failed;
					}
				};
				for (const RawRule& r : kWitcherRules) { add(r); }
				for (const RawRule& r : kGeneralRules) { add(r); }
				logger::info("mod sort: {} name rules ready ({} skipped)", rules.size(), failed);
			});
			return rules;
		}

		// "FriendlyHUD" -> also "Friendly HUD"; "modAutoTakeAll" -> "Auto Take All" (a menu file's mod prefix dropped).
		std::string Prepare(std::string a_name, bool a_split)
		{
			static const std::regex modPrefix(R"(^mod(?=[A-Z_]))");
			static const std::regex camel(R"(([a-z0-9])([A-Z]))");
			static const std::regex caps(R"(([A-Z]+)([A-Z][a-z]))");
			static const std::regex punct(R"([_\-.]+)");
			static const std::regex spaces(R"(\s+)");
			a_name = std::regex_replace(a_name, modPrefix, "");
			if (a_split)
			{
				a_name = std::regex_replace(a_name, camel, "$1 $2");
				a_name = std::regex_replace(a_name, caps, "$1 $2");
			}
			a_name = std::regex_replace(a_name, punct, " ");
			a_name = std::regex_replace(a_name, spaces, " ");
			return Trim(a_name);
		}

		std::string JudgedText(const ModInfo& a_mod)
		{
			std::vector<std::string> parts{ a_mod.entry, a_mod.key };
			for (const auto& n : a_mod.names) { parts.push_back(n); }
			std::string text;
			for (const auto& p : parts)
			{
				const std::string whole = Prepare(p, false);
				const std::string split = Prepare(p, true);
				text += (text.empty() ? "" : " | ") + whole + (split == whole ? std::string() : " | " + split);
			}
			return text;
		}

		std::size_t GroupOfRules(const ModInfo& a_mod)
		{
			static std::mutex                                   s_lock;
			static std::unordered_map<std::string, std::size_t> s_cache;
			{
				std::scoped_lock l(s_lock);
				if (const auto it = s_cache.find(a_mod.key); it != s_cache.end()) { return it->second; }
			}
			const std::string text = JudgedText(a_mod);
			std::vector<float>       score(std::size(kGroups), 0.0f);
			std::vector<bool>        voted(std::size(kGroups), false);
			std::vector<std::size_t> firstVote;   // a tie goes to the group that scored first
			for (const Rule& rule : Rules())
			{
				if (!std::regex_search(text, rule.rx)) { continue; }
				if (!voted[rule.group]) { firstVote.push_back(rule.group); }
				voted[rule.group] = true;
				score[rule.group] += rule.weight;
			}
			std::size_t best = kOther;
			for (const std::size_t g : firstVote) { if (best == kOther || score[g] > score[best]) { best = g; } }
			logger::debug("mod sort: '{}' judged by \"{}\" -> {}", a_mod.entry, text, kGroups[best].english);
			std::scoped_lock l(s_lock);
			s_cache[a_mod.key] = best;
			return best;
		}

		std::string GroupName(std::size_t a_group) { return TR(kGroups[a_group].trKey, kGroups[a_group].english); }

		// ---- learned placements: a mod the player moved under another category's separator ----
		const fs::path& LearnedPath() { static const fs::path p = paths::Data() / "ModMenusSortLearned.txt"; return p; }
		std::mutex                                   g_learnLock;
		std::unordered_map<std::string, std::string> g_learned;   // mod key -> group key
		bool                                         g_learnedLoaded = false;

		std::size_t GroupIndex(const std::string& a_key)
		{
			for (std::size_t i = 0; i < std::size(kGroups); ++i) { if (a_key == kGroups[i].key) { return i; } }
			return std::size(kGroups);
		}

		void LoadLearnedLocked()
		{
			if (g_learnedLoaded) { return; }
			g_learnedLoaded = true;
			std::ifstream in(LearnedPath());
			for (std::string line; std::getline(in, line);)
			{
				line = Trim(line);
				if (line.empty() || line[0] == ';') { continue; }
				const auto eq = line.rfind('=');
				if (eq == std::string::npos) { continue; }
				const std::string key = Trim(line.substr(0, eq));
				const std::string group = Trim(line.substr(eq + 1));
				if (!key.empty() && GroupIndex(group) < std::size(kGroups)) { g_learned[key] = group; }
			}
			if (!g_learned.empty()) { logger::info("mod sort: {} placement(s) learned from the player", g_learned.size()); }
		}

		void SaveLearnedLocked()
		{
			std::error_code ec;
			fs::create_directories(LearnedPath().parent_path(), ec);
			std::ofstream out(LearnedPath(), std::ios::trunc);
			out << "; Mods the player moved by hand under a category's separator - the sort follows these before its name rules.\n"
				   "; mod key = category. Delete a line (or the file) to let the name rules decide again.\n";
			std::vector<std::pair<std::string, std::string>> rows(g_learned.begin(), g_learned.end());
			std::sort(rows.begin(), rows.end());
			for (const auto& [key, group] : rows) { out << key << '=' << group << '\n'; }
		}

		std::size_t GroupOf(const ModInfo& a_mod)
		{
			{
				std::scoped_lock l(g_learnLock);
				LoadLearnedLocked();
				if (const auto it = g_learned.find(a_mod.key); it != g_learned.end())
				{
					if (const std::size_t g = GroupIndex(it->second); g < std::size(kGroups)) { return g; }
				}
			}
			return GroupOfRules(a_mod);
		}

		// The category a separator stands for: its name is a group's shown name, English name or key, letter case aside.
		std::size_t GroupOfSeparator(const std::string& a_name)
		{
			const std::string name = Lower(Trim(a_name));
			for (std::size_t i = 0; i < std::size(kGroups); ++i)
			{
				if (name == Lower(GroupName(i)) || name == Lower(kGroups[i].english) || name == Lower(kGroups[i].key)) { return i; }
			}
			return std::size(kGroups);
		}

		std::string FindSeparator(std::size_t a_group)
		{
			for (const auto& sep : personalization::Separators())
			{
				if (GroupOfSeparator(sep.name) == a_group) { return sep.id; }
			}
			return {};
		}

		// The one separator-name filter (Personalization has a single slot). Category separators show in the language picked;
		// everything else as the Renderer's filter does it (an unnamed separator, "New separator" in any shipped language).
		// Registered by ApplyImportChoices, after the Renderer's own registration at start-up, so this one is the one in use.
		std::string ShownSeparatorName(const std::string& a_stored)
		{
			if (const std::size_t g = GroupOfSeparator(a_stored); g < std::size(kGroups)) { return GroupName(g); }
			if (a_stored.empty()) { return TR("AMF_SeparatorUnnamed", "Separator"); }
			const std::string stored = Lower(a_stored);
			if (stored == "new separator") { return TR("AMF_SeparatorDefaultName", "New separator"); }
			for (const std::string& text : strings::EveryLanguage("AMF_SeparatorDefaultName"))
			{
				if (Lower(text) == stored) { return TR("AMF_SeparatorDefaultName", "New separator"); }
			}
			return a_stored;
		}

		void LearnFromLayout()
		{
			const auto mods = Mods();
			if (mods.empty()) { return; }
			std::unordered_map<std::string, const ModInfo*> byEntry;
			{
				std::scoped_lock l(g_importLock);
				for (const auto& m : mods) { if (ImportedLocked(m.key)) { byEntry[m.entry] = &m; } }
			}
			int learned = 0, forgot = 0;
			std::string currentSep;
			std::scoped_lock l(g_learnLock);
			LoadLearnedLocked();
			for (const auto& row : personalization::Order(registry::Snapshot()))
			{
				if (row.separator) { currentSep = row.displayName; continue; }
				const auto m = byEntry.find(row.modName);
				if (m == byEntry.end() || row.depth == 0) { continue; }
				const std::size_t g = GroupOfSeparator(currentSep);
				if (g >= std::size(kGroups)) { continue; }   // the player's own separator: not a category
				const std::size_t byRules = GroupOfRules(*m->second);
				const auto it = g_learned.find(m->second->key);
				if (g == byRules)
				{
					if (it != g_learned.end()) { g_learned.erase(it); ++forgot; }   // back where the rules put it
					continue;
				}
				if (it != g_learned.end() && it->second == kGroups[g].key) { continue; }
				g_learned[m->second->key] = kGroups[g].key;
				++learned;
				logger::info("mod sort: learned '{}' -> {} (moved there by hand; the rules said {})", row.modName, kGroups[g].english,
					kGroups[byRules].english);
			}
			if (learned > 0 || forgot > 0) { SaveLearnedLocked(); }
		}

		struct SortResult
		{
			std::size_t sorted = 0, moved = 0, kept = 0, separatorsMade = 0;
			bool        changed = false;
		};

		std::mutex       g_sortLock;
		std::atomic<int> g_undoAvailable{ -1 };   // -1 not read yet
		std::string      g_status;                 // the last sort's result, for the tab (render thread)
		std::atomic_bool g_statusStale{ false };   // a sort or undo ran through DevBench: the tab's line no longer applies

		SortResult SortIntoCategories(bool a_all)
		{
			LearnFromLayout();   // a mod moved by hand since the last look is remembered before this sort places it
			std::scoped_lock sortLock(g_sortLock);
			SortResult result;
			// Undo: the layout from before the sort, as an ordinary layout preset (deleted again when nothing changed).
			const std::string before = personalization::IniBlock();
			const bool        undoSaved = settings::SaveLayoutPreset(kUndoPreset);
			std::vector<registry::Entry> entries = registry::Snapshot();

			std::unordered_map<std::string, int>         depth;
			std::unordered_map<std::string, std::string> groupBefore;
			std::string current;
			for (const auto& row : personalization::Order(entries))
			{
				if (row.separator) { current = Lower(Trim(row.displayName)); continue; }
				depth[row.modName] = row.depth;
				groupBefore[row.modName] = row.depth > 0 ? current : std::string();
			}

			struct Move { std::string entry; std::size_t group; };
			std::vector<Move> moves;
			for (const auto& m : Mods())
			{
				{
					std::scoped_lock l(g_importLock);
					if (!ImportedLocked(m.key)) { continue; }
				}
				const auto at = depth.find(m.entry);
				if (at == depth.end()) { continue; }
				if (!a_all && at->second > 0) { ++result.kept; continue; }   // already under a separator: left alone
				moves.push_back({ m.entry, GroupOf(m) });
			}
			std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
				return a.group != b.group ? a.group < b.group : Lower(a.entry) < Lower(b.entry);
			});
			std::unordered_map<std::size_t, std::string> separatorOf;
			for (const Move& m : moves)
			{
				auto sep = separatorOf.find(m.group);
				if (sep == separatorOf.end())
				{
					std::string id = FindSeparator(m.group);
					if (id.empty())
					{
						id = personalization::AddSeparator(entries, "", kGroups[m.group].english);   // stored in English, shown translated
						++result.separatorsMade;
					}
					sep = separatorOf.emplace(m.group, id).first;
				}
				if (personalization::SendTo(entries, m.entry, sep->second))
				{
					++result.sorted;
					result.moved += groupBefore[m.entry] == Lower(GroupName(m.group)) || groupBefore[m.entry] == Lower(kGroups[m.group].english) ? 0 : 1;
				}
			}
			result.changed = personalization::IniBlock() != before;
			if (result.changed)
			{
				settings::Save();
				g_undoAvailable = undoSaved ? 1 : 0;
			}
			else if (undoSaved)
			{
				settings::DeleteLayoutPreset(kUndoPreset);
			}
			logger::info("mod sort ({}): {} mods sorted, {} changed group, {} left where they were, {} separators made{}",
				a_all ? "all" : "loose only", result.sorted, result.moved, result.kept, result.separatorsMade,
				result.changed ? "" : " - the list is as it was");
			return result;
		}

		bool CanUndo()
		{
			if (g_undoAvailable < 0)
			{
				const auto names = settings::ListLayoutPresets();
				g_undoAvailable = std::find(names.begin(), names.end(), kUndoPreset) != names.end() ? 1 : 0;
			}
			return g_undoAvailable > 0;
		}

		bool Undo()
		{
			std::scoped_lock sortLock(g_sortLock);
			const bool loaded = settings::LoadLayoutPreset(kUndoPreset);
			g_undoAvailable = 0;
			if (!loaded) { return false; }
			settings::DeleteLayoutPreset(kUndoPreset);
			logger::info("mod sort: the menu list is back to its order from before the sort");
			return true;
		}

		std::string Esc(const std::string& a_s)
		{
			std::string out;
			for (const char c : a_s) { if (c == '\\' || c == '"') { out += '\\'; } out += (static_cast<unsigned char>(c) < 0x20) ? ' ' : c; }
			return out;
		}

		std::string JsonStr(const std::string& a_json, const char* a_key)
		{
			const std::string needle = std::string("\"") + a_key + "\"";
			auto at = a_json.find(needle);
			if (at == std::string::npos) { return {}; }
			at = a_json.find(':', at + needle.size());
			if (at == std::string::npos) { return {}; }
			at = a_json.find_first_not_of(" \t", at + 1);
			if (at == std::string::npos) { return {}; }
			if (a_json[at] == '"') { const auto end = a_json.find('"', at + 1); return a_json.substr(at + 1, end - at - 1); }
			const auto end = a_json.find_first_of(",}", at);
			return Trim(a_json.substr(at, end - at));
		}
	}

	void ApplyImportChoices()
	{
		// the single separator-name filter now covers the category separators as well (see ShownSeparatorName)
		personalization::SetSeparatorNameFilter(&ShownSeparatorName);
		const auto mods = Mods();
		int listed = 0;
		std::scoped_lock l(g_importLock);
		for (const auto& m : mods)
		{
			const bool on = ImportedLocked(m.key);
			ShowMod(m, on);
			listed += on;
		}
		logger::info("mod menus: {} of {} mods listed in the Mod Control Panel", listed, mods.size());
		// learn from the player's own moves whenever the Menu list's layout changes (checked about once a second)
		static bool s_hooked = false;
		if (!s_hooked)
		{
			s_hooked = true;
			red3::AddFrameHook([] {
				static ULONGLONG   s_next = 0;
				static std::size_t s_seen = 0;
				const ULONGLONG    now = ::GetTickCount64();
				if (now < s_next) { return; }
				s_next = now + 1000;
				const std::size_t h = std::hash<std::string>{}(personalization::IniBlock());
				if (h != s_seen) { s_seen = h; LearnFromLayout(); }
			});
		}
	}

	std::string SortFromSideList()
	{
		SortResult r;
		try {
			r = SortIntoCategories(false);
		} catch (const std::exception& e) {
			// never let the sort take the game down from inside a frame: say what went wrong instead
			logger::error("mod sort: the side list's Sort failed ({}) - the menu list is unchanged where it had not been reached", e.what());
			g_status = TR("AMF_PresetNotLoaded", "Could not be read - see the log.");
			return g_status;
		}
		if (r.changed)
		{
			char text[256]{};
			std::snprintf(text, sizeof(text), TR("AMF_W3MenusSorted", "Sorted: %d moved, %d separators made."), static_cast<int>(r.moved),
				static_cast<int>(r.separatorsMade));
			g_status = text;
		}
		else
		{
			g_status = TR("AMF_W3MenusUnchanged", "Nothing to sort - every listed mod is already under a separator.");
		}
		logger::info("side list: Sort pressed - {}", g_status);
		return g_status;
	}

	void DrawSettingsTab()
	{
		if (g_statusStale.exchange(false)) { g_status.clear(); }   // set off-thread by SortToolJson; cleared here, on the render thread
		const auto mods = Mods();
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_W3MenusTitle", "Mod settings menus"));
		ImGui::TextDisabled("%s", TR("AMF_W3MenusHelp", "Each mod that adds a menu to the game's Options > Mods is listed in the Mod "
			"Control Panel as its own entry. Switch off the ones you do not want listed here; they stay in the game's own menu."));
		ImGui::PopTextWrapPos();
		if (mods.empty())
		{
			ImGui::TextDisabled("%s", TR("AMF_W3MenusNone", "No mod settings menus were found."));
			return;
		}

		// The import choices are drawn under g_importLock, and the lock is released BEFORE the sort section: the sort takes
		// the same (non-recursive) lock, and taking it twice on this thread threw out of the frame and std::terminate closed
		// the game with nothing logged (1.0.3 run, 2026-10-05: the Sort button killed the game; the DevBench op, which never
		// held the lock, worked).
		{
		std::scoped_lock l(g_importLock);
		LoadImportLocked();
		int listed = 0;
		for (const auto& m : mods) { listed += ImportedLocked(m.key); }
		ImGui::Text(TR("AMF_W3MenusCount", "%d of %d mods listed"), listed, static_cast<int>(mods.size()));

		bool importNew = g_importNew;
		if (widgets::Toggle(TR("AMF_W3MenusNew", "List new mods' menus as they appear"), &importNew))
		{
			g_importNew = importNew;
			SaveImportLocked();
			for (const auto& m : mods) { ShowMod(m, ImportedLocked(m.key)); }
		}
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", TR("AMF_W3MenusNewHelp", "On: a mod installed later is listed straight away. Off: only the mods "
			"switched on below are listed."));
		ImGui::PopTextWrapPos();

		static char filter[64] = "";
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
		ImGui::InputTextWithHint("##modfilter", TR("AMF_W3MenusFilter", "Filter by name"), filter, sizeof(filter));
		keyboard::NoteTextField(ImGui::GetItemID());
		ImGui::SameLine();
		const bool allOn = ImGui::Button(TR("AMF_W3MenusAllOn", "All on"));
		ImGui::SameLine();
		const bool allOff = ImGui::Button(TR("AMF_W3MenusAllOff", "All off"));
		const std::string needle = Lower(filter);
		bool changed = false;
		for (const auto& m : mods)
		{
			if (!needle.empty() && Lower(m.entry).find(needle) == std::string::npos) { continue; }
			bool on = ImportedLocked(m.key);
			if (allOn || allOff) { on = allOn; g_choice[m.key] = on; ShowMod(m, on); changed = true; continue; }
			if (widgets::Toggle((m.entry + "##imp." + m.key).c_str(), &on))
			{
				g_choice[m.key] = on;
				ShowMod(m, on);
				changed = true;
				logger::info("mod menus: '{}' {}", m.entry, on ? "listed" : "left to the game's own menu");
			}
		}
		if (changed) { SaveImportLocked(); }
		}

		ImGui::Separator();
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextDisabled("%s", TR("AMF_W3MenusSortHelp", "Sort into categories puts each listed mod that is not already under a "
			"separator under one for its kind - Interface, Combat, Gameplay and so on - judged by its name. Nothing is hidden or "
			"removed, and a mod you move by hand under another category is remembered for the next sort."));
		ImGui::PopTextWrapPos();
		if (ImGui::Button(TR("AMF_W3MenusSort", "Sort into categories")))
		{
			SortResult r;
			try {
				r = SortIntoCategories(false);
			} catch (const std::exception& e) {
				// never let the sort take the game down from inside a frame: say what went wrong instead
				logger::error("mod sort: the sort failed ({}) - the menu list is unchanged where it had not been reached", e.what());
				g_status = TR("AMF_PresetNotLoaded", "Could not be read - see the log.");
				return;
			}
			if (r.changed)
			{
				char text[256]{};
				std::snprintf(text, sizeof(text), TR("AMF_W3MenusSorted", "Sorted: %d moved, %d separators made."), static_cast<int>(r.moved),
					static_cast<int>(r.separatorsMade));
				g_status = text;
			}
			else
			{
				g_status = TR("AMF_W3MenusUnchanged", "Nothing to sort - every listed mod is already under a separator.");
			}
		}
		if (CanUndo())
		{
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_W3MenusUndo", "Undo the sort")))
			{
				g_status = Undo() ? TR("AMF_W3MenusUndone", "The list is back as it was before the sort.") : TR("AMF_PresetNotLoaded", "Could not be read - see the log.");
			}
		}
		if (!g_status.empty()) { ImGui::TextDisabled("%s", g_status.c_str()); }
	}

	std::string SortToolJson(const std::string& a_args)
	{
		const std::string action = JsonStr(a_args, "action").empty() ? "list" : JsonStr(a_args, "action");
		const auto        mods = Mods();
		std::string       extra;
		if (action == "set" || action == "all" || action == "new")
		{
			const bool on = JsonStr(a_args, "on") != "false" && JsonStr(a_args, "on") != "0";
			std::scoped_lock l(g_importLock);
			LoadImportLocked();
			if (action == "new") { g_importNew = on; }
			for (const auto& m : mods)
			{
				if (action == "all" || (action == "set" && m.key == JsonStr(a_args, "key"))) { g_choice[m.key] = on; }
				ShowMod(m, ImportedLocked(m.key));
			}
			SaveImportLocked();
		}
		else if (action == "run" || action == "all-sort")
		{
			const SortResult r = SortIntoCategories(action == "all-sort");
			g_statusStale = true;
			extra = std::format(R"(,"sorted":{},"moved":{},"kept":{},"separatorsMade":{},"changed":{})", r.sorted, r.moved, r.kept,
				r.separatorsMade, r.changed ? "true" : "false");
		}
		else if (action == "undo")
		{
			extra = std::string(R"(,"restored":)") + (Undo() ? "true" : "false");
			g_statusStale = true;
		}
		else if (action == "learned") { LearnFromLayout(); }
		else if (action != "list" && action != "preview")
		{
			return R"({"ok":false,"error":"modsort needs action list | set {key,on} | all {on} | new {on} | preview | run | all-sort | undo | learned"})";
		}

		std::string rows;
		{
			std::scoped_lock l(g_importLock);
			for (const auto& m : mods)
			{
				rows += std::format(R"({}{{"key":"{}","entry":"{}","listed":{},"category":"{}"}})", rows.empty() ? "" : ",", Esc(m.key),
					Esc(m.entry), ImportedLocked(m.key) ? "true" : "false", Esc(kGroups[GroupOf(m)].english));
			}
		}
		std::string learned;
		{
			std::scoped_lock l(g_learnLock);
			LoadLearnedLocked();
			for (const auto& [k, g] : g_learned) { learned += std::format(R"({}"{}":"{}")", learned.empty() ? "" : ",", Esc(k), Esc(g)); }
		}
		return std::format(R"({{"ok":true,"action":"{}","importNew":{},"mods":[{}],"learned":{{{}}},"undoAvailable":{}{}}})", action,
			g_importNew ? "true" : "false", rows, learned, CanUndo() ? "true" : "false", extra);
	}
}
