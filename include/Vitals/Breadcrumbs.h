#pragma once
// Vitals breadcrumbs for Defib ports. Header-only; works whether or not Vitals is installed.
//
//   #include <Vitals/Breadcrumbs.h>
//   VITALS_CRUMB("MyPort", "hook:Actor::Update");   // at the top of a hook
//
// Each call writes "which port, which hook" into a crash-safe ring kept by Vitals.dll
// (a memory-mapped file the OS writes out even if the game crashes). If Vitals.dll is not
// loaded, the call does nothing. Cost: one indirect call and two short string copies (plus a module lookup on
// the first calls, until Vitals.dll is found).

#include <atomic>
#include <cstdint>
#include <REX/W32/KERNEL32.h>  // from CommonLibF4 (commonlib-shared); ports already use it

namespace VitalsAPI
{
	using CrumbFn = void (*)(const char* a_port, const char* a_where);

	// Only a successful lookup is kept. F4SE loads plugins in name order, so a port whose DLL sorts before Vitals.dll
	// and crumbs from its Load (or a static constructor) asks before Vitals is there; it asks again on the next call.
	// After 4096 misses (Vitals is not installed) it stops asking, so a hot hook never pays for the lookup.
	inline CrumbFn ResolveCrumb() noexcept
	{
		static std::atomic<CrumbFn>       s_fn{ nullptr };
		static std::atomic<std::uint32_t> s_misses{ 0 };
		if (const auto fn = s_fn.load(std::memory_order_acquire))
			return fn;
		if (s_misses.load(std::memory_order_relaxed) >= 4096)
			return nullptr;
		const auto mod = REX::W32::GetModuleHandleA("Vitals.dll");
		const auto fn = mod ? reinterpret_cast<CrumbFn>(REX::W32::GetProcAddress(mod, "Vitals_Breadcrumb")) : nullptr;
		if (fn)
			s_fn.store(fn, std::memory_order_release);
		else
			s_misses.fetch_add(1, std::memory_order_relaxed);
		return fn;
	}

	inline void Crumb(const char* a_port, const char* a_where) noexcept
	{
		if (auto fn = ResolveCrumb())
			fn(a_port, a_where);
	}
}

#define VITALS_CRUMB(port, where) ::VitalsAPI::Crumb((port), (where))
