#include "Paths.h"

namespace paths
{
	const std::filesystem::path& Data()
	{
		// bin\x64_dx12\AMF beside witcher3.exe - from the EXE's path, not the .asi's. Under Mod Organizer 2 the .asi reports its
		// REAL mod-folder path, which shows only AMF's own files (1.0.1 first run, 2026-10-05: the game's menus were looked for
		// inside AMF's mod folder). The exe's folder is the game's own, and listing it from inside the process goes through
		// the virtual folder, so themes, fonts or translations another mod adds to AMF\ are seen too. A manual install has the
		// .asi beside the exe, so both give the same folder there. The .asi's folder is the fallback.
		static const std::filesystem::path s_data = [] {
			wchar_t file[MAX_PATH]{};
			const DWORD n = ::GetModuleFileNameW(nullptr, file, MAX_PATH);
			if (n > 0 && n < MAX_PATH) {
				return std::filesystem::path(file).parent_path() / "AMF";
			}
			HMODULE self = nullptr;
			if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&Data), &self) &&
				::GetModuleFileNameW(self, file, MAX_PATH)) {
				return std::filesystem::path(file).parent_path() / "AMF";
			}
			return std::filesystem::current_path() / "AMF";
		}();
		return s_data;
	}

	std::string Str(const char* a_relative)
	{
		return (Data() / a_relative).string();
	}
}
