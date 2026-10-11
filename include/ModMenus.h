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
		// 1.0.5 (plan row 52): where it sits in the game's Options tree - its groups' display paths up to the segment that
		// names the mod ("Mods.CRO", "Mods.FaenMods.ATA_Name"), each once. What "here only" takes out of Options > Mods.
		std::vector<std::string> menuPaths;
	};
	std::vector<ModInfo> Mods();   // empty until the menus have been read

	// Shows or hides each mod's pages as the player chose (ModMenusImport.txt). Called once the pages are registered.
	void ApplyImportChoices();

	// Framework Settings > Mod menus: which mods' menus are listed, and the sort into categories.
	void DrawSettingsTab();

	// The side list's Sort button (the Mods row): the same sort as the tab's "Sort into categories", guarded; returns the
	// status line, which the Mod menus tab shows too.
	std::string SortFromSideList();

	// amf.process op=modsort (args action: list | set {key,on} or {key,place} | all {on} | new {on} | preview | run | all-sort |
	// undo | learned).
	std::string SortToolJson(const std::string& a_argsJson);

	// PLACEMENT (1.0.5, plan row 52): per mod, 1 = here and in the game's menu, 0 = the game's menu only, 2 = here only.
	// "Here only" mods' menu paths, joined with ';', for the hidden setting ApocryphaMenuFramework.HiddenModMenus, which the
	// framework's script reads in CR4IngameMenu.showOptionsPanel and leaves those mods out of the game's Options > Mods.
	// Any thread (its own lock). Empty = none. False until the choices were applied once (then the value means nothing yet).
	bool HereOnlyPaths(std::string& a_out);
	// For op=modmenus: every mod's placement, the paths wanted and the setting's value as last read on the game thread.
	std::string PlacementJson();
	// The game thread's last read of ApocryphaMenuFramework.HiddenModMenus (ModMenus.cpp), for PlacementJson.
	void NoteHiddenVar(const std::string& a_value);
}
