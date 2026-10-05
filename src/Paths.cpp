#include "Paths.h"

namespace paths
{
	const std::filesystem::path& Data()
	{
		static const std::filesystem::path s_data = [] {
			HMODULE self = nullptr;
			wchar_t file[MAX_PATH]{};
			if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&Data), &self) &&
				::GetModuleFileNameW(self, file, MAX_PATH)) {
				return std::filesystem::path(file).parent_path() / "AMF";
			}
			return std::filesystem::current_path() / "AMF";   // never expected; logged by the caller's first use
		}();
		return s_data;
	}

	std::string Str(const char* a_relative)
	{
		return (Data() / a_relative).string();
	}
}
