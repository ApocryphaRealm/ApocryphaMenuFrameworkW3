// Runs AMF's engine-bridge resolution (src/Red3.cpp) against witcher3.exe OUTSIDE the game: the exe is mapped as an image
// with LoadLibraryEx(DONT_RESOLVE_DLL_REFERENCES) - nothing in it runs, no import is loaded - and the same code that runs in
// game finds the config natives in it. Exit code 0 when every function was found and checked.
//
//   red3_test "<...\The Witcher 3\bin\x64_dx12\witcher3.exe>"

#include "PCH.h"

#include "Red3.h"

#include <spdlog/sinks/stdout_color_sinks.h>

int wmain(int argc, wchar_t** argv)
{
	spdlog::set_default_logger(spdlog::stdout_color_mt("red3_test"));
	if (argc < 2) {
		std::puts("usage: red3_test <witcher3.exe>");
		return 2;
	}
	HMODULE exe = ::LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
	if (!exe) {
		std::printf("could not map the exe (%lu)\n", ::GetLastError());
		return 2;
	}
	std::printf("mapped at %p\n", static_cast<void*>(exe));
	const bool ok = red3::ResolveIn(exe);
	std::printf("%s\n%s\n", ok ? "RESOLVED" : "NOT RESOLVED", red3::StatusJson().c_str());
	return ok ? 0 : 1;
}
