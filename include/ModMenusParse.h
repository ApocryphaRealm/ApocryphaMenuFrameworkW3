#pragma once

// ============================================================================================================
// The Witcher 3's mod settings menus, read as data (M4; the owner, 2026-10-05: "make AMF read the config menus to make
// AMF pages"). A mod adds its menu as an XML file in bin\config\r4game\user_config_matrix\pc\: each <Group> whose
// displayName starts "Mods." is one page of the game's Options > Mods menu, holding <Var>s of displayType TOGGLE,
// OPTIONS, SLIDER;min;max;steps and SUBTLE_SEPARATOR. The labels are localisation keys (panel_<path segment>,
// option_<var>, preset_<group path>); the values live in Documents\The Witcher 3\dx12user.settings as [GroupId]
// VarId=value. This half is pure parsing - no ImGui, no game - so tools\modmenus_test.cpp runs it on a mod folder.
// ============================================================================================================

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace modmenus
{
	struct Option
	{
		std::string label;   // a localisation key until resolved
		std::string value;   // the value its <Entry> sets on the var itself
	};

	struct Var
	{
		std::string         id;
		std::string         label;   // localisation key (displayName) until resolved
		std::string         type;    // TOGGLE, OPTIONS, SLIDER, SUBTLE_SEPARATOR, or whatever else a mod wrote
		double              min = 0.0, max = 1.0;
		int                 steps = 0;
		std::vector<Option> options;
	};

	struct Group
	{
		std::string              id;            // the settings-file section
		std::string              displayName;   // "Mods.CRO.Damage_Scaling"
		std::vector<std::string> path;          // {"Mods", "CRO", "Damage_Scaling"}
		std::string              file;          // the XML it came from, for the log
		std::vector<Var>         vars;
		std::size_t              modIndex = 1;  // path[modIndex] names the mod; the segments after it name the page (Place)
	};

	// Bytes of a file to UTF-8: UTF-16 LE/BE with or without a BOM (mods ship both) and UTF-8 with or without one.
	std::string DecodeText(std::string_view a_bytes);

	// Every group of a menu file, in file order. Tolerant like the game's own reader: comments, odd whitespace and a
	// missing closing tag do not lose the rest of the file. Vars marked visibilityCondition="hideAlways" are dropped.
	std::vector<Group> ParseMenuXml(std::string_view a_utf8, const std::string& a_file);

	// True for the groups the game lists under Options > Mods.
	bool IsModGroup(const Group& a_group);

	// Where one menu file's groups sit in AMF: the mod is the deepest segment every group of the file shares above its own
	// page name (Mods.FaenMods.ATA_Name.Page_General -> "ATA_Name"; Mods.CRO.Damage_Scaling -> "CRO"), never "Mods" itself.
	// A single-group file "Mods.FastTravelPack" is mod FastTravelPack with one page. Sets each group's modIndex.
	void Place(std::vector<Group>& a_groupsOfOneFile);

	// The localisation keys a group needs: panel_<segment> for each path segment after "Mods", option_<var>, and
	// each OPTIONS entry's key.
	std::vector<std::string> NeededKeys(const Group& a_group);

	// Labels found so far. A row names its key as text (key(str)) or only by the key's hash (key(hex)); both are kept.
	struct StringTable
	{
		std::unordered_map<std::string, std::string>   byKey;    // lower-case key -> text
		std::unordered_map<std::uint32_t, std::string> byHash;   // key hash -> text
	};

	// The hash the game stores for a string key (the key(hex) column, and the .w3strings key table): h = h*31 + c over the
	// UTF-16 units of the lower-cased key, no seed (Java's String.hashCode).
	std::uint32_t KeyHash(std::string_view a_key);

	// A compiled string file (.w3strings, magic "RTSW", versions 162-164): adds hash -> text for each key hash in
	// a_wantedHashes not already in a_out. Returns how many, or -1 when the file is not one it can read.
	int ReadW3Strings(std::string_view a_bytes, const std::unordered_map<std::uint32_t, bool>& a_wantedHashes, StringTable& a_out);

	// A W3 string CSV, in either shape mods ship: "id|hexkey|key|text", or "key|text" under an ";idspace=" header. Adds the
	// rows whose key is in a_wanted (lower case) and every hash-only row, never replacing one already there. Returns how many.
	std::size_t ReadStringsCsv(std::string_view a_utf8, const std::unordered_map<std::string, bool>& a_wanted, StringTable& a_out);

	// The language of a string CSV. Its file name wins ("en.csv", "strings_en.csv"): the ";meta[language=..]" line is copied
	// between files unchanged (Friendly HUD's cn.csv says en). "" when neither tells.
	std::string CsvLanguage(std::string_view a_utf8, const std::string& a_fileName);

	// A settings file: [Section] -> key -> value.
	using Settings = std::map<std::string, std::map<std::string, std::string>>;
	Settings ParseSettings(std::string_view a_utf8);

	// A label as text: the game's Flash-HTML (<font color=...>, <br>) removed, entities decoded, whitespace trimmed.
	std::string StripMarkup(std::string_view a_text);

	// A readable stand-in for a key no string table covers: "Sword_Attacks_Multiplier" -> "Sword Attacks Multiplier",
	// "FastTravelPack_EnableMod" -> "Fast Travel Pack Enable Mod".
	std::string Humanize(std::string_view a_id);

	// The slider's step and how many decimals show it: SLIDER;1;1.7;70 steps 0.01 -> 2 decimals.
	int SliderDecimals(const Var& a_var);

	// Resolved labels. Each lookup strips the markup and falls back to Humanize when no table has the key; a_found (optional)
	// says which happened.
	std::string PanelLabel(const std::string& a_segment, const StringTable& a_strings, bool* a_found = nullptr);
	std::string VarLabel(const Var& a_var, const StringTable& a_strings, bool* a_found = nullptr);
	std::string OptionLabel(const Option& a_option, const StringTable& a_strings, bool* a_found = nullptr);
}
