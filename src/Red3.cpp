#include "PCH.h"

#include "Red3.h"

#include <algorithm>
#include <functional>
#include <initializer_list>

namespace red3
{
	namespace
	{
		// The engine's string as the config code passes it: a heap buffer and its size in bytes INCLUDING the terminator
		// (SetVarValue's impl copies `size` bytes and compares them byte by byte up to the NUL).
		struct RedString
		{
			const char*   data = nullptr;
			std::uint32_t size = 0;
			std::uint32_t pad = 0;
		};

		using NamePoolGet_t = void* (*)();
		using NameAdd_t = std::uint32_t (*)(void* a_pool, const char* a_name);
		using FindVar_t = void* (*)(void* a_unused, const std::uint32_t* a_group, const std::uint32_t* a_var);
		using SetImpl_t = void (*)(void* a_unused, const std::uint32_t* a_group, const std::uint32_t* a_var, const RedString* a_value);
		using GetValue_t = RedString* (*)(void* a_var, RedString* a_out);   // the var's vtable slot 3 (+0x18)
		using Free_t = void (*)(void* a_ptr);
		using Native_t = void (*)(void* a_context, void* a_stackFrame, void* a_result);

		std::uintptr_t g_base = 0;
		NamePoolGet_t  g_poolGet = nullptr;
		NameAdd_t      g_nameAdd = nullptr;
		FindVar_t      g_findVar = nullptr;
		SetImpl_t      g_setImpl = nullptr;
		Free_t         g_free = nullptr;
		Native_t       g_saveNative = nullptr;

		// PAUSE (the owner's bPauseGame - Skyrim held UI::numPausesGame): CGame's own Pause/Unpause, virtual functions taking
		// a reason string - the script natives `theGame.Pause("reason")` call them as [vtable + 0x2B0] / [+0x2B8] on 5.0.
		void**         g_gameGlobal = nullptr;   // where the engine keeps its CGame* (theGame)
		std::ptrdiff_t g_pauseSlot = -1, g_unpauseSlot = -1;   // byte offsets into CGame's vtable
		std::string    g_pauseWhy;               // why pausing is off, when it is
		bool           g_gamePaused = false;     // game thread only: whether AMF holds a pause now

		std::mutex       g_resolveLock;
		bool             g_tried = false;
		std::atomic_bool g_resolved{ false };
		std::atomic_bool g_faulted{ false };
		std::string      g_why;   // why the bridge is off
		std::atomic<std::uint64_t> g_gets{ 0 }, g_sets{ 0 }, g_saves{ 0 };

		std::mutex                         g_jobsLock;
		std::vector<std::function<void()>> g_jobs;
		std::vector<std::function<void()>> g_frameHooks;   // run every Pump (AddFrameHook)
		std::atomic_bool                   g_savePending{ false };
		std::atomic<ULONGLONG>             g_saveAfter{ 0 };
		std::unordered_map<std::string, std::uint32_t> g_names;   // game thread only

		struct Range
		{
			const std::uint8_t* begin = nullptr;
			std::size_t         size = 0;
		};

