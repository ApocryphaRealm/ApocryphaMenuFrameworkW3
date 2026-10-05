// Runs the Witcher 3 mod-menu reader (src/ModMenusParse.cpp) over a Mod Organizer 2 mods folder, outside the game, and
// prints every page AMF would build from it: mod, page, each setting's label (and whether a string table supplied it),
// type, range or choices, and its value when a settings file is given. Exit code 1 when a menu file yields no groups.
//
//   modmenus_test "<...\The Witcher 3 MO2\mods>" ["<...\Documents\The Witcher 3\user.settings>"]

#include "ModMenusParse.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
	std::string ReadFile(const fs::path& a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), {});
	}

	std::string LowerPath(const fs::path& a_path)
	{
		std::string s = a_path.generic_string();
		for (char& c : s) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return s;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::puts("usage: modmenus_test <mods folder> [settings file]");
		return 2;
	}
	const fs::path       root = argv[1];
	modmenus::Settings   values;
	if (argc > 2) {
		values = modmenus::ParseSettings(modmenus::DecodeText(ReadFile(argv[2])));
		std::printf("settings: %zu sections from %s\n", values.size(), argv[2]);
	}

	int failures = 0, mods = 0, groupsTotal = 0, varsTotal = 0, labelled = 0;
	for (const auto& modDir : fs::directory_iterator(root)) {
		if (!modDir.is_directory()) {
			continue;
		}
		std::vector<modmenus::Group> groups;
		std::vector<fs::path>        csvs;
		std::vector<fs::path>        w3s;
		for (const auto& e : fs::recursive_directory_iterator(modDir.path(), fs::directory_options::skip_permission_denied)) {
			if (!e.is_regular_file()) {
				continue;
			}
			const std::string p = LowerPath(e.path());
			if (p.ends_with(".xml") && p.find("/user_config_matrix/pc/") != std::string::npos) {
				auto parsed = modmenus::ParseMenuXml(modmenus::DecodeText(ReadFile(e.path())), e.path().filename().string());
				std::vector<modmenus::Group> mine;
				for (auto& g : parsed) {
					if (modmenus::IsModGroup(g)) {
						mine.push_back(std::move(g));
					}
				}
				const std::size_t kept = mine.size();
				modmenus::Place(mine);
				for (auto& g : mine) {
					groups.push_back(std::move(g));
				}
				if (parsed.empty()) {
					std::printf("  !! %s: no groups parsed\n", e.path().string().c_str());
					++failures;
				} else if (!kept) {
					std::printf("  (%s: %zu groups, none under Mods. - not a mod menu)\n", e.path().filename().string().c_str(), parsed.size());
				}
			} else if (p.ends_with(".csv")) {
				csvs.push_back(e.path());
			} else if (p.ends_with("/en.w3strings")) {
				w3s.push_back(e.path());
			}
		}
		if (groups.empty()) {
			continue;
		}
		++mods;

		std::unordered_map<std::string, bool> wanted;
		for (const auto& g : groups) {
			for (const auto& k : modmenus::NeededKeys(g)) {
				std::string low = k;
				for (char& c : low) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				wanted[low] = true;
			}
		}
		modmenus::StringTable strings;   // labels
		for (int pass = 0; pass < 2; ++pass) {   // English first, then anything else fills gaps
			for (const auto& c : csvs) {
				const std::string text = modmenus::DecodeText(ReadFile(c));
				const bool        en = modmenus::CsvLanguage(text, c.filename().string()) == "en";
				if ((pass == 0) == en) {
					modmenus::ReadStringsCsv(text, wanted, strings);
				}
			}
		}

		std::unordered_map<std::uint32_t, bool> wantedHashes;
		for (const auto& [k, _] : wanted) {
			wantedHashes[modmenus::KeyHash(k)] = true;
		}
		for (const auto& w : w3s) {
			if (modmenus::ReadW3Strings(ReadFile(w), wantedHashes, strings) < 0) {
				std::printf("  !! %s: not a readable .w3strings\n", w.string().c_str());
				++failures;
			}
		}

		std::printf("\n== %s  (%zu groups, %zu csv, %zu w3strings, %zu labels found)\n", modDir.path().filename().string().c_str(), groups.size(),
			csvs.size(), w3s.size(), strings.byKey.size() + strings.byHash.size());
		for (const auto& g : groups) {
			++groupsTotal;
			const std::string mod = g.path.size() > g.modIndex ? modmenus::PanelLabel(g.path[g.modIndex], strings) : g.id;
			std::string       page;
			for (std::size_t i = g.modIndex + 1; i < g.path.size(); ++i) {
				page += (page.empty() ? "" : " / ") + modmenus::PanelLabel(g.path[i], strings);
			}
			if (page.empty()) {
				page = "Settings";
			}
			std::printf("  [%s] %s -> %s   (group %s, %s)\n", g.displayName.c_str(), mod.c_str(), page.c_str(), g.id.c_str(), g.file.c_str());
			const auto sec = values.find(g.id);
			for (const auto& v : g.vars) {
				++varsTotal;
				if (v.type == "SUBTLE_SEPARATOR") {
					std::puts("      ----");
					continue;
				}
				bool              found = false;
				const std::string label = modmenus::VarLabel(v, strings, &found);
				labelled += found;
				std::string value = "(not set)";
				if (sec != values.end()) {
					if (const auto it = sec->second.find(v.id); it != sec->second.end()) {
						value = it->second;
					}
				}
				std::printf("      %s %-50s %-8s", found ? " " : "~", label.c_str(), v.type.c_str());
				if (v.type == "SLIDER") {
					std::printf(" %g..%g /%d (%d dp)", v.min, v.max, v.steps, modmenus::SliderDecimals(v));
				}
				for (const auto& o : v.options) {
					std::printf(" [%s=%s]", o.value.c_str(), modmenus::OptionLabel(o, strings).c_str());
				}
				std::printf("  = %s\n", value.c_str());
			}
		}
	}
	std::printf("\n%d mods, %d groups, %d settings, %d labelled from a string table (~ = readable id), %d parse failures\n", mods,
		groupsTotal, varsTotal, labelled, failures);
	return failures ? 1 : 0;
}
