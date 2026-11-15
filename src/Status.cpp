#include "Vitals.h"

// Timeline and smoke-test support.
//  * MarkPhase writes every F4SE event to Vitals.jsonl ("phase" records, with milliseconds since Vitals
//    loaded) and rewrites status.json, a tiny file an outside script can poll to know how far the game got.
//  * ScheduleExit is an OPT-IN test-rig feature (Vitals.ini iExitAfter*Seconds, default 0 = never). It flushes the
//    logs and then ends the game process WITHOUT saving. Never enable it on a profile you play.

namespace Vitals
{
	namespace
	{
		std::mutex                                  g_lock;
		std::vector<std::pair<std::string, double>> g_phases;
		const auto                                  g_start = std::chrono::steady_clock::now();
		std::atomic_bool                            g_menuExit{ false }, g_loadExit{ false };  // one per timer

		double Ms() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_start).count(); }

		void WriteStatus()
		{
			std::vector<std::string> items;
			for (const auto& [name, ms] : g_phases)
				items.push_back(Json().Str("phase", name).Str("ms", std::format("{:.0f}", ms)).Build());
			const auto last = g_phases.empty() ? std::string{} : g_phases.back().first;
			const auto text = Json()
			                      .Str("vitals_version", F4SE::GetPluginVersion().string())
			                      .UNum("pid", ProcessId())  // lets tools/smoke_test.ps1 stop this game only
			                      .Str("game_version", REX::FModule::GetExecutingModule().GetFileVersion().string())
			                      .Str("last_phase", last)
			                      .Bool("main_menu_ready", std::ranges::any_of(g_phases, [](auto& p) { return p.first == "GameDataReady"; }))
			                      .Bool("save_or_new_game_loaded", std::ranges::any_of(g_phases, [](auto& p) { return p.first == "PostLoadGame" || p.first == "NewGame"; }))
			                      .Raw("phases", Json::Array(items))
			                      .Build();
			// Written to a temporary file and renamed over status.json, so a script polling it never reads half a file.
			// If the rename fails (a reader holds the file open), the file is written in place as before.
			const auto      final = OutputDir() / L"status.json";
			const auto      tmp = OutputDir() / L"status.json.tmp";
			std::error_code ec;
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				out << text << '\n';
				if (!out)
					ec = std::make_error_code(std::errc::io_error);
			}
			if (!ec)
				std::filesystem::rename(tmp, final, ec);
			if (ec) {
				std::filesystem::remove(tmp, ec);
				std::ofstream out(final, std::ios::binary | std::ios::trunc);
				out << text << '\n';
			}
		}
	}

	void MarkPhase(std::string_view a_phase)
	{
		std::scoped_lock l(g_lock);
		const auto       ms = Ms();
		g_phases.emplace_back(std::string(a_phase), ms);
		Emit("phase", Json().Str("event", a_phase).Str("ms_since_vitals_load", std::format("{:.0f}", ms)));
		WriteStatus();
	}

	void ScheduleExit(std::uint32_t a_seconds, std::string_view a_reason)
	{
		// Each timer arms once. Both may be set: the menu timer must not stop the load timer from arming.
		auto& armed = a_reason == "main_menu_ready" ? g_menuExit : g_loadExit;
		if (!a_seconds || armed.exchange(true))
			return;
		const std::string reason(a_reason);
		Summary(std::format("TEST-RIG EXIT scheduled in {} s ({}). The game will be ended without saving.", a_seconds, reason));
		std::thread([a_seconds, reason]() {
			std::this_thread::sleep_for(std::chrono::seconds(a_seconds));
			Emit("auto_exit", Json().Str("reason", reason).UNum("after_seconds", a_seconds));
			Summary("Auto-exit now");
			REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), 0);
		}).detach();
	}
}
