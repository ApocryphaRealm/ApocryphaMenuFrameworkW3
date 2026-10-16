#pragma once

// ============================================================================================================
// RUNNING UNDER WINE OR PROTON (1.0.7; a Linux player's notes on Nexus, Level12Lobster, 2026-10-16: with the Amethyst mod
// manager AMF runs under Wine/Proton once [Display] uDrawPath is set to 1 by hand).
//
// The standard test: Wine's ntdll.dll exports wine_get_version, which Windows' ntdll has never had. Proton is Wine, so the
// same export is there (Proton can be told to hide Wine's exports - then this says "not Wine" and the manual uDrawPath=1
// still works). Asked once, the first time anything wants it; the answer is cached for the session, and no thread can see a
// half-made answer (a function-local static).
//
// What depends on it: [Display] uDrawPath "auto" resolves to 1 under Wine and 0 on Windows (Settings.cpp, DrawPathCore.h),
// and the hang watchdog does not arm under Wine (Watchdog.cpp).
// ============================================================================================================

#include <string>

namespace wine
{
	// True when ntdll.dll exports wine_get_version.
	bool Detected();

	// Wine's version string ("9.0", "8.0-3"; Proton's carries its own suffix), or empty when not Wine or not reported.
	const std::string& Version();

	// Wine's host system from wine_get_host_version ("Linux 6.8.0-45-generic"), or empty when not exported.
	const std::string& Host();

	// One line for the log: "Wine 9.0 on Linux 6.8.0" / "Wine (version not reported)" / "Windows".
	std::string Describe();

	// The start-up log line (main.cpp, once): what was found and how.
	void LogOnce();

	// DevBench amf.menu op=state: {"detected":bool,"version":"...","host":"..."}.
	std::string StateJson();
}
