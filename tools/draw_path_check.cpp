// [Display] uDrawPath "auto" and Wine / Proton detection outside the game (1.0.7): `xmake build draw_path_check` then
// `xmake run draw_path_check`. Runs include/DrawPathCore.h (the rule Settings.cpp applies) and src/Wine.cpp (the detection the
// .asi runs) and exits 1 on any failure:
//   * the parser: auto in any case, empty, 0, 1, the old clamp above 1, below 0, junk;
//   * the decision table: auto -> 0 on Windows and 1 under Wine; a 1 honoured from either file; a 0 honoured from User.ini;
//     a 0 in the shipped file (the 1.0.4-1.0.6 default) read as auto;
//   * the update scenarios through a model of Settings.cpp's merge (shipped file, User.ini, the first-run move of an older
//     build's shipped values, what Save() pins) - a fresh install, an old shipped file kept, Level12Lobster's line-89 edit
//     under 1.0.6, a hand edit to the new file, a player forcing 0 under Wine;
//   * the shipped INI says uDrawPath=auto, the compiled default (rule 16);
//   * THIS machine: ntdll.dll has no wine_get_version, so the detection says Windows and auto resolves to 0 - the 1.0.6 path.

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include "DrawPathCore.h"
#include "Wine.h"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <string>

namespace
{
	namespace dp = drawpath::core;

	int g_failures = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		std::printf("%s: %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
		if (!a_ok) ++g_failures;
	}

	void CheckParse(const char* a_text, bool a_ok, std::int32_t a_want)
	{
		std::int32_t got = 99;
		const bool ok = dp::Parse(a_text, got);
		Check(ok == a_ok && got == a_want, std::string("parse \"") + a_text + "\" -> " + (ok ? "ok " : "rejected ") + std::to_string(got));
	}

	void CheckResolve(std::int32_t a_setting, dp::Source a_src, bool a_wine, std::int32_t a_effective, bool a_auto, bool a_legacy)
	{
		const dp::Decision d = dp::Resolve(a_setting, a_src, a_wine);
		Check(d.effective == a_effective && d.automatic == a_auto && d.legacyZero == a_legacy,
			"resolve setting " + dp::Text(a_setting) + " from " + dp::SourceName(a_src) + (a_wine ? " under Wine" : " on Windows") + " -> " +
				std::to_string(d.effective) + (d.automatic ? " (auto)" : "") + (d.legacyZero ? " (old shipped 0)" : ""));
	}

	// Settings.cpp's numeric / text equality for this key (SameValue): numbers by value, "auto" in any case.
	bool Same(const std::string& a_l, const std::string& a_r)
	{
		std::int32_t l = 0, r = 0;
		if (dp::IsAutoText(a_l) && dp::IsAutoText(a_r)) return true;
		if (dp::Parse(a_l, l) && dp::Parse(a_r, r) && !dp::IsAutoText(a_l) && !dp::IsAutoText(a_r)) return l == r;
		return dp::Trim(a_l) == dp::Trim(a_r);
	}

	// A model of Settings::Load for this one key: what counts as the player's, which file it comes from, the setting.
	// a_shipped / a_user: the key's text in each file (nullopt = not carried); a_userFile: User.ini exists at all.
	struct Outcome
	{
		dp::Decision decision;
		dp::Source   source = dp::Source::kDefault;
		bool         pinnedInUser = false;   // what Save() leaves in User.ini for this key afterwards
	};

	Outcome Load(std::optional<std::string> a_shipped, bool a_userFile, std::optional<std::string> a_user, bool a_wine)
	{
		const std::string baseline = a_shipped.value_or(dp::Text(dp::kAuto));   // the compiled default is auto
		bool        userSets = false;
		std::string text = baseline;
		if (a_userFile) {
			if (a_user && !Same(*a_user, baseline)) {   // a User.ini value equal to the shipped one does not count
				userSets = true;
				text = *a_user;
			}
		} else if (a_shipped && !Same(*a_shipped, dp::Text(dp::kAuto)) && !dp::IsLegacyShippedZero(*a_shipped)) {
			userSets = true;   // FindPlayerValues: an older build's player value in the shipped file moves to User.ini
		}
		std::int32_t setting = dp::kAuto;
		if (!dp::Parse(text, setting)) setting = dp::kAuto;
		Outcome o;
		o.source = dp::SourceOf(userSets, a_shipped.has_value());
		o.decision = dp::Resolve(setting, o.source, a_wine);
		o.pinnedInUser = o.source == dp::Source::kUser;   // Save(): the key is written only when it came from User.ini
		return o;
	}

	void Scenario(const char* a_name, std::optional<std::string> a_shipped, bool a_userFile, std::optional<std::string> a_user,
		bool a_wine, std::int32_t a_want, bool a_wantPinned)
	{
		const Outcome o = Load(a_shipped, a_userFile, a_user, a_wine);
		Check(o.decision.effective == a_want && o.pinnedInUser == a_wantPinned,
			std::string(a_name) + (a_wine ? " [Wine]" : " [Windows]") + " -> uDrawPath " + std::to_string(o.decision.effective) + " from " +
				dp::SourceName(o.source) + (o.pinnedInUser ? ", kept in User.ini" : ", not written to User.ini"));
	}
}

