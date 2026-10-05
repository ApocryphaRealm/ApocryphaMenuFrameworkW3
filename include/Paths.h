#pragma once

// Where AMF's own files live: <the folder of ApocryphaMenuFramework.asi>\AMF (bin\x64_dx12\AMF under the game).
// Witcher 3 5.0 runs with its working folder at bin\, not bin\x64_dx12 (M1 log, 2026-10-05: the layout INI landed in
// bin\AMF and the themes folder was never found), so nothing may be resolved against the current directory. Under Mod
// Organizer 2 the .asi's folder is the virtual bin\x64_dx12, which maps the mod's AMF folder and sends new files to
// overwrite.

#include <filesystem>
#include <string>

namespace paths
{
	const std::filesystem::path& Data();          // <asi folder>\AMF
	std::string Str(const char* a_relative);      // (Data() / a_relative), as a UTF-8 string for streams and logs
}
