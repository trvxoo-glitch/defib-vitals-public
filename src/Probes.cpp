#include "Vitals.h"

// Layout probes (opt-in: bLayoutProbes=1). Each probe reads a known field at every candidate offset
// and reports which candidate looks right. Results feed measured facts into The Chart.
// Only library-provided singletons are used; reads go through SafeRead.

namespace Vitals
{
	namespace
	{
		// BSTimer: the library says lastTime is at 0x18 (Old-Gen/Next-Gen) and 0x20 (Anniversary).
		// Measured facts so far: the 8-byte slot at +0x00 is a QueryPerformanceCounter value taken when the timer
		// started (constant), and lastTime is a millisecond counter. It is NOT wall-clock time since that anchor:
		// the timer pauses while it is disabled (loading screens), so after a load it falls behind (1.10.163:
		// lastTime 33,843 ms when 90,966 ms had passed). A one-shot "close to elapsed time" test therefore fails
		// after any load. So the probe samples twice, 2 seconds apart, on a background thread, and reports
		// the slots that advanced by about 2,000 ms. If nothing advanced, the timer was paused: inconclusive.
		struct TimerSample
		{
			std::vector<std::uint64_t> vals;
			std::vector<bool>          ok;
			std::int64_t               qpc{ 0 };
		};

		TimerSample SampleTimer()
		{
			TimerSample t;
			auto*       timer = RE::BSTimer::GetSingleton();
			REX::W32::QueryPerformanceCounter(&t.qpc);
			const std::size_t size = RE::BSTimer::GetRuntimeSize();
			for (std::size_t off = 0; off + 8 <= size; off += 8) {
				std::uint64_t v = 0;
				t.ok.push_back(timer && SafeRead(reinterpret_cast<const std::byte*>(timer) + off, &v, sizeof(v)));
				t.vals.push_back(v);
			}
			return t;
		}

		void ProbeBSTimer(std::string a_when)
		{
			std::int64_t freq = 1;
			REX::W32::QueryPerformanceFrequency(&freq);
			std::this_thread::sleep_for(std::chrono::seconds(3));  // let a load finish fading in
			const auto a = SampleTimer();
			std::this_thread::sleep_for(std::chrono::seconds(2));
			const auto b = SampleTimer();
			const double wallMs = static_cast<double>(b.qpc - a.qpc) * 1000.0 / static_cast<double>(freq);

			std::vector<std::string> slots, advancing;
			for (std::size_t i = 0; i < a.vals.size() && i < b.vals.size(); ++i) {
				const auto off = i * 8;
				const bool readable = a.ok[i] && b.ok[i];
				const double delta = static_cast<double>(b.vals[i]) - static_cast<double>(a.vals[i]);
				// millisecond counter: small value that advanced by about the wall time (within 25%)
				const bool isMs = readable && b.vals[i] < 0xFFFFFFFFull && delta > wallMs * 0.75 && delta < wallMs * 1.25;
				slots.push_back(Json().Str("offset", std::format("0x{:X}", off)).Bool("readable", readable)
				                    .Str("first", std::format("{:016X}", a.vals[i])).Str("second", std::format("{:016X}", b.vals[i]))
				                    .Bool("advances_like_ms_counter", isMs).Build());
				if (isMs)
					advancing.push_back(std::format("0x{:X}", off));
			}
			const std::string expected = REX::FModule::IsRuntimeAE() ? "0x20" : "0x18";
			const bool        agrees = std::ranges::find(advancing, expected) != advancing.end();
			const std::string verdict = advancing.empty() ? "INCONCLUSIVE (timer paused)" : agrees ? "OK" : "CHECK";
			std::vector<std::string> quoted;
			for (const auto& s : advancing)
				quoted.push_back(Json::Quote(s));
			Emit("layout_probe", Json()
				.Str("probe", "BSTimer.lastTime")
				.Str("method", "two samples, 2 s apart")
				.Str("when", a_when)
				.Str("runtime", RuntimeName())
				.UNum("object_size", RE::BSTimer::GetRuntimeSize())
				.Str("library_offset", expected)
				.Str("wall_ms_between_samples", std::format("{:.0f}", wallMs))
				.Raw("advancing_ms_slots", Json::Array(quoted))
				.Str("verdict", verdict)
				// only a real observation agrees or disagrees: a paused timer says nothing about the layout
				.Raw("agrees_with_library", advancing.empty() ? "null" : agrees ? "true" : "false")
				.Raw("slots", Json::Array(slots)));
			std::string list;
			for (const auto& s : advancing)
				list += (list.empty() ? "" : ", ") + s;
			Summary(std::format("Layout probe BSTimer ({}): slots advancing like a ms counter [{}], library lastTime {} -> {}",
				a_when, list.empty() ? "none" : list, expected, verdict));
		}
	}

	void RunLayoutProbes(std::string_view a_when)
	{
		Crumb("Vitals", "layout_probes");
		// BSTimer::GetSingleton goes through CommonLib's ID lookup, which ends the game on a missing ID. Check first.
		if (!LibraryIdResolves("BSTimer::Singleton")) {
			Summary("Layout probe BSTimer skipped: its Address Library ID does not resolve on this build");
			return;
		}
		// Runs on its own thread: it only reads, and it must not hold up the game while it waits between samples.
		std::thread([when = std::string(a_when)] { ProbeBSTimer(when); }).detach();
	}
}