		bool Section(const char* a_name, Range& a_out)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_base + dos->e_lfanew);
			const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
			for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
				if (std::strncmp(reinterpret_cast<const char*>(s[i].Name), a_name, IMAGE_SIZEOF_SHORT_NAME) == 0) {
					a_out = { reinterpret_cast<const std::uint8_t*>(g_base + s[i].VirtualAddress), s[i].Misc.VirtualSize };
					return true;
				}
			}
			return false;
		}

		std::uintptr_t Rel32(const std::uint8_t* a_instr, std::size_t a_dispAt, std::size_t a_length)
		{
			std::int32_t disp;
			std::memcpy(&disp, a_instr + a_dispAt, sizeof(disp));
			return reinterpret_cast<std::uintptr_t>(a_instr) + a_length + disp;
		}

		// First match of a byte pattern (-1 = any byte) in [a_from, a_from + a_len).
		const std::uint8_t* Find(const std::uint8_t* a_from, std::size_t a_len, std::initializer_list<int> a_pattern)
		{
			const std::size_t n = a_pattern.size();
			for (std::size_t i = 0; i + n <= a_len; ++i) {
				std::size_t k = 0;
				for (const int b : a_pattern) {
					if (b >= 0 && a_from[i + k] != static_cast<std::uint8_t>(b)) {
						break;
					}
					++k;
				}
				if (k == n) {
					return a_from + i;
				}
			}
			return nullptr;
		}

		// The address must begin a function in the exe's unwind table - a wrong pattern hit lands mid-function and fails here.
		bool IsFunctionStart(std::uintptr_t a_address)
		{
			DWORD64 imageBase = 0;
			const PRUNTIME_FUNCTION fe = ::RtlLookupFunctionEntry(a_address, &imageBase, nullptr);
			return fe && imageBase == g_base && imageBase + fe->BeginAddress == a_address;
		}

		struct Registration
		{
			std::uintptr_t native = 0, get = 0, add = 0;
			std::uintptr_t site = 0;   // the `lea rdx,[name]` in the class's registering function
		};

		// Script natives by name: "\0Name\0" in .rdata; in .text the `lea rdx,[rip+name]` that sits between
		// `call CNamePool::Get` and `mov rcx,rax; call CNamePool::Add`, with the native's own `lea rax,[rip+fn]` before it.
		// Every registration of each name - several classes can register a native of one name (Pause: CGame and an
		// animation class), so the caller picks by the class's registering function.
		std::vector<std::pair<std::string, Registration>> FindAllNatives(const Range& a_text, const Range& a_rdata,
			std::initializer_list<const char*> a_names)
		{
			std::unordered_map<std::uintptr_t, std::string> byAddress;
			for (const char* name : a_names) {
				std::vector<std::uint8_t> needle{ 0 };
				needle.insert(needle.end(), name, name + std::strlen(name));
				needle.push_back(0);
				// every copy of the string: classes registering a native of the same name can each hold their own (Pause)
				const auto* end = a_rdata.begin + a_rdata.size;
				const std::boyer_moore_horspool_searcher searcher(needle.begin(), needle.end());
				for (const auto* it = std::search(a_rdata.begin, end, searcher); it != end; it = std::search(it + 1, end, searcher)) {
					byAddress.emplace(reinterpret_cast<std::uintptr_t>(it + 1), name);
				}
			}
			std::vector<std::pair<std::string, Registration>> out;
			const std::uint8_t* p = a_text.begin;
			for (std::size_t i = 0x40; i + 16 < a_text.size; ++i) {
				if (p[i] != 0x48 || p[i + 1] != 0x8D || p[i + 2] != 0x15) {
					continue;
				}
				const auto hit = byAddress.find(Rel32(p + i, 3, 7));
				if (hit == byAddress.end()) {
					continue;
				}
				if (p[i - 5] != 0xE8 || p[i + 7] != 0x48 || p[i + 8] != 0x8B || p[i + 9] != 0xC8 || p[i + 10] != 0xE8) {
					continue;
				}
				Registration r;
				r.site = reinterpret_cast<std::uintptr_t>(p + i);
				r.get = Rel32(p + i - 5, 1, 5);
				r.add = Rel32(p + i + 10, 1, 5);
				for (std::size_t j = i - 0x40; j < i; ++j) {
					if (p[j] == 0x48 && p[j + 1] == 0x8D && p[j + 2] == 0x05) {
						r.native = Rel32(p + j, 3, 7);
					}
				}
				out.emplace_back(hit->second, r);
			}
			return out;
		}

		// The first registration of each name (SetVarValue and SaveUserSettings are registered once).
		std::unordered_map<std::string, Registration> FindNatives(const Range& a_text, const Range& a_rdata,
			std::initializer_list<const char*> a_names)
		{
			std::unordered_map<std::string, Registration> out;
			for (auto& [name, r] : FindAllNatives(a_text, a_rdata, a_names)) {
				out.emplace(name, r);
			}
			return out;
		}

		// The function a code address belongs to (its unwind entry's start), 0 when none.
		std::uintptr_t FunctionOf(std::uintptr_t a_address)
		{
			DWORD64 imageBase = 0;
			const PRUNTIME_FUNCTION fe = ::RtlLookupFunctionEntry(a_address, &imageBase, nullptr);
			return fe ? imageBase + fe->BeginAddress : 0;
		}

		std::string Rva(std::uintptr_t a_address)
		{
			return a_address ? std::format("\"0x{:X}\"", a_address - g_base) : std::string("null");
		}

		void TurnOff(const std::string& a_why)
		{
			g_why = a_why;
			g_resolved = false;
			logger::warn("engine bridge off: {} - AMF's mod pages stay read-only", a_why);
		}

		// ---- SEH-guarded calls: no C++ objects with destructors in these functions ----
		bool SafeName(const char* a_name, std::uint32_t* a_out)
		{
			__try {
				*a_out = g_nameAdd(g_poolGet(), a_name);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool SafeFind(const std::uint32_t* a_group, const std::uint32_t* a_var, void** a_out)
		{
			__try {
				*a_out = g_findVar(nullptr, a_group, a_var);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool SafeGetValue(void* a_var, RedString* a_out)
		{
			__try {
				const auto fn = reinterpret_cast<GetValue_t>((*reinterpret_cast<void***>(a_var))[3]);
				fn(a_var, a_out);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool SafeFree(void* a_ptr)
		{
			__try {
				g_free(a_ptr);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool SafeSet(const std::uint32_t* a_group, const std::uint32_t* a_var, const RedString* a_value)
		{
			__try {
				g_setImpl(nullptr, a_group, a_var, a_value);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// The SaveUserSettings native reads nothing from its script stack frame except skipping the end-of-parameters
		// opcode (`inc [frame+0x30]`), so a small frame whose +0x30 points at scratch bytes is a valid call.
		bool SafeSave()
		{
			alignas(16) std::uint8_t frame[0x80] = {};
			std::uint8_t             code[16] = {};
			std::uint8_t*            cursor = code;
			std::memcpy(frame + 0x30, &cursor, sizeof(cursor));
			__try {
				g_saveNative(nullptr, frame, nullptr);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool SafePauseCall(void* a_game, std::ptrdiff_t a_slot, const RedString* a_reason)
		{
			__try {
				using Fn = void (*)(void*, const RedString*);
				const auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void***>(a_game))[a_slot / 8]);
				fn(a_game, a_reason);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		void Fault(const char* a_what)
		{
			if (!g_faulted.exchange(true)) {
				logger::error("engine bridge: {} faulted - the bridge is off for this session, AMF's mod pages are read-only again", a_what);
			}
		}

		bool Name(const std::string& a_name, std::uint32_t& a_out)
		{
			if (const auto it = g_names.find(a_name); it != g_names.end()) {
				a_out = it->second;
				return true;
			}
			if (!SafeName(a_name.c_str(), &a_out)) {
				Fault("CNamePool::Add");
				return false;
			}
			g_names.emplace(a_name, a_out);
			return true;
		}

		void* FindVarPtr(const std::string& a_group, const std::string& a_var)
		{
			std::uint32_t group = 0, var = 0;
			if (!Name(a_group, group) || !Name(a_var, var)) {
				return nullptr;
			}
			void* found = nullptr;
			if (!SafeFind(&group, &var, &found)) {
				Fault("FindVar");
				return nullptr;
			}
			return found;
		}
	}

	bool Resolve() { return ResolveIn(::GetModuleHandleW(nullptr)); }

	bool ResolveIn(void* a_module)
	{
		std::scoped_lock l(g_resolveLock);
		if (g_tried) {
			return g_resolved;
		}
		g_tried = true;
		const auto start = std::chrono::steady_clock::now();
		g_base = reinterpret_cast<std::uintptr_t>(a_module);
		Range text, rdata;
		if (!g_base || !Section(".text", text) || !Section(".rdata", rdata)) {
			TurnOff("the exe's .text/.rdata sections were not found");
			return false;
		}

		const auto natives = FindNatives(text, rdata, { "SetVarValue", "SaveUserSettings" });
		const auto set = natives.find("SetVarValue");
		const auto save = natives.find("SaveUserSettings");
		if (set == natives.end() || save == natives.end() || !set->second.native || !save->second.native) {
			TurnOff(std::format("the registration of {} was not found",
				set == natives.end() || !set->second.native ? "SetVarValue" : "SaveUserSettings"));
			return false;
		}

		// inside the SetVarValue native: lea r9,[rsp+a]; lea r8,[rsp+b]; lea rdx,[rsp+c]; call impl - then the string's free
		const auto* native = reinterpret_cast<const std::uint8_t*>(set->second.native);
		const auto* call = Find(native, 0x200, { 0x4C, 0x8D, 0x4C, 0x24, -1, 0x4C, 0x8D, 0x44, 0x24, -1, 0x48, 0x8D, 0x54, 0x24, -1, 0xE8 });
		if (!call) {
			TurnOff("SetVarValue's call into the config code was not found");
			return false;
		}
		const std::uintptr_t impl = Rel32(call + 15, 1, 5);
		const auto*          freeCall = Find(call + 20, 0x40, { 0x48, 0x85, 0xC9, 0x74, -1, 0xE8 });
		const auto*          findCall = Find(reinterpret_cast<const std::uint8_t*>(impl), 0x20, { 0xE8 });
		if (!freeCall || !findCall) {
			TurnOff("the config code's var lookup or the engine's free was not found");
			return false;
		}
		const std::uintptr_t freeFn = Rel32(freeCall + 5, 1, 5);
		const std::uintptr_t findFn = Rel32(findCall, 1, 5);

		const std::pair<const char*, std::uintptr_t> all[] = {
			{ "SetVarValue native", set->second.native }, { "SaveUserSettings native", save->second.native },
			{ "SetVarValue impl", impl }, { "FindVar", findFn }, { "free", freeFn }, { "CNamePool::Get", set->second.get },
			{ "CNamePool::Add", set->second.add },
		};
		for (const auto& [what, address] : all) {
			if (!IsFunctionStart(address)) {
				TurnOff(std::format("{} resolved to {}, which does not start a function", what, Rva(address)));
				return false;
			}
		}
		if (save->second.get != set->second.get || save->second.add != set->second.add) {
			TurnOff("the two registrations name different CNamePool functions");
			return false;
		}

		// ---- pause: optional - a miss leaves the bridge up and only the pause setting hidden ----
		[&] {
			// theGame: SaveUserSettings starts `push rbx; sub rsp,20; inc [rdx+30]; mov rbx,[rip+theGame]`
			const auto* saveCode = reinterpret_cast<const std::uint8_t*>(save->second.native);
			const auto* movGame = Find(saveCode, 0x20, { 0x48, 0x8B, 0x1D });
			if (!movGame) { g_pauseWhy = "where the engine keeps theGame was not found"; return; }
			g_gameGlobal = reinterpret_cast<void**>(Rel32(movGame, 3, 7));
			// CGame's Pause/Unpause: the registrations made by the same function that registers ExitGame (only CGame has it)
			std::uintptr_t gameRegistrar = 0, pauseNative = 0, unpauseNative = 0;
			const auto all = FindAllNatives(text, rdata, { "ExitGame", "Pause", "Unpause" });
			for (const auto& [name, r] : all) {
				if (name == "ExitGame") { gameRegistrar = FunctionOf(r.site); }
			}
			for (const auto& [name, r] : all) {
				if (gameRegistrar == 0 || FunctionOf(r.site) != gameRegistrar) { continue; }
				if (name == "Pause") { pauseNative = r.native; }
				if (name == "Unpause") { unpauseNative = r.native; }
			}
			if (!pauseNative || !unpauseNative || !IsFunctionStart(pauseNative) || !IsFunctionStart(unpauseNative)) {
				g_pauseWhy = "CGame's Pause/Unpause natives were not found";
				return;
			}
			// each native ends in `call qword ptr [rax + slot]` on the game object (FF 90 disp32)
			const auto slotOf = [](std::uintptr_t a_native) -> std::ptrdiff_t {
				const auto* call = Find(reinterpret_cast<const std::uint8_t*>(a_native), 0x100, { 0xFF, 0x90 });
				if (!call) { return -1; }
				std::int32_t disp;
				std::memcpy(&disp, call + 2, sizeof(disp));
				return disp;
			};
			const std::ptrdiff_t ps = slotOf(pauseNative), us = slotOf(unpauseNative);
			if (ps <= 0 || us <= 0 || ps % 8 != 0 || us != ps + 8 || ps > 0x2000) {
				g_pauseWhy = std::format("CGame's Pause/Unpause slots look wrong ({:#x} / {:#x})", ps, us);
				return;
			}
			g_pauseSlot = ps;
			g_unpauseSlot = us;
			logger::info("engine bridge: pause found - theGame at {}, Pause {} (vtable +{:#x}), Unpause {} (+{:#x})",
				Rva(reinterpret_cast<std::uintptr_t>(g_gameGlobal)), Rva(pauseNative), ps, Rva(unpauseNative), us);
		}();
		if (g_pauseSlot < 0) { logger::warn("engine bridge: {} - pausing the game while the menu is open stays off", g_pauseWhy); }

		g_poolGet = reinterpret_cast<NamePoolGet_t>(set->second.get);
		g_nameAdd = reinterpret_cast<NameAdd_t>(set->second.add);
		g_findVar = reinterpret_cast<FindVar_t>(findFn);
		g_setImpl = reinterpret_cast<SetImpl_t>(impl);
		g_free = reinterpret_cast<Free_t>(freeFn);
		g_saveNative = reinterpret_cast<Native_t>(save->second.native);
		g_resolved = true;
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		logger::info("engine bridge: found SetVarValue {} (impl {}, FindVar {}), SaveUserSettings {}, CNamePool {} / {}, free {} in {} ms",
			Rva(set->second.native), Rva(impl), Rva(findFn), Rva(save->second.native), Rva(set->second.get), Rva(set->second.add),
			Rva(freeFn), ms);
		return true;
	}

	bool ConfigReady() { return g_resolved.load() && !g_faulted.load(); }

	void Post(std::function<void()> a_job)
	{
		std::scoped_lock l(g_jobsLock);
		g_jobs.push_back(std::move(a_job));
	}

	void AddFrameHook(std::function<void()> a_hook)
	{
		std::scoped_lock l(g_jobsLock);
		g_frameHooks.push_back(std::move(a_hook));
	}

	void Pump()
	{
		std::vector<std::function<void()>> jobs;
		std::vector<std::function<void()>> hooks;
		{
			std::scoped_lock l(g_jobsLock);
			jobs.swap(g_jobs);
			hooks = g_frameHooks;
		}
		for (auto& job : jobs) {
			job();
		}
		for (auto& hook : hooks) {
			hook();
		}
		if (g_savePending.load() && ::GetTickCount64() >= g_saveAfter.load() && ConfigReady()) {
			g_savePending = false;
			if (SafeSave()) {
				++g_saves;
				logger::info("engine bridge: settings saved (the game's SaveUserSettings)");
			} else {
				Fault("SaveUserSettings");
			}
		}
	}

	bool GetVar(const std::string& a_group, const std::string& a_var, std::string& a_out)
	{
		if (!ConfigReady()) {
			return false;
		}
		void* var = FindVarPtr(a_group, a_var);
		if (!var) {
			return false;
		}
		RedString s{};
		if (!SafeGetValue(var, &s)) {
			Fault("the var's GetValue");
			return false;
		}
		const std::uint64_t n = ++g_gets;
		if (s.data && s.size > 0) {
			a_out.assign(s.data, ::strnlen(s.data, s.size));
		} else {
			a_out.clear();
		}
		if (n == 1) {
			// the first live read names the string's shape in the log: narrow text expected ("true", "0.5")
			std::string hex;
			for (std::uint32_t i = 0; s.data && i < std::min<std::uint32_t>(s.size, 12); ++i) {
				hex += std::format("{:02X} ", static_cast<std::uint8_t>(s.data[i]));
			}
			logger::info("engine bridge: first live read {}.{} = \"{}\" (size {}, bytes {})", a_group, a_var, a_out, s.size, hex);
		}
		if (s.data && !SafeFree(const_cast<char*>(s.data))) {
			Fault("free");
		}
		return true;
	}

	bool SetVar(const std::string& a_group, const std::string& a_var, const std::string& a_value)
	{
		if (!ConfigReady()) {
			return false;
		}
		std::uint32_t group = 0, var = 0;
		if (!FindVarPtr(a_group, a_var) || !Name(a_group, group) || !Name(a_var, var)) {
			logger::warn("engine bridge: {}.{} is not a var the game knows; nothing set", a_group, a_var);
			return false;
		}
		const RedString value{ a_value.c_str(), static_cast<std::uint32_t>(a_value.size() + 1), 0 };
		if (!SafeSet(&group, &var, &value)) {
			Fault("SetVarValue");
			return false;
		}
		++g_sets;
		logger::info("engine bridge: set {}.{} = {}", a_group, a_var, a_value);
		return true;
	}

	bool PauseAvailable() { return ConfigReady() && g_pauseSlot > 0 && g_gameGlobal; }

	bool SetGamePaused(bool a_paused)
	{
		if (a_paused == g_gamePaused) { return true; }
		if (!PauseAvailable()) { return false; }
		void* game = *g_gameGlobal;
		if (!game) {
			logger::debug("engine bridge: no game object yet - pause request ignored");
			return false;
		}
		static constexpr char kReason[] = "ApocryphaMenuFramework";
		const RedString reason{ kReason, static_cast<std::uint32_t>(sizeof(kReason)), 0 };
		if (!SafePauseCall(game, a_paused ? g_pauseSlot : g_unpauseSlot, &reason)) {
			Fault(a_paused ? "CGame::Pause" : "CGame::Unpause");
			return false;
		}
		g_gamePaused = a_paused;
		logger::info("engine bridge: game {} (reason \"{}\")", a_paused ? "paused" : "unpaused", kReason);
		return true;
	}

	void RequestSave()
	{
		g_saveAfter = ::GetTickCount64() + 800;   // once edits stop for a moment, as the game saves when its menu closes
		g_savePending = true;
	}

	std::string StatusJson()
	{
		return std::format(R"({{"resolved":{},"faulted":{},"why":"{}","gets":{},"sets":{},"saves":{},"savePending":{}}})",
			g_resolved.load() ? "true" : "false", g_faulted.load() ? "true" : "false", g_why, g_gets.load(), g_sets.load(),
			g_saves.load(), g_savePending.load() ? "true" : "false");
	}
}
