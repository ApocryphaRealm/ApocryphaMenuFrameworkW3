#include "Wine.h"

#include "Logger.h"

#include <Windows.h>

#include <atomic>
#include <format>

namespace wine
{
	namespace
	{
		struct Info
		{
			bool        detected = false;
			std::string version;
			std::string host;
		};

		using GetVersion_t = const char*(__cdecl*)();
		using GetHostVersion_t = void(__cdecl*)(const char** a_sysname, const char** a_release);

		std::string Text(const char* a_s) { return a_s ? std::string(a_s) : std::string(); }

		Info Query()
		{
			Info info;
			const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");   // always loaded in every Windows (and Wine) process
			if (!ntdll) {
				return info;   // cannot happen in practice; treated as Windows, the safe answer (uDrawPath auto -> 0, as before 1.0.7)
			}
			const auto getVersion = reinterpret_cast<GetVersion_t>(::GetProcAddress(ntdll, "wine_get_version"));
			if (!getVersion) {
				return info;
			}
			info.detected = true;
			info.version = Text(getVersion());
			if (const auto getHost = reinterpret_cast<GetHostVersion_t>(::GetProcAddress(ntdll, "wine_get_host_version"))) {
				const char* sysname = nullptr;
				const char* release = nullptr;
				getHost(&sysname, &release);
				info.host = Text(sysname);
				if (release && *release) {
					info.host += (info.host.empty() ? "" : " ") + Text(release);
				}
			}
			return info;
		}

		const Info& Get()
		{
			static const Info s_info = Query();
			return s_info;
		}

		std::string Esc(const std::string& a_s)
		{
			std::string out;
			for (const char c : a_s) {
				if (c == '"' || c == '\\') out += '\\';
				if (static_cast<unsigned char>(c) >= 0x20) out += c;
			}
			return out;
		}
	}

	bool Detected() { return Get().detected; }
	const std::string& Version() { return Get().version; }
	const std::string& Host() { return Get().host; }

	std::string Describe()
	{
		const Info& i = Get();
		if (!i.detected) {
			return "Windows";
		}
		std::string s = i.version.empty() ? std::string("Wine (version not reported)") : "Wine " + i.version;
		if (!i.host.empty()) {
			s += " on " + i.host;
		}
		return s;
	}

	void LogOnce()
	{
		static std::atomic_bool s_done{ false };
		if (s_done.exchange(true)) {
			return;
		}
		if (Detected()) {
			logger::info("platform: running under {} (ntdll.dll exports wine_get_version - Wine or Proton). [Display] uDrawPath auto "
						 "draws at dxgi's own Present here, and the hang watchdog stays off.", Describe());
		} else {
			logger::info("platform: Windows (ntdll.dll has no wine_get_version export - not Wine or Proton)");
		}
	}

	std::string StateJson()
	{
		const Info& i = Get();
		return std::format(R"({{"detected":{},"version":"{}","host":"{}"}})", i.detected ? "true" : "false", Esc(i.version), Esc(i.host));
	}
}
