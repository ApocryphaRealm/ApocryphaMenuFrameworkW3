#pragma once

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <ShlObj.h>
#include <Shlwapi.h>

// wingdi.h defines ERROR as 0 (kept undefined, as on the Oblivion build)
#ifdef ERROR
#	undef ERROR
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Logger.h"

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;
