#include "Paths.h"
#include "Settings.h"

#include "Bindings.h"
#include "Personalization.h"
#include "Theme.h"
#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace settings
{
	namespace
	{
		// SETTINGS SURVIVE UPDATES (Skyrim 2.0.3 / 2.0.5, carried to The Witcher 3). Three places, all beside the exe
		// in bin\x64_dx12\AMF - paths::Data(), which under Mod Organizer 2 is the merged virtual folder:
		//   the SHIPPED file - the defaults, replaced by every update; read, never written by a setting change;
		//   the PLAYER'S file, User.ini - only what the player changed in the menu (theme, text size, window, keys,
		//     and the mod list's order, separators, favourites and renames). The download never contains it, so no
		//     update can replace it. Under MO2 a file no mod provides is created in overwrite, which a reinstall or an
		//     update of AMF's own mod does not touch; under a manual install it is a file no installer ever wrote;
		//   Presets\ - saved menu-list layouts, one INI each. Never shipped either.
		const std::filesystem::path& IniPath() { static const auto p = paths::Data() / "ApocryphaMenuFramework.ini"; return p; }
		const std::filesystem::path& UserPath() { static const auto p = paths::Data() / "User.ini"; return p; }
		const std::filesystem::path& PresetDir() { static const auto p = paths::Data() / "Presets"; return p; }
		// Where an older build's settings-in-the-shipped-file are kept after they are moved to User.ini (MigrateFromShipped).
		const std::filesystem::path& MigratedPath() { static const auto p = paths::Data() / "ApocryphaMenuFramework.ini.migrated"; return p; }

		std::string PathText(const std::filesystem::path& a_path) { return a_path.string(); }

		Values g_values;

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) a_text.remove_suffix(1);
			return a_text;
		}

		// key -> raw value text, sections flattened ("Input.uToggleKey"). A tiny parser is all
		// an INI this size needs, and it keeps the file the single source of truth.
		std::unordered_map<std::string, std::string> ParseFile(std::istream& a_file)
		{
			std::unordered_map<std::string, std::string> entries;
			std::string line;
			std::string section;

			while (std::getline(a_file, line))
			{
				const std::string_view trimmed = Trim(line);

				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#')
				{
					continue;
				}

				if (trimmed.front() == '[' && trimmed.back() == ']')
				{
					section = std::string(Trim(trimmed.substr(1, trimmed.size() - 2)));
					continue;
				}

				const auto equals = trimmed.find('=');
				if (equals == std::string_view::npos)
				{
					continue;
				}

				const std::string key = section + "." + std::string(Trim(trimmed.substr(0, equals)));
				entries[key] = std::string(Trim(trimmed.substr(equals + 1)));
			}

			return entries;
		}

		// A WHOLE NUMBER, IN DECIMAL OR HEX (Skyrim 2.0.5). The parse must consume the whole text: from_chars stops at
		// the first character it cannot use and still reports success, so "0x3B" read as 0 - the menu key silently
		// became "no key" (HadToRegister's report). "0x"/"0X" switches to base 16.
		bool ParseInt(std::string_view a_text, long long& a_out)
		{
			a_text = Trim(a_text);
			bool negative = false;
			if (!a_text.empty() && (a_text.front() == '-' || a_text.front() == '+'))
			{
				negative = a_text.front() == '-';
				a_text.remove_prefix(1);
			}
			int base = 10;
			if (a_text.size() > 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X'))
			{
				base = 16;
				a_text.remove_prefix(2);
			}
			if (a_text.empty()) { return false; }
			long long value = 0;
			const char* const last = a_text.data() + a_text.size();
			const auto result = std::from_chars(a_text.data(), last, value, base);
			if (result.ec != std::errc{} || result.ptr != last) { return false; }
			a_out = negative ? -value : value;
			return true;
		}

		bool ParseDouble(std::string_view a_text, double& a_out)
		{
			a_text = Trim(a_text);
			if (long long whole = 0; ParseInt(a_text, whole)) { a_out = static_cast<double>(whole); return true; }
			if (a_text.empty()) { return false; }
			const char* const last = a_text.data() + a_text.size();
			const auto result = std::from_chars(a_text.data(), last, a_out);
			return result.ec == std::errc{} && result.ptr == last;
		}

		// Two INI values are the same setting when they are the same number ("59", "0x3B", "59.0") or, for text,
		// the same text. What decides whether User.ini is pinning a value the shipped file already gives.
		bool SameValue(std::string_view a_left, std::string_view a_right)
		{
			double l = 0.0, r = 0.0;
			if (ParseDouble(a_left, l) && ParseDouble(a_right, r))
			{
				if (!std::isfinite(l) || !std::isfinite(r)) { return l == r; }
				const double scale = std::max(1.0, std::max(std::abs(l), std::abs(r)));
				return std::abs(l - r) <= 1e-6 * scale;
			}
			return Trim(a_left) == Trim(a_right);
		}

		template <class T>
		void ReadNumber(const std::unordered_map<std::string, std::string>& a_entries, const char* a_key, T& a_out)
		{
			const auto it = a_entries.find(a_key);
			if (it == a_entries.end())
			{
				logger::debug("settings: {} not present in the INI; keeping compiled default", a_key);
				return;
			}

			if constexpr (std::is_same_v<T, float>)
			{
				double parsed = 0.0;
				if (ParseDouble(it->second, parsed)) { a_out = static_cast<float>(parsed); }
				else { logger::warn("settings: {} = \"{}\" is not a number; keeping {}", a_key, it->second, a_out); }
			}
			else
			{
				long long parsed = 0;
				if (ParseInt(it->second, parsed) && parsed >= static_cast<long long>(std::numeric_limits<T>::min()) &&
					parsed <= static_cast<long long>(std::numeric_limits<T>::max()))
				{
					a_out = static_cast<T>(parsed);
				}
				else
				{
					logger::warn("settings: {} = \"{}\" is not a whole number (decimal, or hex as 0x3B); keeping {}",
								 a_key, it->second, a_out);
				}
			}
		}

		void ReadBool(const std::unordered_map<std::string, std::string>& a_entries, const char* a_key, bool& a_out)
		{
			std::int32_t number = a_out ? 1 : 0;
			ReadNumber(a_entries, a_key, number);
			a_out = number != 0;
		}
	}

	Values& Get()
	{
		return g_values;
	}

	namespace
	{
		// The shipped file's opening comment. The same text heads the packaged INI (dist\bin\x64_dx12\AMF), so the copy
		// MigrateFromShipped puts back is the file the download ships.
		constexpr const char* kShippedHeader =
			"; ============================================================================\n"
			"; ApocryphaRealm Menu Framework - settings.\n"
			";\n"
			"; F1 opens the framework as its own centred window; uToggleKey=0 gives the key\n"
			"; back to the game. The Witcher 3 build adds no row to the game's own menus.\n"
			";\n"
			"; WHERE YOUR SETTINGS GO. This file holds the defaults and is never written.\n"
			"; Settings you change in the menu are saved to User.ini beside this file\n"
			"; (bin\\x64_dx12\\AMF\\User.ini), which holds only what you changed and wins over\n"
			"; this file; layout presets go in AMF\\Presets. The download never contains\n"
			"; either, so an update cannot replace them (under Mod Organizer 2 they are in\n"
			"; overwrite). To set something by hand, edit User.ini - or this file, for\n"
			"; anything you have not changed in the menu. Delete User.ini to go back to\n"
			"; these defaults. The log says which file each setting came from.\n"
			"; ============================================================================\n"
			"\n";

		// Every scalar setting as INI text with its comments, for the given values. Save() writes it for the live
		// values and leaves out each key at its shipped value; Load() renders it for a default Values to learn the
		// compiled defaults of keys the shipped file does not carry (Skyrim 2.0.5). Rendered for a default Values,
		// after kShippedHeader, it IS the shipped INI (rule 16: the two are one text).
		std::string ScalarBlock(const Values& a_v)
		{
			std::ostringstream file;
			file << "[Menus]\n"
				"; Not on The Witcher 3: this build adds no row to the game's own menus, so this value\n"
				"; does nothing here. The menu opens from its key (uToggleKey, F1).\n"
				"bSystemMenuRow=" << (a_v.systemMenuRow ? 1 : 0) << "\n"
				"\n"
				"[Input]\n"
				"; DirectInput scan code that toggles the framework menu, decimal or hex: 59 (0x3B) = F1.\n"
				"; 0 = no key at all, which is the way to leave F1 entirely to the game. The same key as\n"
				"; Controls > Open and close the menu; a code no key can have falls back to F1.\n"
				"uToggleKey=" << a_v.toggleKey << "\n"
				"; 1 = the on-screen keyboard for controller players: highlight a text box and press A,\n"
				"; and a key grid appears across the bottom of the screen; the D-pad walks it, A types,\n"
				"; B goes back to the box. 0 turns it off.\n"
				"bOnScreenKeyboard=" << (a_v.onScreenKeyboard ? 1 : 0) << "\n"
				"; Keyboard or controller navigation is DETECTED from whatever you last used, and is\n"
				"; not a setting: press a key or move the mouse for keyboard navigation, touch the\n"
				"; pad for controller navigation. The menu shows which one it is reading.\n"
				"\n"
				"[Menu]\n"
				"; 1 = pause the game while this menu is open, the way the game's own menus do: world time,\n"
				"; actors and weather stop until it closes. 0 (the default) leaves the game running behind it.\n"
				"bPauseGame=" << (a_v.pauseGameWhileOpen ? 1 : 0) << "\n"
				"; 1 = skip the game's start-up videos (disclaimer, legal notice, logos and the story recap) and\n"
				"; go straight to its main menu, from the next start. 0 (the default) plays them as usual.\n"
				"bSkipIntro=" << (a_v.skipIntro ? 1 : 0) << "\n"
				"; 1 = skip the story recap video the game plays on the loading screen when a save loads (its\n"
				"; engine setting [LoadingScreen/Debug] DisableVideos, set through the game's settings code).\n"
				"; 0 (the default) leaves it alone; switching it off in the menu turns the recap back on.\n"
				"bSkipLoadingRecap=" << (a_v.skipLoadingRecap ? 1 : 0) << "\n"
				"; Not used on The Witcher 3.\n"
				"bKeepCameraAwake=" << (a_v.keepCameraAwake ? 1 : 0) << "\n"
				"\n"
				"[Display]\n"
				"; Extra text scale on top of the automatic resolution scaling.\n"
				"fTextScale=" << a_v.textScale << "\n"
				"; See-through window (0/1): on, uWindowOpacity below fades the menu's background (100 = solid,\n"
				"; the default). Off: the background is always solid.\n"
				"bSeeThrough=" << (a_v.seeThrough ? 1 : 0) << "\n"
				"; How solid the menu is, in percent (5-100): 100 is solid, lower lets the game show through. The\n"
				"; black background fades by the full amount, boxes and borders much less, text least; right-click\n"
				"; menus stay solid.\n"
				"uWindowOpacity=" << a_v.windowOpacity << "\n"
				"; Optional .ttf to rasterise the menu text from. Empty = a clean system font.\n"
				"; Fonts dropped into AMF\\fonts are listed on the settings page.\n"
				"sFontPath=" << a_v.fontPath << "\n"
				"; Language of the framework's own text: empty = the game's text language, set in its own\n"
				"; options (your Windows language if AMF has no file for it); or a translation file's name -\n"
				"; english, german, french, spanish, italian, russian, polish, czech, japanese, korean,\n"
				"; chinese (AMF\\Translations\\ApocryphaMenuFramework_<language>.txt).\n"
				"sLanguage=" << a_v.language << "\n"
				"; Window position preset. 0 = centre: where the menu opens until it is moved (bMovable).\n"
				"uWindowPreset=" << a_v.windowPreset << "\n"
				"\n"
				"[Window]\n"
				"; Move the window (0/1): on, drag its top row (the name and version) to move it, and it opens where it\n"
				"; was left. Off: it sits in the middle of the screen. On is the default.\n"
				"bMovable=" << (a_v.movableWindow ? 1 : 0) << "\n"
				"; Resize the window (0/1): on, drag any edge or corner to resize it, freely. Off: its size is fixed.\n"
				"; On is the default.\n"
				"bFreeResize=" << (a_v.freeResize ? 1 : 0) << "\n"
				"; Where the menu window was left (fHotkey*), as FRACTIONS of the screen so the numbers\n"
				"; stay right at any resolution. -1 means it has never been moved, so it opens in the\n"
				"; centre of the screen. Move or resize it and it is remembered here; the settings page\n"
				"; can reset it. The fNested* keys and sNestedArt belong to a row in the game's own menu,\n"
				"; which The Witcher 3 build does not have; they are not used here.\n"
				"fNestedX=" << a_v.nestedWindow.x << "\n"
				"fNestedY=" << a_v.nestedWindow.y << "\n"
				"fNestedW=" << a_v.nestedWindow.w << "\n"
				"fNestedH=" << a_v.nestedWindow.h << "\n"
				"sNestedArt=" << a_v.nestedWindow.art << "\n"
				"fHotkeyX=" << a_v.hotkeyWindow.x << "\n"
				"fHotkeyY=" << a_v.hotkeyWindow.y << "\n"
				"fHotkeyW=" << a_v.hotkeyWindow.w << "\n"
				"fHotkeyH=" << a_v.hotkeyWindow.h << "\n"
				"\n"
				"[Watchdog]\n"
				"; If the menu renderer stops producing frames for uSeconds the game is treated as\n"
				"; hung and closes itself - no Task Manager needed. 0 or bEnabled=0 disables it.\n"
				"bEnabled=" << (a_v.watchdogEnabled ? 1 : 0) << "\n"
				"uSeconds=" << a_v.watchdogSeconds << "\n"
				"\n"
				"[FastExit]\n"
				"; When the game exits, end the process at once instead of running every DLL's and\n"
				"; driver's shutdown code - the phase where a closing game can get stuck beyond any kill.\n"
				"; Saves and settings are written when you save or change them, not at exit. 0 disables.\n"
				"bEnabled=" << (a_v.fastExit ? 1 : 0) << "\n"
				"\n"
				"[Screenshot]\n"
				"; Where the Screenshot control (Controls; F11 / View while this menu is open) saves a PNG of\n"
				"; what the screen shows, this menu included (Steam's own F12 screenshot shows the menu too).\n"
				"; Empty = Documents\\The Witcher 3\\AMF Screenshots.\n"
				"sFolder=" << a_v.screenshotFolder << "\n"
				"\n"
				"[Theme]\n"
				"; Registry id of the active theme: oathvein (the default - grey lines, charcoal and blood red),\n"
				"; untarnished (plain), veldun (bone lines on warm brown), norden (slate lines and silver on\n"
				"; grey) or norden-black (the same on black). oblivion (an embroidered map's edge in gold and\n"
				"; brown on parchment) and skyrim (the Nordic knotwork look) are the framework's other builds'\n"
				"; looks. Drop a theme INI into AMF\\themes to add your own (the shipped ones show the keys);\n"
				"; a theme's own art draws whenever that theme is picked.\n"
				"sThemeId=" << a_v.themeId << "\n"
				"\n"
				"[Skin]\n"
				"; Replacement ARTWORK for the menu shell, for a UI author matching their own\n"
				"; interface. Every image is a 32-bit RGBA PNG - DDS is not decoded. All optional.\n"
				";\n"
				"; bEnabled is the master switch and is OFF by default: with it off nothing below is\n"
				"; even loaded and the menu keeps its built-in look, whatever the paths say. Turn it\n"
				"; on only when you actually have artwork to point it at. There is a matching switch\n"
				"; on the Framework Settings page, so it can be turned off without editing this file.\n"
				";   sFrame       one square PNG with a TRANSPARENT CENTRE, drawn as a nine-slice\n"
				";                around the window. 192x192 with 64px corners is a good default.\n"
				";   uFrameCorner how many pixels of that PNG are the corner ornament.\n"
				";   sBackground  a small tileable PNG (<=512px each side) OR a full-screen one -\n"
				";                which it is is decided by its own size, so both just work.\n"
				";   sPlates      a FOLDER holding any of toggle.png, slider.png, tab.png, to restyle\n"
				";                individual controls. Supply only the ones you want changed.\n"
				"; Paths are relative to bin\\x64_dx12 (AMF/themes/YourTheme/frame.png), or absolute while working.\n"
				"bEnabled=" << (a_v.skinEnabled ? 1 : 0) << "\n"
				"sFrame=" << a_v.skinFrame << "\n"
				"uFrameCorner=" << a_v.skinFrameCorner << "\n"
				"sBackground=" << a_v.skinBackground << "\n"
				"sPlates=" << a_v.skinPlates << "\n"
				"\n"
				"[Log]\n"
				"; 0 = trace (everything, for a bug report), 1 = debug, 2 = info (the default), 3 = warning, 4 = error, 5 = critical, 6 = off.\n"
				"uLogLevel=" << a_v.logLevel << "\n";
			return file.str();
		}

		// ---- User.ini holds only what the player changed (Skyrim 2.0.5) ---------------------------------------------
		// HadToRegister, Nexus, 2026-10-03, on the Skyrim build: once every setting was copied into User.ini, which wins
		// every key it holds, the shipped file - which still reads like the settings file - was silently ignored and a
		// hand edit there did nothing. So a scalar key is written only when its value differs from what the shipped file
		// (or, for a key it does not carry, the compiled default) would give, and on load a User.ini value equal to the
		// shipped one counts as not set. The list-shaped sections - renames, the mod order, favourites, separators -
		// keep their wholesale semantics and are written whole.

		std::atomic<int> g_toggleSource{ static_cast<int>(ToggleKeySource::kDefault) };

		using Entries = std::unordered_map<std::string, std::string>;

		bool IsListKey(const std::string& a_key)
		{
			return a_key.rfind("MenuAlias.", 0) == 0 || a_key.rfind("MenuOrder.", 0) == 0 ||
				   a_key.rfind("MenuFavourites.", 0) == 0 || a_key.rfind("MenuSeparators.", 0) == 0;
		}

		Entries ParseText(const std::string& a_text)
		{
			std::istringstream in(a_text);
			return ParseFile(in);
		}

		// The compiled defaults as INI entries: rendered from a default Values and the default bindings, so the two
		// can never disagree.
		Entries CompiledDefaults()
		{
			return ParseText(ScalarBlock(Values{}) + bindings::DefaultIniBlock());
		}

		// The value each scalar key has when the player has not set it: the shipped file's, else the compiled default.
		Entries Baseline(bool* a_haveShipped = nullptr, Entries* a_shipped = nullptr)
		{
			Entries base = CompiledDefaults();
			Entries shipped;
			bool found = false;
			if (std::ifstream file(IniPath()); file.is_open())
			{
				shipped = ParseFile(file);
				found = true;
			}
			for (const auto& [key, value] : shipped)
			{
				if (!IsListKey(key)) { base[key] = value; }
			}
			if (a_haveShipped) { *a_haveShipped = found; }
			if (a_shipped) { *a_shipped = std::move(shipped); }
			return base;
		}

		// "Input.uToggleKey" -> "[Input] uToggleKey", the way a player sees it in the file.
		std::string KeyLabel(const std::string& a_key)
		{
			const auto dot = a_key.find('.');
			return dot == std::string::npos ? a_key : "[" + a_key.substr(0, dot) + "] " + a_key.substr(dot + 1);
		}

		// A value as the log shows it; the menu key also gets its key's name.
		std::string Describe(const std::string& a_key, const std::string& a_value)
		{
			long long code = 0;
			if (a_key == "Input.uToggleKey" && ParseInt(a_value, code))
			{
				if (code == 0) { return a_value + " (no key)"; }
				if (code > 0 && code <= 0xFF) { return a_value + " (" + bindings::KeyName(static_cast<std::uint32_t>(code)) + ")"; }
			}
			return a_value.empty() ? std::string("(empty)") : a_value;
		}

		// The scalar text Save() renders, with every key at its baseline value left out - its comment lines go with
		// it, and a section left with no key is left out too.
		std::string KeepChanged(const std::string& a_text, const Entries& a_baseline, int& a_kept, int& a_dropped)
		{
			std::string out;
			std::string section;
			std::string sectionLine;
			bool sectionWritten = false;
			std::string pending;   // comment and blank lines waiting for the key they describe
			std::istringstream in(a_text);
			std::string line;
			while (std::getline(in, line))
			{
				const std::string_view trimmed = Trim(line);
				if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']')
				{
					section = std::string(Trim(trimmed.substr(1, trimmed.size() - 2)));
					sectionLine = std::string(trimmed);
					sectionWritten = false;
					pending.clear();
					continue;
				}
				const auto equals = trimmed.find('=');
				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || equals == std::string_view::npos)
				{
					if (!trimmed.empty() || !pending.empty()) { pending += line + "\n"; }
					continue;
				}
				const std::string key = section + "." + std::string(Trim(trimmed.substr(0, equals)));
				const std::string value(Trim(trimmed.substr(equals + 1)));
				const auto base = a_baseline.find(key);
				if (base != a_baseline.end() && SameValue(value, base->second))
				{
					++a_dropped;
					pending.clear();
					continue;
				}
				if (!sectionWritten)
				{
					out += (out.empty() ? "" : "\n") + sectionLine + "\n";
					sectionWritten = true;
				}
				out += pending + line + "\n";
				pending.clear();
				++a_kept;
			}
			return out;
		}

		// A list-shaped key at a value that is not its default (empty name, empty list, the alphabetical order).
		bool ListKeyIsSet(const std::string& a_key, const std::string& a_value)
		{
			if (a_value.empty()) { return false; }
			if (a_key == "MenuOrder.bCustomOrder" || a_key == "MenuOrder.iSort") { return a_value != "0"; }
			return true;
		}

		// ---- MOVING AN OLDER BUILD'S SETTINGS OUT OF THE SHIPPED FILE (first run of the User.ini model) -------------
		// Up to 1.0.2 the Witcher 3 build saved every change straight into the shipped ApocryphaMenuFramework.ini: under
		// Mod Organizer 2 into AMF's own mod folder, which the next update replaces. Skyrim 2.0.3 met the same move with
		// an installer option that kept the old file and let the first save copy it into User.ini; that cost players
		// their settings whenever the option was missed, and once User.ini held only CHANGED values (2.0.5) the old file
		// read as "the defaults", so nothing in it counted as the player's any more.
		// Here the move is done by the plugin, once, at load: when there is no User.ini yet, every value in the shipped
		// file that differs from the compiled defaults - which equal the shipped INI (rule 16), so a difference can only
		// be the player's - is the player's. Those values are loaded as usual, the old file is copied aside
		// (ApocryphaMenuFramework.ini.migrated), the shipped file is written back as the defaults, and Save() writes
		// User.ini. The old file's values win once and are then retired, so they can never override a later change
		// (logic library: migrating a renamed mod's INI). Keys this build no longer has ([Startup]) are not carried.
		// Returns the scalar keys that were the player's; a_any is true when anything at all (lists included) was.
		std::unordered_set<std::string> FindPlayerValues(const Entries& a_shipped, bool& a_any)
		{
			std::unordered_set<std::string> scalars;
			a_any = false;
			const Entries defaults = CompiledDefaults();
			int lists = 0, retired = 0;
			for (const auto& [key, value] : a_shipped)
			{
				if (IsListKey(key))
				{
					if (ListKeyIsSet(key, value))
					{
						++lists;
						a_any = true;
						logger::info("settings: moving to User.ini - {} = {} (the mod list, set in the menu)", KeyLabel(key), Describe(key, value));
					}
					continue;
				}
				const auto def = defaults.find(key);
				if (def == defaults.end())
				{
					++retired;
					logger::debug("settings: {} = {} in the old file is not a setting this build has; not carried", KeyLabel(key), value);
					continue;
				}
				if (SameValue(value, def->second)) { continue; }
				scalars.insert(key);
				a_any = true;
				logger::info("settings: moving to User.ini - {} = {} (the default is {})", KeyLabel(key), Describe(key, value),
							 Describe(key, def->second));
			}
			logger::info("settings: the shipped file holds {} setting(s) and {} mod-list value(s) of your own from an earlier build; "
						 "{} retired key(s) left behind", scalars.size(), lists, retired);
			return scalars;
		}

		// The shipped file put back as the download ships it, after its old values were moved (FindPlayerValues).
		// The original is copied aside first; nothing is deleted.
		bool RestoreShipped()
		{
			std::error_code ec;
			std::filesystem::copy_file(IniPath(), MigratedPath(), std::filesystem::copy_options::overwrite_existing, ec);
			if (ec)
			{
				logger::warn("settings: could not copy the old settings file aside to {} ({}); it is rewritten anyway - its values "
							 "are in User.ini", PathText(MigratedPath()), ec.message());
			}
			std::ofstream file(IniPath(), std::ios::trunc);
			if (!file.is_open())
			{
				logger::error("settings: could not write the defaults back to {} - the values moved to User.ini are kept, but a "
							  "value equal to the old file's may be left out of User.ini until the next update", PathText(IniPath()));
				return false;
			}
			file << kShippedHeader << ScalarBlock(Values{}) << bindings::DefaultIniBlock();
			logger::info("settings: {} is the shipped defaults again; the earlier file is kept as {}", PathText(IniPath()),
						 PathText(MigratedPath()));
			return true;
		}

		// Puts the menu key into effect: kToggleMenu's keyboard key and toggleKey become the same value. A code no
		// key can have, or one another function already holds, falls back to F1 with a warning; 0 is "no key".
		void ApplyToggleKey(std::int32_t a_key, ToggleKeySource a_source)
		{
			std::int32_t key = a_key;
			if (key < 0 || key > 0xFF || key == 0x01)
			{
				logger::warn("settings: uToggleKey {} (0x{:X}) is not a key the menu can open on - a DirectInput keyboard scan "
							 "code is 1-255 and Escape is the menu's own close key; the menu key is F1 (59) instead",
							 a_key, static_cast<std::uint32_t>(a_key));
				key = 0x3B;
				a_source = ToggleKeySource::kFallback;
			}
			std::string holder;
			if (key != bindings::ToggleKeyboardCode() && !bindings::SetToggleKeyboard(key, &holder))
			{
				logger::warn("settings: uToggleKey {} ({}) is already the key for \"{}\" - one key cannot do both; the menu key "
							 "is F1 (59) instead", key, bindings::KeyName(static_cast<std::uint32_t>(key)), holder);
				key = 0x3B;
				a_source = ToggleKeySource::kFallback;
				if (!bindings::SetToggleKeyboard(key, &holder))
				{
					logger::warn("settings: F1 is also taken (by \"{}\") - the menu has no key; Controls can bind one, after "
								 "opening the menu through a menu launcher", holder);
					key = 0;
					bindings::SetToggleKeyboard(0, nullptr);
				}
			}
			g_values.toggleKey = key;
			g_toggleSource.store(static_cast<int>(a_source), std::memory_order_release);
		}
	}

	ToggleKeySource GetToggleKeySource()
	{
		return static_cast<ToggleKeySource>(g_toggleSource.load(std::memory_order_acquire));
	}

	const char* ToggleKeySourceName(ToggleKeySource a_source)
	{
		switch (a_source)
		{
		case ToggleKeySource::kShipped:      return "shipped";
		case ToggleKeySource::kUser:         return "user";
		case ToggleKeySource::kUserControls: return "user-controls";
		case ToggleKeySource::kFallback:     return "fallback";
		default:                             return "default";
		}
	}

	bool SetToggleKey(std::int32_t a_scancode)
	{
		std::string holder;
		if (a_scancode < 0 || a_scancode > 0xFF || a_scancode == 0x01)
		{
			logger::info("settings: the menu key was not moved to {} - not a key the menu can open on", a_scancode);
			return false;
		}
		if (!bindings::SetToggleKeyboard(a_scancode, &holder))
		{
			logger::info("settings: the menu key was not moved to {} - it is already the key for \"{}\"",
						 bindings::KeyName(static_cast<std::uint32_t>(a_scancode)), holder);
			return false;
		}
		g_values.toggleKey = a_scancode;
		Save();
		logger::info("settings: the menu key is now {} ({})", a_scancode,
					 a_scancode > 0 ? bindings::KeyName(static_cast<std::uint32_t>(a_scancode)) : std::string("no key"));
		return true;
	}

	void Load()
	{
		// The shipped INI gives the defaults; User.ini, when it exists, overrides the keys it holds a DIFFERENT value
		// for (one equal to the shipped value does not pin it). A key a later version adds is missing from an older
		// User.ini and so comes from the shipped file. Renames are a set rather than single keys: once User.ini exists
		// its [MenuAlias] is the whole set, so one the player cleared does not come back from the shipped file.
		bool haveShipped = false;
		Entries shipped;
		const Entries baseline = Baseline(&haveShipped, &shipped);
		Entries entries = shipped;
		Entries userEntries;
		std::unordered_set<std::string> userKept;   // scalar keys User.ini really sets (differs from the baseline)
		bool haveUser = false;
		bool migrating = false;
		std::error_code existsEc;
		const bool userExists = std::filesystem::exists(UserPath(), existsEc);
		if (userExists)
		{
			if (std::ifstream user(UserPath()); user.is_open())
			{
				userEntries = ParseFile(user);
				std::erase_if(entries, [](const auto& a_entry) { return a_entry.first.rfind("MenuAlias.", 0) == 0; });
				int pinned = 0;
				for (const auto& [key, value] : userEntries)
				{
					if (IsListKey(key)) { entries[key] = value; continue; }
					const auto base = baseline.find(key);
					if (base != baseline.end() && SameValue(value, base->second))
					{
						++pinned;
						logger::debug("settings: {} in User.ini equals the {} value ({}) - not counted as yours", KeyLabel(key),
									  shipped.contains(key) ? "shipped" : "default", value);
						continue;
					}
					entries[key] = value;
					userKept.insert(key);
					if (base != baseline.end())
					{
						// Said once per load: from the player's side a setting that "will not take" in the shipped file
						// is exactly this, and the line names which file won.
						logger::info("settings: {} - your User.ini says {} over the {} {}; User.ini wins", KeyLabel(key),
									 Describe(key, value), shipped.contains(key) ? "shipped" : "default", Describe(key, base->second));
					}
				}
				haveUser = true;
				logger::info("settings: User.ini sets {} value(s) of its own; {} more equal the shipped values and no longer "
							 "pin them (they are left out the next time the settings are saved)", userKept.size(), pinned);
			}
			else
			{
				logger::error("settings: {} exists but could not be read; the shipped defaults are used this session",
							  PathText(UserPath()));
			}
		}
		else if (haveShipped)
		{
			// No User.ini yet: an earlier build may have saved the player's settings into the shipped file itself.
			bool any = false;
			userKept = FindPlayerValues(shipped, any);
			migrating = any;
			if (!any) { logger::debug("settings: no User.ini yet, and the shipped file holds only defaults - nothing to move"); }
		}
		logger::info("settings: defaults from {} ({}); your settings from {} ({})", PathText(IniPath()), haveShipped ? "read" : "not found",
			PathText(UserPath()), haveUser ? "read" : (migrating ? "being created from the earlier build's values in the shipped file"
																	: "not saved yet - written the first time a setting changes"));

		if (!haveShipped && !haveUser)
		{
			logger::info("settings: no settings file; compiled defaults in effect (they match the shipped INI, rule 16)");
		}
		{
			ReadNumber(entries, "Input.uToggleKey", g_values.toggleKey);
			ReadBool(entries, "Input.bOnScreenKeyboard", g_values.onScreenKeyboard);
			ReadBool(entries, "Menu.bPauseGame", g_values.pauseGameWhileOpen);
			ReadBool(entries, "Menu.bSkipIntro", g_values.skipIntro);
			ReadBool(entries, "Menu.bSkipLoadingRecap", g_values.skipLoadingRecap);
			ReadBool(entries, "Menu.bKeepCameraAwake", g_values.keepCameraAwake);
			ReadBool(entries, "Menus.bSystemMenuRow", g_values.systemMenuRow);

			// Window profiles. Each field defaults to -1, which the renderer reads as "this profile
			// has never been moved, so use its default geometry"; a missing key therefore behaves
			// exactly like a fresh install rather than pinning the window at 0,0.
			ReadBool(entries, "Window.bMovable", g_values.movableWindow);
			ReadBool(entries, "Window.bFreeResize", g_values.freeResize);
			ReadNumber(entries, "Window.fNestedX", g_values.nestedWindow.x);
			ReadNumber(entries, "Window.fNestedY", g_values.nestedWindow.y);
			ReadNumber(entries, "Window.fNestedW", g_values.nestedWindow.w);
			ReadNumber(entries, "Window.fNestedH", g_values.nestedWindow.h);
			if (const auto it = entries.find("Window.sNestedArt"); it != entries.end()) { g_values.nestedWindow.art = it->second; }
			ReadNumber(entries, "Window.fHotkeyX", g_values.hotkeyWindow.x);
			ReadNumber(entries, "Window.fHotkeyY", g_values.hotkeyWindow.y);
			ReadNumber(entries, "Window.fHotkeyW", g_values.hotkeyWindow.w);
			ReadNumber(entries, "Window.fHotkeyH", g_values.hotkeyWindow.h);
			// A geometry that is not whole (an inf saved while the display was 0x0, a -1 on one field) is "never set":
			// cleared, so it is not carried into User.ini as a value of the player's.
			for (WindowGeometry* g : { &g_values.nestedWindow, &g_values.hotkeyWindow })
			{
				if (!g->IsSet() && (g->x != -1.0f || g->y != -1.0f || g->w != -1.0f || g->h != -1.0f))
				{
					logger::warn("settings: a saved window geometry ({}, {}, {}, {}) is not usable; the default placement is used",
								 g->x, g->y, g->w, g->h);
					g->Clear();
				}
			}
			ReadNumber(entries, "Display.fTextScale", g_values.textScale);
			ReadBool(entries, "Display.bSeeThrough", g_values.seeThrough);
			ReadNumber(entries, "Display.uWindowOpacity", g_values.windowOpacity);
			{
				auto it = entries.find("Display.sFontPath");
				if (it != entries.end()) { g_values.fontPath = it->second; }
				if (const auto lt = entries.find("Display.sLanguage"); lt != entries.end()) { g_values.language = lt->second; }
			}
			ReadBool(entries, "Watchdog.bEnabled", g_values.watchdogEnabled);
			ReadBool(entries, "FastExit.bEnabled", g_values.fastExit);
			if (const auto it = entries.find("Screenshot.sFolder"); it != entries.end()) { g_values.screenshotFolder = it->second; }
			ReadNumber(entries, "Watchdog.uSeconds", g_values.watchdogSeconds);
			ReadNumber(entries, "Display.uWindowPreset", g_values.windowPreset);
			if (const auto it = entries.find("Theme.sThemeId"); it != entries.end() && !it->second.empty())
			{
				// Retired ids from the 2026-09-01 theme merge are mapped, not dropped.
				g_values.themeId = theme::MigrateThemeId(it->second);
			}
			ReadBool(entries, "Skin.bEnabled", g_values.skinEnabled);
			if (const auto it = entries.find("Skin.sFrame"); it != entries.end()) { g_values.skinFrame = it->second; }
			if (const auto it = entries.find("Skin.sBackground"); it != entries.end()) { g_values.skinBackground = it->second; }
			if (const auto it = entries.find("Skin.sPlates"); it != entries.end()) { g_values.skinPlates = it->second; }
			ReadNumber(entries, "Skin.uFrameCorner", g_values.skinFrameCorner);
			ReadNumber(entries, "Log.uLogLevel", g_values.logLevel);
			// Menu-shell personalization (aliases, order, favourites, separators).
			personalization::LoadFrom(entries);
			// Always, even with no file at all: LoadFrom is also what puts the default bindings in place (Skyrim 2.0.5 -
			// with no file it used to be skipped and every control, the menu key included, was unbound).
			bindings::LoadFrom(entries);

			// THE MENU KEY (Skyrim 2.0.5 / 2.1.1). The input hook opens the menu on Controls' "Open and close the menu"
			// key; until now [Input] uToggleKey was a second copy that only the settings page's label and the
			// reserved-key export read, so editing it - or the Settings page's own Rebind - never changed the key that
			// opens the menu. One value now: uToggleKey decides, unless only the Controls page's binding was changed by
			// the player.
			{
				ToggleKeySource source = userKept.contains("Input.uToggleKey") ? ToggleKeySource::kUser
									   : shipped.contains("Input.uToggleKey")  ? ToggleKeySource::kShipped
																			   : ToggleKeySource::kDefault;
				const std::int32_t bound = bindings::ToggleKeyboardCode();
				const bool controlsChanged = userKept.contains("Bindings.sToggleMenu");
				// Moving an earlier build's file: the Controls page's binding was the key that really opened the menu
				// there (its Settings page Rebind moved only the uToggleKey label), so it wins when the two differ.
				if (controlsChanged && (source != ToggleKeySource::kUser || migrating))
				{
					logger::info("settings: the menu key is {} ({}) from {} [Bindings] sToggleMenu, set on the Controls page; it "
								 "wins over the {} uToggleKey {}", bound,
								 bound > 0 ? bindings::KeyName(static_cast<std::uint32_t>(bound)) : std::string("no key"),
								 migrating ? "the earlier build's" : "your User.ini's",
								 ToggleKeySourceName(source), g_values.toggleKey);
					g_values.toggleKey = bound;
					source = ToggleKeySource::kUserControls;
				}
				else if (controlsChanged && bound != g_values.toggleKey)
				{
					logger::warn("settings: your User.ini gives the menu two keys - uToggleKey {} and [Bindings] sToggleMenu {}; "
								 "uToggleKey wins", g_values.toggleKey, bound);
				}
				ApplyToggleKey(g_values.toggleKey, source);
				logger::info("settings: menu key {} ({}) - from {}", g_values.toggleKey,
							 g_values.toggleKey > 0 ? bindings::KeyName(static_cast<std::uint32_t>(g_values.toggleKey)) : std::string("no key"),
							 ToggleKeySourceName(GetToggleKeySource()));
			}

			logger::info("settings loaded: uToggleKey=0x{:X}, bSystemMenuRow={}, fTextScale={:.2f}, bSeeThrough={}, uWindowOpacity={}, "
						 "bMovable={}, bFreeResize={}, uLogLevel={}",
						 g_values.toggleKey, g_values.systemMenuRow, g_values.textScale, g_values.seeThrough, g_values.windowOpacity,
						 g_values.movableWindow, g_values.freeResize, g_values.logLevel);
		}

		if (g_values.textScale < 1.0f || g_values.textScale > 2.5f)
		{
			logger::warn("settings: fTextScale {:.2f} outside [1.0, 2.5]; clamped", g_values.textScale);
			g_values.textScale = g_values.textScale < 1.0f ? 1.0f : 2.5f;
		}

		if (g_values.windowOpacity < 5 || g_values.windowOpacity > 100)
		{
			logger::warn("settings: uWindowOpacity {} outside [5, 100]; clamped", g_values.windowOpacity);
			g_values.windowOpacity = g_values.windowOpacity < 5 ? 5 : 100;
		}

		const auto level = static_cast<spdlog::level::level_enum>(
			g_values.logLevel < 0 ? 0 : (g_values.logLevel > 6 ? 6 : g_values.logLevel));
		logger::set_level(level, level);
		logger::info("settings: log level applied ({})", g_values.logLevel);

		// The move, finished once everything is loaded: the shipped file back to the defaults, then User.ini written
		// from the values now in effect - which are the earlier build's. Save() compares against the restored file, so
		// every one of them is kept.
		if (migrating)
		{
			RestoreShipped();
			Save();
			logger::info("settings: your settings from the earlier build are now in {}; from here on the shipped file is only "
						 "the defaults", PathText(UserPath()));
		}
	}

	void Save()
	{
		// The menu key is the Controls page's binding; uToggleKey follows it, so a rebind there and DevBench's rebind op
		// (settings::SetToggleKey) both end up as the one value written below (Skyrim 2.0.5).
		g_values.toggleKey = bindings::ToggleKeyboardCode();

		std::error_code ec;
		std::filesystem::create_directories(UserPath().parent_path(), ec);
		std::ofstream file(UserPath(), std::ios::trunc);

		if (!file.is_open())
		{
			logger::error("settings: could not open {} for writing; the change will not survive this session", PathText(UserPath()));
			return;
		}

		bool haveShipped = false;
		Entries shipped;
		const Entries baseline = Baseline(&haveShipped, &shipped);
		int kept = 0;
		int dropped = 0;
		const std::string changed = KeepChanged(ScalarBlock(g_values) + bindings::IniBlock(), baseline, kept, dropped);

		file << "; ApocryphaRealm Menu Framework - YOUR settings: only what you changed in the menu. Every setting not\n"
				"; listed here comes from ApocryphaMenuFramework.ini beside this file, the shipped defaults; one that is\n"
				"; listed wins over that file. Edits made here while the game is closed are honoured on the next load.\n"
				"; Delete this file to go back to the defaults. The download never contains it, so an update cannot\n"
				"; replace it.\n"
				"\n"
			 << changed;

		// Menu-shell personalization writes its own sections (renames, order, favourites, separators), whole.
		file << personalization::IniBlock();

		// Where the menu key now comes from, for the DevBench report: the player's file when it differs.
		const auto base = baseline.find("Input.uToggleKey");
		const bool atBaseline = base != baseline.end() && SameValue(std::to_string(g_values.toggleKey), base->second);
		g_toggleSource.store(static_cast<int>(atBaseline ? (shipped.contains("Input.uToggleKey") ? ToggleKeySource::kShipped
																								 : ToggleKeySource::kDefault)
														 : ToggleKeySource::kUser),
							 std::memory_order_release);

		logger::debug("settings: saved to {} - {} value(s) of your own, {} at the shipped value left out", PathText(UserPath()), kept, dropped);
	}

	// ---- Menu-list layout presets (Skyrim 2.0.3) --------------------------------------------------------------------
	namespace
	{
		std::string PresetFileName(const std::string& a_name)
		{
			// Letters, digits, spaces and - _ ' ( ) only; a name is a file name and must stay one.
			std::string clean;
			for (const char c : a_name)
			{
				const auto u = static_cast<unsigned char>(c);
				if ((u < 0x80 && std::isalnum(u)) || c == ' ' || c == '-' || c == '_' || c == '\'' || c == '(' || c == ')') { clean += c; }
			}
			while (!clean.empty() && clean.back() == ' ') { clean.pop_back(); }
			while (!clean.empty() && clean.front() == ' ') { clean.erase(clean.begin()); }
			if (clean.size() > 48) { clean.resize(48); }
			return clean;
		}

		std::filesystem::path PresetPath(const std::string& a_cleanName) { return PresetDir() / (a_cleanName + ".ini"); }
	}

	std::vector<std::string> ListLayoutPresets()
	{
		std::vector<std::string> names;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(PresetDir(), ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && it->path().extension() == ".ini") { names.push_back(it->path().stem().string()); }
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	bool SaveLayoutPreset(const std::string& a_name)
	{
		const std::string name = PresetFileName(a_name);
		if (name.empty())
		{
			logger::info("presets: not saved - \"{}\" has no letters or digits to name a file with", a_name);
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(PresetDir(), ec);
		const std::filesystem::path path = PresetPath(name);
		std::ofstream file(path, std::ios::trunc);
		if (!file.is_open())
		{
			logger::error("presets: could not write {}", PathText(path));
			return false;
		}
		file << "; ApocryphaRealm Menu Framework - a saved menu-list layout (order, separators, favourites, renames).\n"
				"; Load it from Framework Settings > Menu list > Layout presets.\n";
		file << personalization::IniBlock();
		logger::info("presets: the menu list was saved as \"{}\" ({})", name, PathText(path));
		return true;
	}

	bool RenameLayoutPreset(const std::string& a_from, const std::string& a_to)
	{
		const std::string from = PresetFileName(a_from);
		const std::string to = PresetFileName(a_to);
		if (from.empty() || to.empty()) { return false; }
		std::error_code ec;
		const std::filesystem::path src = PresetPath(from);
		const std::filesystem::path dst = PresetPath(to);
		if (!std::filesystem::exists(src, ec)) { return false; }
		std::filesystem::remove(dst, ec);
		std::filesystem::rename(src, dst, ec);
		if (ec)
		{
			logger::error("presets: could not rename {} to {} ({})", PathText(src), PathText(dst), ec.message());
			return false;
		}
		logger::info("presets: \"{}\" is now \"{}\" ({})", from, to, PathText(dst));
		return true;
	}

	bool LoadLayoutPreset(const std::string& a_name)
	{
		const std::string name = PresetFileName(a_name);
		const std::filesystem::path path = PresetPath(name);
		std::ifstream file(path);
		if (name.empty() || !file.is_open())
		{
			logger::warn("presets: \"{}\" was not found at {}", a_name, PathText(path));
			return false;
		}
		personalization::LoadFrom(ParseFile(file));
		Save();
		logger::info("presets: \"{}\" loaded - the menu list now follows it", name);
		return true;
	}

	bool DeleteLayoutPreset(const std::string& a_name)
	{
		const std::string name = PresetFileName(a_name);
		std::error_code ec;
		const bool removed = !name.empty() && std::filesystem::remove(PresetPath(name), ec);
		if (removed) { logger::info("presets: \"{}\" deleted ({})", name, PathText(PresetPath(name))); }
		else { logger::warn("presets: \"{}\" could not be deleted ({})", a_name, ec ? ec.message() : "not found"); }
		return removed;
	}
}