int main()
{
	spdlog::set_default_logger(spdlog::stdout_logger_mt("check"));

	// ---- the parser ------------------------------------------------------------------------------------------------
	CheckParse("auto", true, dp::kAuto);
	CheckParse("Auto", true, dp::kAuto);
	CheckParse(" AUTO\r", true, dp::kAuto);
	CheckParse("", true, dp::kAuto);
	CheckParse("0", true, 0);
	CheckParse("1", true, 1);
	CheckParse("+1", true, 1);
	CheckParse("2", true, 1);    // 1.0.4-1.0.6 clamped to 0..1
	CheckParse("-1", true, dp::kAuto);
	CheckParse("-0", true, 0);
	CheckParse("yes", false, dp::kAuto);
	CheckParse("1.5", false, dp::kAuto);
	CheckParse("autox", false, dp::kAuto);
	Check(dp::Text(dp::kAuto) == "auto" && dp::Text(0) == "0" && dp::Text(1) == "1", "Text() writes auto / 0 / 1");
	Check(dp::IsLegacyShippedZero("0") && !dp::IsLegacyShippedZero("1") && !dp::IsLegacyShippedZero("auto"), "the old shipped default is 0 only");

	// ---- the decision table ----------------------------------------------------------------------------------------
	for (const auto src : { dp::Source::kDefault, dp::Source::kShipped, dp::Source::kUser }) {
		CheckResolve(dp::kAuto, src, false, 0, true, false);
		CheckResolve(dp::kAuto, src, true, 1, true, false);
		CheckResolve(1, src, false, 1, false, false);
		CheckResolve(1, src, true, 1, false, false);
	}
	CheckResolve(0, dp::Source::kUser, false, 0, false, false);
	CheckResolve(0, dp::Source::kUser, true, 0, false, false);
	CheckResolve(0, dp::Source::kShipped, false, 0, true, true);
	CheckResolve(0, dp::Source::kShipped, true, 1, true, true);

	// ---- updates, through the merge model --------------------------------------------------------------------------
	for (const bool w : { false, true }) {
		Scenario("fresh 1.0.7 install", std::string("auto"), false, std::nullopt, w, w ? 1 : 0, false);
		Scenario("1.0.7 over a 1.0.6 that never saved a setting", std::string("auto"), true, std::nullopt, w, w ? 1 : 0, false);
		Scenario("old 1.0.6 shipped file kept (uDrawPath=0), no User.ini", std::string("0"), false, std::nullopt, w, w ? 1 : 0, false);
		Scenario("old 1.0.6 shipped file kept (uDrawPath=0), User.ini without the key", std::string("0"), true, std::nullopt, w, w ? 1 : 0, false);
		Scenario("Level12Lobster's line-89 edit under 1.0.6 (moved to User.ini), then 1.0.7", std::string("auto"), true, std::string("1"), w, 1, true);
		Scenario("Level12Lobster's line-89 edit, old shipped file kept, User.ini created by 1.0.7", std::string("1"), false, std::nullopt, w, 1, true);
		Scenario("the 1.0.7 shipped file edited to 1, User.ini exists", std::string("1"), true, std::nullopt, w, 1, false);
		Scenario("the 1.0.7 shipped file edited to 0 (reads as the old default)", std::string("0"), true, std::nullopt, w, w ? 1 : 0, false);
		Scenario("uDrawPath=0 in User.ini (forced)", std::string("auto"), true, std::string("0"), w, 0, true);
		Scenario("uDrawPath=Auto in User.ini (same as the shipped auto)", std::string("auto"), true, std::string("Auto"), w, w ? 1 : 0, false);
		Scenario("key missing from both files", std::nullopt, true, std::nullopt, w, w ? 1 : 0, false);
	}

	// ---- the shipped INI carries the compiled default (rule 16) ----------------------------------------------------
	{
		std::ifstream ini("dist\\bin\\x64_dx12\\AMF\\ApocryphaMenuFramework.ini");
		std::string   line, value;
		bool          inDisplay = false, found = false;
		while (ini && std::getline(ini, line)) {
			const std::string_view t = dp::Trim(line);
			if (!t.empty() && t.front() == '[') {
				inDisplay = t == "[Display]";
				continue;
			}
			if (inDisplay && t.rfind("uDrawPath=", 0) == 0) {
				value = std::string(t.substr(10));
				found = true;
			}
		}
		std::int32_t v = 0;
		Check(found && dp::Parse(value, v) && v == dp::kAuto && dp::Text(v) == value,
			"the shipped INI's [Display] uDrawPath is \"" + value + "\" (the compiled default is auto)");
	}

	// ---- this machine: Windows ------------------------------------------------------------------------------------
	{
		const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
		const bool    exported = ntdll && ::GetProcAddress(ntdll, "wine_get_version") != nullptr;
		Check(wine::Detected() == exported, std::string("wine::Detected() agrees with ntdll's exports (") + (exported ? "Wine" : "Windows") + ")");
		Check(!wine::Detected() && wine::Version().empty() && wine::Describe() == "Windows",
			"on this Windows machine: not Wine, no version, described as \"" + wine::Describe() + "\"");
		Check(dp::Resolve(dp::kAuto, dp::Source::kShipped, wine::Detected()).effective == 0,
			"on this Windows machine auto resolves to 0 - the game-facing path 1.0.6 drew on");
		Check(wine::StateJson() == R"({"detected":false,"version":"","host":""})", "StateJson " + wine::StateJson());
		wine::LogOnce();
	}

	std::printf("%s - %d failure(s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures);
	return g_failures ? 1 : 0;
}
