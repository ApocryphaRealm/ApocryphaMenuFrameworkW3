#pragma once

#include <cmath>

// ============================================================================================
// M2: the framework's own settings. Plain std::fstream file I/O ONLY - never the Win32 profile
// API (GetPrivateProfileString et al.), which is how the PrivateProfileRedirector stale-cache
// class of bug was born into six of this project's mods at once (fixed 2026-08-27; rule in
// plan.md decided requirement 7). Compiled defaults here MUST match the shipped INI exactly
// (project rule 16) - the shipped file lives at dist/bin/x64_dx12/AMF/ApocryphaMenuFramework.ini in this repo.
// ============================================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace settings
{
	// A REMEMBERED WINDOW GEOMETRY, held as FRACTIONS of the display so it survives a resolution
	// change, a different monitor, or borderless-to-fullscreen.
	//
	// There is one of these per way of opening the framework, and they are PROFILES rather than
	// presets (author, 2026-09-04: "lets have it treat them as profiles to save the users settings
	// to so that we set the default to vanilla positioning and size and if their ui mod does
	// different then they can change it and it will remember"). Each starts unset, which means
	// "use this profile's default" - the measured journal panel when nested, the centre anchor
	// otherwise. The moment the player moves or resizes the window, the result is stored here and
	// that profile stops taking the default. Vanilla's geometry is therefore a starting point, not
	// a cage: a menu replacer whose panel sits somewhere else only needs dragging once.
	struct WindowGeometry
	{
		float x = -1.0f;   // -1 on any field = never set by the player
		float y = -1.0f;
		float w = -1.0f;
		float h = -1.0f;
		// 1.7.8: the KEY-OPENED window keeps only x/y/w/h (a corner drag keeps its shape; nothing scales).
		// 1.8.1: the NESTED profile also remembers which journal ART it was dragged under (the owner,
		// 2026-09-13: the position must only follow the redesign while the redesign is active). A stored
		// position whose art is not the art on screen is ignored and the measured panel is used.
		std::string art;

		// A non-finite field (an inf written while the display size was 0) is "never set", so the default placement is used.
		bool IsSet() const
		{
			return std::isfinite(x) && std::isfinite(y) && std::isfinite(w) && std::isfinite(h) &&
				   x >= 0.0f && y >= 0.0f && w > 0.0f && h > 0.0f;
		}
		void Clear() { x = y = w = h = -1.0f; }
	};

	struct Values
	{
		// [Window] - one profile per way in; see WindowGeometry above.
		WindowGeometry nestedWindow;   // opened from the row in the game's System menu
		WindowGeometry hotkeyWindow;   // opened by the hotkey, or by a menu launcher through the API
		// [Window] bMovable and bFreeResize (Skyrim 2.1.1 - Barzing on Nexus, 2026-10-05: "the possibility to move the
		// window", "the possibility to resize window also in height size"; the owner: "seperate toggles" ... "in apperance
		// teb" ... "have it default to on"). Both on by default, matching the shipped INI (rule 16).
		bool movableWindow = true;    // bMovable - drag the top row (name and version); it reopens where it was left
		bool freeResize = true;       // bFreeResize - on, any edge or corner resizes freely; off, ImGuiWindowFlags_NoResize

		// [Input]
		std::int32_t toggleKey = 0x3B;   // DirectInput scan code; 0x3B = F1 (framework convention, the author 2026-08-27)
		// 1.8.9: the on-screen keyboard for controller players (the owner, 2026-09-18) - a key grid
		// across the bottom of the screen that types into whichever text box a page has highlighted.
		// See Keyboard.h. Off hides it entirely; consumer mods can still summon it by export.
		bool onScreenKeyboard = true;    // bOnScreenKeyboard

		// [Menu] bPauseGame (the owner, 2026-09-22: "next amf update gets a toggle in settings to stop time while
		// menu is active"): while this window is open the game is paused the way its own menus pause it - world
		// time, actors, weather and cooldowns stop. OFF by default, so nothing changes for anyone who does not turn
		// it on. Opened from the System row the game is already paused by the journal; this adds nothing there.
		bool pauseGameWhileOpen = false;
		// [Menu] bSkipIntro (the owner, 2026-10-05: "instead of a black curtain, let's have an option for a toggle that
		// turns off the intro to the game"): the game's start-up videos are skipped from the next start. AMF keeps the
		// game's hidden setting ApocryphaMenuFramework.SkipIntro equal to this; the framework's game script reads it.
		bool skipIntro = false;
		// [Menu] bKeepCameraAwake (the owner, 2026-09-29): while this window is open the game's idle vanity camera never
		// takes over (it rotates the view and hides the HUD, which is what a HUD mod's page is there to show); its own
		// timer is stopped while the window is open and restarted, as after the game's pause menu, when it closes.
		bool keepCameraAwake = true;

		// [Display]
		float textScale = 1.30f;         // extra font multiplier on top of the resolution scale (the author, 1.0.2 feedback round)
		// [Display] bSeeThrough and uWindowOpacity (Skyrim 2.1.1, Barzing on Nexus, 2026-10-05: "the semi transparence of
		// the window"; the owner: "seperate toggles" ... "see-through window at max opacity"): with See-through on, how
		// solid the window's background is, in percent, 5-100. Text, frames and the right-click menus fade less or not at
		// all (Theme.cpp). On at 100 by default, so it looks solid until lowered. Matches the shipped INI (rule 16).
		bool seeThrough = true;
		std::int32_t windowOpacity = 100;
		// Optional path to a .ttf to rasterise the menu text from. Empty = pick a clean system
		// face automatically. Set it to use any font, e.g. one that matches Skyrim's own lettering.
		std::string fontPath;
		// [Display] sLanguage - which translation file the framework's own text comes from
		// (Interface\Translations\ApocryphaMenuFramework_<language>.txt). Empty = follow the game's
		// sLanguage. Set from the Language combo on the Framework Settings page or here.
		std::string language;

		// Hang watchdog (the author, 2026-08-28): if the renderer stops producing frames for this many
		// seconds the game is treated as hung and the process terminates itself, so a wedged game
		// never needs Task Manager. Generous by default - a slow cell load still animates frames.
		// The row this framework adds to the GAME's own System menu, so mod settings are reached
		// where a player already looks for configuration rather than from a private hotkey.
		// Injected into the live menu at runtime, so it works over whatever menu artwork is
		// installed and collides with none of it.
		bool systemMenuRow = true;
		bool watchdogEnabled = true;
		// Fast exit (the author, 2026-09-05: "a way to deal with this on exit no kill function issue"):
		// when the game asks Windows to exit, end the process at once instead of running every
		// loaded DLL's and driver's shutdown code - the phase in which a game can wedge into a state
		// no kill, inside or outside the process, can reach. Nothing the game needs happens there.
		bool fastExit = true;
		// [Screenshot] sFolder (1.0.6): where the Screenshot control saves (see Screenshot.h). Empty = the game's
		// Data\AMF Screenshots, which Mod Organizer 2 puts in its overwrite. The control itself is in [Bindings].
		std::string   screenshotFolder;
		// No startup curtain on The Witcher 3 (the owner's decision): the [Startup] keys are neither read nor written.
		std::uint32_t watchdogSeconds = 120;
		std::int32_t windowPreset = 0;   // 0 = centre (the standard). Preset positions, never free placement -
		                                 // the author 2026-08-27, same anchor philosophy as the minimap; more presets later.

		// [Theme]
		std::string themeId = "oathvein";     // registry id (theme::Palette::id). Witcher 3 default: Oathvein (the owner, 2026-10-05: "for now")
		                                     // own Skyrim theme for the current test (the author,
		                                     // 2026-08-27) - "Untarnished" (the original identity)
		                                     // is still registered and selectable, just not default.

		// [Skin] - REPLACEMENT ARTWORK for the menu shell, so a UI author can make the framework
		// match their own interface (requested 2026-09-09 for borokoshow / Dragonborn UI). All
		// four are optional and independent; see Skin.h for what an author actually ships.
		// Every image is PNG - AMF decodes through WIC, which does not read DDS at all.
		// OFF BY DEFAULT, and that is the point. Art that is on unless you turn it off means a
		// player with no art replacer installed can end up looking at whatever placeholder PNGs
		// happen to be on disk - which is exactly what happened during development. A UI author
		// turning the feature on is one line; a player seeing art they never asked for is a bug.
		bool          skinEnabled = false; // bEnabled - master switch for everything in [Skin]
		std::string   skinFrame;            // sFrame - nine-slice frame PNG, transparent centre
		std::uint32_t skinFrameCorner = 64; // uFrameCorner - corner slice in px (192x192/64 suggested)
		std::string   skinBackground;       // sBackground - tiled if <= 512px both sides, else stretched
		std::string   skinPlates;           // sPlates - folder holding toggle.png / slider.png / tab.png

		// [Debug]

		// [Log]
		std::int32_t logLevel = 2;       // spdlog level: 2 = info (the shipped default since 2026-09-26; 0 = trace for a bug report)
	};

	// The live values. Read freely from any thread; written by Load() at plugin init and by the
	// settings page on the render thread. Torn reads of a float/int are acceptable here (no
	// value is multi-word), so no lock - matching how every mod in this project treats INI state.
	Values& Get();

	// Reads the shipped bin\x64_dx12\AMF\ApocryphaMenuFramework.ini as the defaults, then the player's
	// bin\x64_dx12\AMF\User.ini over it (plain file reads - the files are the source of truth, rule 16's persistence
	// half). A missing file or key keeps the compiled default and logs which happened. Applies the log level.
	// The first time it runs with no User.ini, values an earlier build saved INTO the shipped file are moved to
	// User.ini (see MigrateFromShipped in Settings.cpp).
	void Load();

	// Writes User.ini, comments included, so a settings-page change survives the next game load (rule 16) AND the
	// next update - the download never ships User.ini (Skyrim 2.0.3). It holds only what the player changed (Skyrim
	// 2.0.5): a scalar key at the shipped value (or the compiled default, for a key the shipped file lacks) is left
	// out, so the shipped file goes on deciding it. The list-shaped sections are written whole. Logs on failure,
	// never throws.
	void Save();

	// THE MENU KEY (Skyrim 2.0.5 / 2.1.1). [Input] uToggleKey and Controls' "Open and close the menu" are one key;
	// where its value came from on the last load or save, for the DevBench report and the log.
	enum class ToggleKeySource : int
	{
		kDefault = 0,    // the compiled default - neither file has uToggleKey
		kShipped,        // the shipped ApocryphaMenuFramework.ini
		kUser,           // User.ini's uToggleKey, which differs from the shipped value
		kUserControls,   // User.ini's [Bindings] sToggleMenu, set on the Controls page
		kFallback,       // the value given was not a usable key, so F1 (or no key, if F1 is taken)
	};
	ToggleKeySource GetToggleKeySource();
	const char* ToggleKeySourceName(ToggleKeySource a_source);
	// Moves the menu key and saves (the DevBench rebind op). False (nothing changed) for Escape, a code no key can
	// have, or a key another function already holds.
	bool SetToggleKey(std::int32_t a_scancode);

	// Menu-list layout presets (Skyrim 2.0.3): the order, separators, favourites and renames saved under a name in
	// bin\x64_dx12\AMF\Presets\<name>.ini. Names are cleaned to letters, digits, spaces and - _ ' ( ); loading one
	// replaces the current layout and saves it as the player's. Every preset can be deleted (the owner's rule).
	std::vector<std::string> ListLayoutPresets();
	bool SaveLayoutPreset(const std::string& a_name);
	bool LoadLayoutPreset(const std::string& a_name);
	bool DeleteLayoutPreset(const std::string& a_name);
}
