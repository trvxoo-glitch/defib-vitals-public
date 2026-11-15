#include "Vitals.h"

// Vitals entry points. Passive: no engine hooks, no writes to game memory.
// Old-Gen F4SE 0.6.x calls Query then Load. Next-Gen/Anniversary F4SE 0.7.x reads F4SEPlugin_Version
// (res/vitals/commonlibf4-plugin.cpp.in) and calls Preload/Load.

namespace
{
	const char* MessageName(std::uint32_t a_type)
	{
		using M = F4SE::MessagingInterface;
		switch (a_type) {
		case M::kPostLoad: return "PostLoad";
		case M::kPostPostLoad: return "PostPostLoad";
		case M::kPreLoadGame: return "PreLoadGame";
		case M::kPostLoadGame: return "PostLoadGame";
		case M::kPreSaveGame: return "PreSaveGame";
		case M::kPostSaveGame: return "PostSaveGame";
		case M::kDeleteGame: return "DeleteGame";
		case M::kInputLoaded: return "InputLoaded";
		case M::kNewGame: return "NewGame";
		case M::kGameLoaded: return "GameLoaded";
		case M::kGameDataReady: return "GameDataReady";
		default: return "Unknown";
		}
	}

	void HandleMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		using M = F4SE::MessagingInterface;
		const auto& s = Vitals::GetSettings();
		if (s.timeline)
			Vitals::MarkPhase(MessageName(a_msg->type));
		switch (a_msg->type) {
		case M::kPostPostLoad:
			// every plugin has had its Load call by now
			if (s.environment)
				Vitals::Guarded("environment", Vitals::ReportEnvironment);
			// Before the game reads its .esm/.esp files: a load order that is about to crash can still be reported.
			if (s.doctor)
				Vitals::Guarded("doctor", Vitals::RunDoctor);
			break;
		case M::kGameDataReady:
			if (s.addressCheck)
				Vitals::Guarded("address_check", Vitals::CheckAddresses);
			// Deferred to the game's next task tick: plugins that loaded after Vitals get this same message after us, and
			// some register their Papyrus natives in it.
			if (s.papyrusNatives) {
				if (const auto tasks = F4SE::GetTaskInterface())
					tasks->AddTask([]() { Vitals::Guarded("papyrus_natives", Vitals::ListPapyrusNatives); });
				else
					Vitals::Guarded("papyrus_natives", Vitals::ListPapyrusNatives);
			}
			if (s.layoutProbes)
				Vitals::Guarded("layout_probes", [] { Vitals::RunLayoutProbes("game_data_ready"); });
			Vitals::Summary("Game data ready");
			Vitals::ScheduleExit(s.exitAfterMenuSeconds, "main_menu_ready");
			break;
		case M::kPostLoadGame:
		case M::kNewGame:
			if (s.addressCheck)
				Vitals::Guarded("pointer_recheck", Vitals::RecheckPointers);
			if (s.layoutProbes)
				Vitals::Guarded("layout_probes", [&] { Vitals::RunLayoutProbes(a_msg->type == M::kNewGame ? "new_game" : "save_loaded"); });
			Vitals::Emit("session", Vitals::Json().Str("event", a_msg->type == M::kNewGame ? "new_game" : "save_loaded"));
			Vitals::ScheduleExit(s.exitAfterLoadSeconds, "after_load");
			break;
		default:
			break;
		}
		Vitals::Crumb("Vitals", "idle");
	}

	// Vitals must never stop the game: an exception (a file that cannot be read, a bad date, out of memory) is logged
	// and that step is skipped.
	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
		Vitals::Guarded(MessageName(a_msg->type), [&]() { HandleMessage(a_msg); });
	}

	bool InitPlugin(const F4SE::LoadInterface* a_f4se)
	{
		static bool isInit = false;
		static std::once_flag once;
		std::call_once(once, [&]() {
			F4SE::Init(a_f4se, { .logName = "Vitals", .hook = false });  // passive: no hooks, ever
			Vitals::LoadSettings();
			Vitals::OpenReport();
			Vitals::InitBreadcrumbs();
			Vitals::Crumb("Vitals", "load");
			Vitals::Emit("start", Vitals::Json()
				.Str("vitals_version", F4SE::GetPluginVersion().string())
				.Bool("papyrus_natives", Vitals::GetSettings().papyrusNatives)
				.Bool("layout_probes", Vitals::GetSettings().layoutProbes)
				.Bool("doctor", Vitals::GetSettings().doctor));
			if (const auto msg = F4SE::GetMessagingInterface())
				msg->RegisterListener(OnMessage);
			isInit = true;
		});
		return isInit;
	}
}

F4SE_PLUGIN_QUERY(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
{
	// Only Old-Gen F4SE (0.6.x) calls this.
	if (const auto data = F4SE::PluginVersionData::GetSingleton()) {
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->name = data->GetPluginName().data();
		a_info->version = data->GetPluginVersion().pack();
	}
	if (a_f4se->IsEditor())
		return false;
	// Accept only 1.10.163 here; any other runtime calling Query is unexpected.
	return a_f4se->RuntimeVersion() == F4SE::RUNTIME_1_10_163;
}

F4SE_PLUGIN_PRELOAD(const F4SE::LoadInterface* a_f4se)
{
	return InitPlugin(a_f4se);
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	return InitPlugin(a_f4se);
}
