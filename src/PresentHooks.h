#pragma once

// The hook tables and the start-up probe behind the overlay's Present hooks (1.0.6, a Nexus report: with ReShade, F1 did
// nothing and no Present hook ever fired). Shared by the .asi (src/Overlay.cpp) and the offline check
// (tools/present_hook_check.cpp), so the check runs the same code the game does.
//
// MORE THAN ONE CLASS PER HOOK. Until 1.0.5 each hook kept ONE original: the first class of swap chain (or factory) AMF met
// was hooked and every later class refused. With a proxy in the chain (ReShade's swap-chain wrapper, a vendor DLL's probe
// swap chain) the class the game's frames really go through can be the second one seen - never hooked, nothing drawn.
// Here every distinct function gets an entry of its own, up to kMaxClasses per hook, and each entry has a detour of its
// own (a template instantiated per index), so a detour always knows its original without looking anything up at
// call time - nothing on the per-frame path takes a lock or depends on what a vtable holds at that moment.
// - InlineSet: MinHook on the function a vtable slot points at (dxgi-level). One entry per distinct target.
// - SlotSet: a direct write of the vtable SLOT (game-facing). One entry per distinct original the slot held; every
//   vtable whose slot held that function is pointed at that entry's detour.
// Entries are only ever added, never removed (hooks stay for the life of the process), under the set's mutex; an entry is
// complete before its hook goes live, and its count is published after, so readers need no lock.

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace presenthooks
{
	constexpr std::size_t kMaxClasses = 8;

	// Who found a hooked function (a bit set: the same function is often found by more than one).
	enum Source : int
	{
		kCreation = 1,     // a swap chain / factory AMF saw being created (its creation hooks)
		kProbe = 2,        // the start-up probe's throwaway swap chain
		kGameFacing = 4,   // the object the game itself holds (a slot write)
		kQueueHook = 8,    // the queue fallback (ID3D12CommandQueue::ExecuteCommandLists, hooked only when it is needed)
	};
	const char* SourceName(int a_bits);   // "the creation hook", "the start-up probe", ...

	std::string ModuleOf(const void* a_address);       // file name of the module a code address lives in, "?" if none
	bool        InSystemDir(const void* a_address);   // code of a module loaded from System32

	// A game-facing detour of ours sitting in a vtable slot -> the function it forwards to (null when not ours). Every
	// SlotSet registers itself, so an InlineSet never MinHooks one of our own slot detours (a game that takes dxgi
	// straight from dxgi.dll has its game-facing slots on dxgi's own vtable, which the dxgi-level hook reads too).
	void* ResolveOwnDetour(void* a_function);
	void  RegisterResolver(void* (*a_resolve)(void*));

	template <template <std::size_t> class D, std::size_t... I>
	std::array<void*, kMaxClasses> DetoursOf(std::index_sequence<I...>)
	{
		return { reinterpret_cast<void*>(&D<I>::Fn)... };
	}
	// D<I>::Fn is the detour of entry I.
	template <template <std::size_t> class D>
	std::array<void*, kMaxClasses> Detours()
	{
		return DetoursOf<D>(std::make_index_sequence<kMaxClasses>{});
	}

	void LogHooked(const char* a_what, std::size_t a_entry, void* a_target, const std::string& a_module, int a_source, bool a_slot);
	void LogHookFailed(const char* a_what, void* a_target, const char* a_why);

	template <class Fn>
	class InlineSet
	{
	public:
		struct Entry
		{
			void*                      target = nullptr;
			Fn                         original = nullptr;
			std::string                module;
			std::atomic<int>           seenBy{ 0 };
			std::atomic<std::uint64_t> calls{ 0 };
		};

		InlineSet(const char* a_what, std::size_t a_index, std::array<void*, kMaxClasses> a_detours) :
			_what(a_what), _index(a_index), _detours(a_detours) {}

		// Hooks the function a_object's vtable slot points at, unless it already is. The entry index, or -1.
		int Hook(void* a_object, int a_source)
		{
			auto** vtbl = a_object ? *reinterpret_cast<void***>(a_object) : nullptr;
			void*  target = vtbl ? vtbl[_index] : nullptr;
			if (!target) {
				LogHookFailed(_what, nullptr, "the vtable slot is empty");
				return -1;
			}
			// our game-facing detour sits in this slot (only SlotSet detours are ever put in a vtable): hook what it forwards to
			if (void* forwardsTo = ResolveOwnDetour(target)) {
				target = forwardsTo;
			}
			return HookTarget(target, a_source);
		}

		int HookTarget(void* a_target, int a_source)
		{
			std::scoped_lock l(_lock);
			const std::size_t n = _count.load(std::memory_order_acquire);
			for (std::size_t i = 0; i < n; ++i) {
				if (_entries[i].target == a_target) {
					_entries[i].seenBy.fetch_or(a_source);
					return static_cast<int>(i);
				}
			}
			if (n >= kMaxClasses) {
				LogHookFailed(_what, a_target, "every entry is in use");
				return -1;
			}
			Entry& e = _entries[n];
			e.target = a_target;
			e.module = ModuleOf(a_target);
			e.seenBy = a_source;
			if (const auto st = MH_CreateHook(a_target, _detours[n], reinterpret_cast<void**>(&e.original)); st != MH_OK) {
				e.target = nullptr;
				e.original = nullptr;
				LogHookFailed(_what, a_target, MH_StatusToString(st));
				return -1;
			}
			if (const auto st = MH_EnableHook(a_target); st != MH_OK) {
				MH_RemoveHook(a_target);
				e.target = nullptr;
				e.original = nullptr;
				LogHookFailed(_what, a_target, MH_StatusToString(st));
				return -1;
			}
			_count.store(n + 1, std::memory_order_release);
			LogHooked(_what, n, a_target, e.module, a_source, false);
			return static_cast<int>(n);
		}

		Fn           Original(std::size_t a_i) const { return _entries[a_i].original; }
		Entry&       At(std::size_t a_i) { return _entries[a_i]; }
		std::size_t  Count() const { return _count.load(std::memory_order_acquire); }
		const char*  What() const { return _what; }

	private:
		const char*                    _what;
		std::size_t                    _index;
		std::array<void*, kMaxClasses> _detours;
		Entry                          _entries[kMaxClasses];
		std::atomic<std::size_t>       _count{ 0 };
		std::mutex                     _lock;
	};

	template <class Fn>
	class SlotSet
	{
	public:
		struct Entry
		{
			Fn                         original = nullptr;
			std::string                module;
			std::atomic<std::uint64_t> calls{ 0 };
		};

		SlotSet(const char* a_what, std::size_t a_index, std::array<void*, kMaxClasses> a_detours) :
			_what(a_what), _index(a_index), _detours(a_detours)
		{
			Register(this);
		}

		// Points a_object's vtable slot at our detour. The entry index, or -1.
		int Write(void* a_object)
		{
			auto** vtbl = a_object ? *reinterpret_cast<void***>(a_object) : nullptr;
			if (!vtbl) {
				return -1;
			}
			std::scoped_lock l(_lock);
			void* current = vtbl[_index];
			if (!current) {
				LogHookFailed(_what, nullptr, "the vtable slot is empty (game-facing)");
				return -1;
			}
			const std::size_t n = _count.load(std::memory_order_acquire);
			for (std::size_t i = 0; i < n; ++i) {
				if (current == _detours[i]) {
					return static_cast<int>(i);   // this class is already ours
				}
			}
			std::size_t i = 0;
			while (i < n && reinterpret_cast<void*>(_entries[i].original) != current) {
				++i;
			}
			const bool fresh = i == n;
			if (fresh) {
				if (n >= kMaxClasses) {
					LogHookFailed(_what, current, "every game-facing entry is in use");
					return -1;
				}
				_entries[i].original = reinterpret_cast<Fn>(current);   // set before the slot is live, so the detour has it
				_entries[i].module = ModuleOf(current);
			}
			DWORD old = 0;
			if (!VirtualProtect(&vtbl[_index], sizeof(void*), PAGE_READWRITE, &old)) {
				LogHookFailed(_what, current, "VirtualProtect refused the vtable");
				return -1;
			}
			InterlockedExchangePointer(&vtbl[_index], _detours[i]);
			VirtualProtect(&vtbl[_index], sizeof(void*), old, &old);
			if (fresh) {
				_count.store(n + 1, std::memory_order_release);
			}
			LogHooked(_what, i, current, _entries[i].module, kGameFacing, true);
			return static_cast<int>(i);
		}

		Fn          Original(std::size_t a_i) const { return _entries[a_i].original; }
		Entry&      At(std::size_t a_i) { return _entries[a_i]; }
		std::size_t Count() const { return _count.load(std::memory_order_acquire); }
		const char* What() const { return _what; }

		void* OriginalOf(void* a_detour) const
		{
			const std::size_t n = _count.load(std::memory_order_acquire);
			for (std::size_t i = 0; i < n; ++i) {
				if (_detours[i] == a_detour) {
					return reinterpret_cast<void*>(_entries[i].original);
				}
			}
			return nullptr;
		}

	private:
		static void Register(SlotSet* a_self);

		const char*                    _what;
		std::size_t                    _index;
		std::array<void*, kMaxClasses> _detours;
		Entry                          _entries[kMaxClasses];
		std::atomic<std::size_t>       _count{ 0 };
		std::mutex                     _lock;
	};

	// Every SlotSet<Fn> answers ResolveOwnDetour (a fixed table; sets are globals made before any hook exists).
	namespace detail
	{
		constexpr std::size_t kMaxSlotSets = 16;
		template <class Fn>
		inline std::array<SlotSet<Fn>*, kMaxSlotSets> g_sets{};
		template <class Fn>
		inline std::atomic<std::size_t> g_setCount{ 0 };
		template <class Fn>
		void* Resolve(void* a_function)
		{
			const std::size_t n = g_setCount<Fn>.load(std::memory_order_acquire);
			for (std::size_t i = 0; i < n; ++i) {
				if (void* p = g_sets<Fn>[i]->OriginalOf(a_function)) {
					return p;
				}
			}
			return nullptr;
		}
	}

	template <class Fn>
	void SlotSet<Fn>::Register(SlotSet* a_self)
	{
		const std::size_t n = detail::g_setCount<Fn>.load();
		if (n < detail::kMaxSlotSets) {
			detail::g_sets<Fn>[n] = a_self;
			detail::g_setCount<Fn>.store(n + 1, std::memory_order_release);
			if (n == 0) {
				RegisterResolver(&detail::Resolve<Fn>);
			}
		}
	}

	// ---- the start-up probe --------------------------------------------------------------------------------------
	// A throwaway D3D12 device, DIRECT queue, hidden 1x1 window and flip-model swap chain, made with System32's d3d12.dll
	// and dxgi.dll loaded BY FULL PATH (a d3d12.dll / dxgi.dll proxy beside the exe is never what it uses). It reads which
	// functions a real dxgi swap chain's Present (8), Present1 (22), ResizeBuffers (13) and SetColorSpace1 (38) slots hold,
	// so they are hooked whatever path the game's own swap chain was created on. Everything is released before it returns.
	// Never call it under the loader lock (it creates a device, which starts driver threads): run it on a thread of its own.
	using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);

	struct ProbeResult
	{
		bool        ok = false;
		std::string summary;   // "ok: Present in dxgi.dll" or what failed
		void*       present = nullptr;
		void*       present1 = nullptr;
		void*       resizeBuffers = nullptr;
		void*       setColorSpace1 = nullptr;
	};

	// a_createFactory2: the REAL CreateDXGIFactory2 (a trampoline past the caller's export hook), or null for System32's
	// export as it stands. a_onFactory runs on the dummy factory before the swap chain is made, a_onSwapChain on the dummy
	// swap chain before it is released (the caller hooks through them; either may be null).
	ProbeResult RunProbe(CreateFactory2_t a_createFactory2, void (*a_onFactory)(IUnknown*), void (*a_onSwapChain)(IUnknown*));

	// ---- the queue a swap chain presents from, when its creation was not seen --------------------------------------------
	// First GetDevice(ID3D12CommandQueue) on the swap chain. (Proven by tools/present_hook_check on Windows 11 build 26200:
	// dxgi answers E_NOINTERFACE there - its GetDevice gives the D3D12 DEVICE - but a wrapper or another dxgi may answer.)
	// Then a_candidate: the last DIRECT queue that executed command lists on the presenting thread, taken only when it is on
	// the swap chain's own device. Returns a reference the caller keeps (or null) and names the route in a_from.
	ID3D12CommandQueue* QueueForSwapChain(IDXGISwapChain* a_swapChain, ID3D12CommandQueue* a_candidate, const char** a_from);

	// Logs every dxgi / d3d12 / d3d11 / ReShade module loaded from outside System32 (file name, ProductName, FileVersion),
	// and whether a dxgi.dll or d3d12.dll file sits in the exe's folder. a_when names the moment in the log.
	void LogGraphicsModules(const char* a_when);
}
