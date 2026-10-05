#pragma once

// ============================================================================================================
// The Witcher 3 mods' own settings menus as AMF pages (M4; the owner, 2026-10-05: "make AMF read the config menus to
// make AMF pages so I can see them"). Every "Mods." group of every menu XML in bin\config\r4game\user_config_matrix\pc
// (Mod Organizer 2's virtual folder included) becomes a page: one AMF entry per mod, one tab per group. Labels come from
// the mods' string files; values from Documents\The Witcher 3\dx12user.settings, re-read when the game rewrites it.
// Shown read-only until the engine layer (M3) can set a value the way the game's own menu does.
// The parsing itself is ModMenusParse (also run outside the game by tools\modmenus_test.cpp).
// ============================================================================================================

#include <string>

namespace modmenus
{
	// Reads the menus on a background thread and registers the pages when done. Call once at start-up.
	void Start();

	// For amf.process op=modmenus: files read, pages built, label coverage, the settings files and timings.
	std::string StatusJson();
}
