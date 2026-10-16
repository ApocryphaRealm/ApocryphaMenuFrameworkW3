#pragma once

// ============================================================================================================
// [Display] uDrawPath - THE RULE, with no game in it, so tools\draw_path_check.cpp runs the same code offline (1.0.7).
//
// What the setting picks: where the menu is drawn into each frame. 0 = at the game-facing Present, before NVIDIA Streamline
// and every overlay hooked into dxgi (so Steam's F12 screenshot shows the menu). 1 = at dxgi's own Present, after them
// (1.0.4, a Nexus report of an invisible menu; the 1.0.4 fix by 美猫少佐 (Bineko Major)). Under Wine / Proton only 1 shows the
// menu (Level12Lobster's notes for the Amethyst mod manager, Nexus, 2026-10-16), so 1.0.7 adds "auto", the new default:
// 0 on Windows, 1 under Wine.
//
// WHAT COUNTS AS THE PLAYER'S CHOICE - and why a 0 in the shipped file does not. How the files merge (Settings.cpp):
//   * ApocryphaMenuFramework.ini is the SHIPPED file. Every update replaces it. Versions 1.0.4-1.0.6 shipped it with
//     "uDrawPath=0" (line 89), documented as "0 = automatic", and the first-run migration of those versions wrote that same
//     line back into it. So a 0 found there is almost always the old default left behind - an install that kept its old
//     file, or a mod manager that does not overwrite INIs - and never a reason to keep a Wine player on the path that
//     draws nothing. It is read as auto. On Windows auto IS 0, so nothing changes there.
//   * User.ini holds only what the player changed (a value equal to the shipped one is not counted). No version ever
//     wrote uDrawPath into it - the key has no control in the menu, and Save() leaves out a key at its shipped value - so a
//     uDrawPath there was put there by hand. Both 0 and 1 in User.ini are honoured.
//   * A 1 is honoured wherever it is: that is the documented manual fix (Level12Lobster's line-89 edit to the shipped
//     file), and nothing ever wrote a 1 except a player.
//   * An older build's shipped file with no User.ini is moved into User.ini once (MigrateFromShipped); a uDrawPath=1 there
//     is moved (it is the player's), a uDrawPath=0 is not (it is the old default) - see IsLegacyShippedZero.
// So: to force the early path under Wine, put uDrawPath=0 in User.ini; to force dxgi's Present anywhere, uDrawPath=1 in
// either file.
// ============================================================================================================

#include <cstdint>
#include <string>
#include <string_view>

namespace drawpath::core
{
	inline constexpr std::int32_t kAuto = -1;   // the INI's "auto"

	// Where the value was read from (Settings.cpp's merge).
	enum class Source : std::uint8_t
	{
		kDefault,   // neither file carries the key: the compiled default (auto)
		kShipped,   // ApocryphaMenuFramework.ini, the file every update replaces
		kUser       // User.ini - the player's own (or moved there from an older build's shipped file)
	};

	inline const char* SourceName(Source a_s)
	{
		return a_s == Source::kUser ? "User.ini" : a_s == Source::kShipped ? "the shipped ApocryphaMenuFramework.ini" : "the compiled default";
	}

	inline std::string_view Trim(std::string_view a_t)
	{
		while (!a_t.empty() && (a_t.front() == ' ' || a_t.front() == '\t')) a_t.remove_prefix(1);
		while (!a_t.empty() && (a_t.back() == ' ' || a_t.back() == '\t' || a_t.back() == '\r')) a_t.remove_suffix(1);
		return a_t;
	}

	inline bool IsAutoText(std::string_view a_t)
	{
		a_t = Trim(a_t);
		if (a_t.size() != 4) {
			return false;
		}
		constexpr char kWord[] = "auto";
		for (std::size_t i = 0; i < 4; ++i) {
			const char c = (a_t[i] >= 'A' && a_t[i] <= 'Z') ? static_cast<char>(a_t[i] - 'A' + 'a') : a_t[i];
			if (c != kWord[i]) {
				return false;
			}
		}
		return true;
	}

	// The INI text as a setting: "auto" (any case) or empty -> kAuto; a whole number below 0 -> kAuto, 0 -> 0, 1 or more -> 1
	// (1.0.4-1.0.6 clamped to 0..1 the same way above 0). False for anything else (a_out is then kAuto, the default).
	inline bool Parse(std::string_view a_text, std::int32_t& a_out)
	{
		a_text = Trim(a_text);
		a_out = kAuto;
		if (a_text.empty() || IsAutoText(a_text)) {
			return true;
		}
		bool negative = false;
		if (a_text.front() == '-' || a_text.front() == '+') {
			negative = a_text.front() == '-';
			a_text.remove_prefix(1);
		}
		if (a_text.empty() || a_text.size() > 9) {
			return false;
		}
		long long v = 0;
		for (const char c : a_text) {
			if (c < '0' || c > '9') {
				return false;
			}
			v = v * 10 + (c - '0');
		}
		if (negative && v != 0) {
			a_out = kAuto;
		} else {
			a_out = v >= 1 ? 1 : 0;
		}
		return true;
	}

	// Which file the value counts as coming from, given Settings.cpp's merge: a_userSets = User.ini holds a value of the
	// player's for the key (one different from the shipped value, or one moved there from an older build's shipped file);
	// a_shippedHas = the shipped file carries the key.
	inline Source SourceOf(bool a_userSets, bool a_shippedHas)
	{
		return a_userSets ? Source::kUser : a_shippedHas ? Source::kShipped : Source::kDefault;
	}

	// How a setting is written back into an INI.
	inline std::string Text(std::int32_t a_setting) { return a_setting == 0 ? "0" : a_setting == 1 ? "1" : "auto"; }

	// The shipped file's 0 from 1.0.4-1.0.6: not the player's choice (see the header).
	inline bool IsLegacyShippedZero(std::string_view a_text)
	{
		std::int32_t v = kAuto;
		return Parse(a_text, v) && v == 0;
	}

	struct Decision
	{
		std::int32_t effective = 0;   // 0 or 1 - what the overlay does
		bool         automatic = false;   // true when it came from "auto" (or the old shipped 0 read as auto)
		bool         legacyZero = false;  // the shipped file's 0, read as auto
	};

	inline Decision Resolve(std::int32_t a_setting, Source a_source, bool a_wine)
	{
		Decision d;
		if (a_setting == 1) {
			d.effective = 1;   // the manual fix, honoured in either file
			return d;
		}
		if (a_setting == 0 && a_source == Source::kUser) {
			d.effective = 0;   // only a player's own edit puts a 0 in User.ini
			return d;
		}
		d.legacyZero = a_setting == 0;
		d.automatic = true;
		d.effective = a_wine ? 1 : 0;
		return d;
	}
}
