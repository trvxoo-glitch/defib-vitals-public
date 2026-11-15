#include "ModScan.h"

#include "Vitals.h"
#include "Vitals/Share.h"

namespace Vitals::ModScan
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr std::size_t kMaxStateBackup = 64u << 20;  // Vortex's full state copy; bigger ones are skipped
		constexpr std::size_t kMaxModFolders = 20'000;

		std::string ReadText(const std::filesystem::path& a_file, std::size_t a_max)
		{
			const auto d = ReadBytes(a_file, a_max);
			return { d.begin(), d.end() };
		}

		std::filesystem::path EnvDir(const wchar_t* a_name)
		{
			wchar_t    buf[512]{};
			const auto n = REX::W32::GetEnvironmentVariableW(a_name, buf, 512);
			return n && n < 512 ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::path{};
		}

		std::string Ms(Clock::time_point a_since)
		{
			return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - a_since).count());
		}

		// The MO2 instance running the game. usvfs_x64.dll is loaded from the MO2 program folder; a portable instance
		// keeps ModOrganizer.ini there, a global one in %LOCALAPPDATA%\ModOrganizer\<instance>. That folder holds the
		// instances of every game, so only one whose gamePath is this game folder counts; of several, the one saved
		// most recently (MO2 writes its ini when it starts and closes).
		std::filesystem::path Mo2Ini(const std::filesystem::path& a_programDir, std::vector<std::string>& a_notes)
		{
			std::error_code ec;
			const auto      game = Narrow(GameDir().wstring());
			auto            forThisGame = [&](const std::filesystem::path& ini) {
                return std::filesystem::is_regular_file(ini, ec) &&
				       ModManagers::SameDir(ModManagers::Mo2GamePath(ReadText(ini, 1u << 20)), game);
			};
			if (const auto portable = a_programDir / L"ModOrganizer.ini"; !a_programDir.empty() && forThisGame(portable))
				return portable;
			std::filesystem::path           best;
			std::filesystem::file_time_type bestTime{};
			std::size_t                     matches = 0;
			if (const auto la = EnvDir(L"LOCALAPPDATA"); !la.empty())
				for (const auto& inst : ListDir(la / L"ModOrganizer", 256)) {
					const auto ini = inst.path() / L"ModOrganizer.ini";
					if (!forThisGame(ini))
						continue;
					++matches;
					const auto t = std::filesystem::last_write_time(ini, ec);
					if (!ec && (best.empty() || t > bestTime))
						best = ini, bestTime = t;
				}
			if (matches > 1)
				a_notes.push_back(std::format("MO2: {} instances manage this game folder; used \"{}\", the one saved most recently",
					matches, Narrow(best.parent_path().filename().wstring())));
			return best;
		}

		// Vortex's own update check: the newest file in %APPDATA%\Vortex\temp\state_backups_full.
		void ReadVortexUpdates(Result& a_out, Clock::time_point a_deadline)
		{
			const auto ad = EnvDir(L"APPDATA");
			if (ad.empty())
				return;
			std::error_code                 ec;
			std::filesystem::path           newest;
			std::filesystem::file_time_type when{};
			for (const auto& f : ListDir(ad / L"Vortex" / L"temp" / L"state_backups_full", 1000)) {
				if (Lower(Narrow(f.path().extension().wstring())) != ".json")
					continue;
				const auto t = f.last_write_time(ec);
				if (!ec && (newest.empty() || t > when))
					newest = f.path(), when = t;
			}
			if (newest.empty()) {
				a_out.notes.push_back("Vortex update info: no state backup found, so Vortex's update check could not be read");
				return;
			}
			const auto size = std::filesystem::file_size(newest, ec);
			if (ec || size > kMaxStateBackup) {
				a_out.notes.push_back(std::format("Vortex update info: {} is {} MB, too big to read before the start-up box; update check skipped",
					Narrow(newest.filename().wstring()), ec ? 0 : size >> 20));
				return;
			}
			// Reading and walking the JSON runs at roughly 100 MB a second; it must fit in the time that is left.
			const auto need = std::chrono::milliseconds(50 + size / (100'000));
			if (Clock::now() + need > a_deadline) {
				a_out.cut = true;
				a_out.notes.push_back(std::format("Vortex update info: no time left before the start-up box to read {} ({} MB); update check skipped",
					Narrow(newest.filename().wstring()), size >> 20));
				return;
			}
			const auto text = ReadText(newest, kMaxStateBackup);
			a_out.vortexMods = ModManagers::VortexStateMods(text);
			std::size_t withNewest = 0;
			for (const auto& [k, v] : a_out.vortexMods)
				withNewest += !v.newest.empty();
			a_out.notes.push_back(std::format("Vortex update info: read {} ({} MB), {} mods in it, {} with a newest version recorded",
				Narrow(newest.filename().wstring()), text.size() >> 20, a_out.vortexMods.size(), withNewest));
		}

		bool SameBytes(const std::filesystem::path& a_file, const std::vector<std::uint8_t>& a_deployed)
		{
			std::error_code ec;
			const auto      size = std::filesystem::file_size(a_file, ec);
			if (ec || size != a_deployed.size())
				return false;
			return ReadBytes(a_file, a_deployed.size()) == a_deployed;
		}
	}

	const DllCopies* Result::For(const std::string& a_dll) const
	{
		const auto it = dlls.find(Lower(a_dll));
		return it == dlls.end() ? nullptr : &it->second;
	}

	std::string Result::NewerOnNexus(const std::string& a_mod) const
	{
		if (a_mod.empty())
			return {};
		if (manager == "Vortex") {
			const auto it = vortexMods.find(Lower(a_mod));
			if (it != vortexMods.end() && ModManagers::NewerVersion(it->second.newest, it->second.version))
				return std::format(" Vortex says a newer version of this mod is on Nexus: {} (you have {}).", it->second.newest, it->second.version);
		} else if (manager == "MO2") {
			const auto it = mo2Meta.find(Lower(a_mod));
			if (it != mo2Meta.end() && ModManagers::NewerVersion(it->second.newest, it->second.version))
				return std::format(" MO2 says a newer version of this mod is on Nexus: {} (you have {}).", it->second.newest, it->second.version);
		}
		return {};
	}

	std::string Result::ModVersion(const std::string& a_mod) const
	{
		if (const auto it = vortexMods.find(Lower(a_mod)); it != vortexMods.end())
			return it->second.version;
		if (const auto it = mo2Meta.find(Lower(a_mod)); it != mo2Meta.end())
			return it->second.version;
		return {};
	}

	Result Find(const std::vector<Deployed>& a_dlls, Clock::time_point a_deadline)
	{
		Result                      out;
		const auto                  started = Clock::now();
		const auto                  deadline = a_deadline;
		out.deadline = a_deadline;
		std::error_code             ec;
		std::filesystem::path       root;     // folder whose subfolders are installed mods
		std::vector<ModManagers::ModListEntry> order;  // MO2: selected profile, winner first
		Share::VortexDlls           vortex;

		// MO2 when usvfs is in the game process (it may run the game even with an old Vortex manifest in Data).
		if (const auto usvfs = REX::W32::GetModuleHandleW(L"usvfs_x64.dll")) {
			out.manager = "MO2";
			wchar_t    buf[1024]{};
			const auto n = REX::W32::GetModuleFileNameW(usvfs, buf, 1024);
			const auto programDir = n && n < 1024 ? std::filesystem::path(std::wstring(buf, n)).parent_path() : std::filesystem::path{};
			const auto ini = Mo2Ini(programDir, out.notes);
			if (ini.empty()) {
				out.notes.push_back("MO2 is running the game, but no MO2 instance for this game folder was found, so other copies were not looked for");
				return out;
			}
			const auto text = ReadText(ini, 1u << 20);
			const auto instance = Narrow(ini.parent_path().wstring());
			root = Widen(ModManagers::Mo2ModsDir(text, instance));
			order = ModManagers::Mo2ModList(ReadText(std::filesystem::path(Widen(ModManagers::Mo2ProfileDir(text, instance))) / L"modlist.txt", 8u << 20));
			if (order.empty())
				out.notes.push_back("MO2: the selected profile's modlist.txt could not be read, so which mod wins a file conflict is not known");
		} else {
			vortex = Share::ParseVortexManifest(ReadText(GameDir() / L"Data" / L"vortex.deployment.json", 128u << 20));
			if (vortex.staging.empty())
				return out;  // no mod manager found: nothing to add
			out.manager = "Vortex";
			root = Widen(vortex.staging);
		}

		// One walk: every mod folder that has an F4SE\Plugins folder, matched against all the DLLs at once.
		std::set<std::string> wanted;
		for (const auto& d : a_dlls)
			wanted.insert(Lower(d.name));
		std::map<std::string, std::vector<Copy>> found;  // lower DLL name -> copies
		std::size_t                              mods = 0;
		bool&                                    cut = out.cut;
		for (const auto& mod : ListDir(root, kMaxModFolders)) {
			if (Clock::now() > deadline) {
				cut = true;
				break;
			}
			if (!mod.is_directory(ec))
				continue;
			++mods;
			const auto plugins = mod.path() / L"F4SE" / L"Plugins";
			if (!std::filesystem::is_directory(plugins, ec))
				continue;
			for (const auto& f : ListDir(plugins, 4096)) {
				const auto name = Lower(Narrow(f.path().filename().wstring()));
				if (wanted.contains(name) && f.is_regular_file(ec))
					found[name].push_back({ Narrow(mod.path().filename().wstring()), f.path(), true });
			}
		}

		std::map<std::string, std::size_t> priority;  // MO2: lower mod name -> place in modlist.txt (0 wins)
		for (std::size_t i = 0; i < order.size(); ++i)
			priority.try_emplace(Lower(order[i].name), i);
		for (const auto& e : order)
			if (!e.enabled)
				priority[Lower(e.name)] += 1'000'000;  // off: never the winner, listed after the ones that are on
		std::set<std::string> named;  // mods whose versions are worth reading
		for (const auto& d : a_dlls) {
			const auto key = Lower(d.name);
			auto&      result = out.dlls[key];
			auto       copies = std::move(found[key]);
			if (out.manager == "Vortex") {
				for (const auto& [dll, src] : vortex.dlls)
					if (Lower(dll) == key)
						result.installedBy = src;
			} else {
				for (auto& c : copies) {  // with no modlist.txt to go by, nothing is known to be off
					const auto it = priority.find(Lower(c.mod));
					c.enabled = order.empty() || (it != priority.end() && it->second < 1'000'000);
				}
				std::ranges::stable_sort(copies, [&](const Copy& a, const Copy& b) {
					const auto pa = priority.contains(Lower(a.mod)) ? priority.at(Lower(a.mod)) : SIZE_MAX;
					const auto pb = priority.contains(Lower(b.mod)) ? priority.at(Lower(b.mod)) : SIZE_MAX;
					return pa < pb;
				});
				// The game sees the copy of the highest-priority mod that is on. If that copy is not the deployed file,
				// the file comes from somewhere else (MO2's overwrite folder, or the game's own Data folder).
				if (!order.empty() && !copies.empty() && copies.front().enabled && Clock::now() <= deadline) {
					if (SameBytes(copies.front().file, *d.bytes))
						result.installedBy = copies.front().mod;
					else {
						result.shadowed = std::move(copies.front());
						copies.erase(copies.begin());
						named.insert(Lower(result.shadowed->mod));
					}
				}
			}
			if (!result.installedBy.empty())
				named.insert(Lower(result.installedBy));
			for (auto& c : copies) {
				if (Clock::now() > deadline) {
					cut = true;  // the rest of this DLL's copies are not compared (OtherCopies says the list is short)
					break;
				}
				if (Lower(c.mod) == Lower(result.installedBy) || SameBytes(c.file, *d.bytes))
					continue;  // the copy the game sees, or the same file in another mod: nothing new to say
				named.insert(Lower(c.mod));
				result.others.push_back(std::move(c));
			}
		}

		if (out.manager == "Vortex" && !named.empty())
			ReadVortexUpdates(out, deadline);
		if (out.manager == "MO2")
			for (const auto& mod : named)
				out.mo2Meta[mod] = ModManagers::Mo2Meta(ReadText(root / std::filesystem::path(Widen(mod)) / L"meta.ini", 1u << 20));

		out.notes.insert(out.notes.begin(),
			std::format("Mod manager: {}; looked through {} installed mods for other copies of the {} not-running DLL(s) in {} ms{}", out.manager, mods,
				a_dlls.size(), Ms(started), cut ? " (stopped at the time limit, so the list of other copies may be short)" : ""));
		return out;
	}
}
