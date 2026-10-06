#pragma once

// ============================================================================================================
// The Witcher 3 mods' own settings menus as AMF pages (M4; the owner, 2026-10-05: "make AMF read the config menus to
// make AMF pages so I can see them"). Every "Mods." group of every menu XML in bin\config\r4game\user_config_matrix\pc
// (Mod Organizer 2's virtual folder included) becomes a page: one AMF entry per mod, one tab per group. Labels come from
// the mods' string files; values from Documents\The Witcher 3\dx12user.settings, re-read when the game rewrites it.
// A change is set through the game's own config code (Red3, the engine bridge); read-only only if that is not found.
// The parsing itself is ModMenusParse (also run outside the game by tools\modmenus_test.cpp).
// ============================================================================================================

#include <string>
#include <vector>

namespace modmenus
{
	// Reads the menus on a background thread and registers the pages when done. Call once at start-up.
	void Start();

	// For amf.process op=modmenus: files read, pages built, label coverage, the settings files and timings.
	std::string StatusJson();

	// ---- the import choice and the category sort (ModMenusSort.cpp; Skyrim AMF 2.1.0's MCM choice and sort) ----
	struct ModInfo
	{
		std::string              key;     // stable id: the menu-path segment that names the mod ("CRO", "ATA_Name")
		std::string              entry;   // the name the Mod Control Panel lists it under
		std::vector<std::string> names;   // more names to judge its kind by: the stems of its menu files
		std::vector<std::string> pages;   // its pages (tabs), as registered
	};
	std::vector<ModInfo> Mods();   // empty until the menus have been read

	// Shows or hides each mod's pages as the player chose (ModMenusImport.txt). Called once the pages are registered.
	void ApplyImportChoices();

	// Framework Settings > Mod menus: which mods' menus are listed, and the sort into categories.
	void DrawSettingsTab();

	// The side list's Sort button (the Mods row): the same sort as the tab's "Sort into categories", guarded; returns the
	// status line, which the Mod menus tab shows too.
	std::string SortFromSideList();

	// amf.process op=modsort (args action: list | set {key,on} | all {on} | new {on} | preview | run | all-sort | undo | learned).
	std::string SortToolJson(const std::string& a_argsJson);
}
