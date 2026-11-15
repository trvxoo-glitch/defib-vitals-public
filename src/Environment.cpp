#include "Vitals.h"
#include "Sha256.h"
#include "Vitals/Formats.h"

// Environment report: what was actually running. Read-only.

namespace Vitals
{
	namespace
	{
		std::string FileSha256(const std::filesystem::path& a_file)
		{
			std::ifstream in(a_file, std::ios::binary);
			if (!in)
				return "";
			Sha256 h;
			std::vector<char> buf(1 << 16);
			while (in) {
				in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
				h.Update(reinterpret_cast<const std::uint8_t*>(buf.data()), static_cast<std::size_t>(in.gcount()));
			}
			return h.Final();
		}

		std::string VersionString(std::uint32_t a_packed)
		{
			// F4SE packs as major<<24 | minor<<16 | build<<4 | sub
			return std::format("{}.{}.{}.{}", (a_packed >> 24) & 0xFF, (a_packed >> 16) & 0xFF, (a_packed >> 4) & 0xFFF, a_packed & 0xF);
		}

		std::string FileVersion(const std::filesystem::path& a_file)
		{
			std::uint32_t handle = 0;
			const auto    size = REX::W32::GetFileVersionInfoSizeW(a_file.c_str(), &handle);
			if (!size)
				return "";
			std::vector<std::uint8_t> buf(size);
			if (!REX::W32::GetFileVersionInfoW(a_file.c_str(), 0, size, buf.data()))
				return "";
			void*         ptr = nullptr;
			std::uint32_t len = 0;
			if (!REX::W32::VerQueryValueW(buf.data(), L"\\", &ptr, &len) || len < 16)
				return "";
			const auto* ffi = static_cast<const std::uint32_t*>(ptr);  // VS_FIXEDFILEINFO: sig, strucVer, verMS, verLS
			return std::format("{}.{}.{}.{}", ffi[2] >> 16, ffi[2] & 0xFFFF, ffi[3] >> 16, ffi[3] & 0xFFFF);
		}

		// SHA-256 of every plugin DLL, remembered in hashes.txt by size and file time, so an unchanged DLL is not read
		// again on every launch.
		class HashCache
		{
		public:
			HashCache()
			{
				std::ifstream in(OutputDir() / L"hashes.txt");
				std::string   line;
				while (std::getline(in, line))
					if (const auto f = Rules::Split(line, '|'); f.size() == 4 && f[2].size() == 64)
						_known[f[3]] = { f[0] + "|" + f[1], f[2] };
			}

			std::string Get(const std::filesystem::path& a_file, std::uint64_t a_size)
			{
				std::error_code ec;
				const auto      t = std::filesystem::last_write_time(a_file, ec).time_since_epoch().count();
				const auto      key = Lower(Narrow(a_file.filename().wstring()));
				const auto      stamp = std::format("{}|{}", a_size, ec ? 0 : t);
				if (const auto it = _known.find(key); it != _known.end() && it->second.first == stamp) {
					_now[key] = it->second;
					return it->second.second;
				}
				auto hash = FileSha256(a_file);
				if (!hash.empty())
					_now[key] = { stamp, hash };
				return hash;
			}

			~HashCache()
			{
				try {
					std::ofstream out(OutputDir() / L"hashes.txt", std::ios::trunc);
					for (const auto& [name, v] : _now)
						out << v.first << "|" << v.second << "|" << name << "\n";
				} catch (...) {
				}
			}

		private:
			std::map<std::string, std::pair<std::string, std::string>> _known, _now;  // name -> (size|time, sha256)
		};
	}

	void ReportEnvironment()
	{
		Crumb("Vitals", "environment");
		const auto game = REX::FModule::GetExecutingModule().GetFileVersion();
		const auto f4se = F4SE::GetF4SEVersion();
		const auto& al = AddressLibrary();

		Emit("environment", Json()
			.Str("game_version", game.string())
			.Str("runtime", RuntimeName())
			.Str("f4se_version", f4se.string())
			.UNum("f4se_release_index", F4SE::GetReleaseIndex())
			.Str("vitals_version", F4SE::GetPluginVersion().string())
			.Str("address_library_file", al.file)
			.Bool("address_library_present", al.present)
			.Bool("address_library_usable", al.usable)
			.Str("address_library_state", al.state)
			.UNum("address_library_entries", al.count)
			.Str("game_dir", Narrow(GameDir().wstring())));  // UTF-8 (path::string() is the ANSI code page)

		Summary(std::format("Vitals {} | game {} ({}) | F4SE {}", F4SE::GetPluginVersion().string(), game.string(), RuntimeName(), f4se.string()));
		Summary(std::format("Address Library {}: {}", al.file, al.usable ? std::format("present, {} entries", al.count) : al.present ? "NOT USABLE (" + al.problem + ")" : "MISSING"s));

		std::error_code ec;
		std::size_t total = 0, loaded = 0;
		HashCache   hashes;
		for (const auto& entry : std::filesystem::directory_iterator(PluginDir(), ec)) {
			if (!entry.is_regular_file(ec) || Lower(Narrow(entry.path().extension().wstring())) != ".dll")  // any case, as F4SE
				continue;
			++total;
			const auto  file = entry.path().filename();
			const auto  hmod = REX::W32::GetModuleHandleW(file.c_str());
			Json        j;
			j.Str("file", Narrow(file.wstring()))
				.UNum("size", entry.file_size(ec))
				.Str("sha256", hashes.Get(entry.path(), entry.file_size(ec)))
				.Str("file_version", FileVersion(entry.path()))
				.Bool("in_process", hmod != nullptr);

			if (hmod) {
				++loaded;
				j.Bool("export_query", REX::W32::GetProcAddress(hmod, "F4SEPlugin_Query") != nullptr)
					.Bool("export_load", REX::W32::GetProcAddress(hmod, "F4SEPlugin_Load") != nullptr)
					.Bool("export_preload", REX::W32::GetProcAddress(hmod, "F4SEPlugin_Preload") != nullptr);
				Formats::VersionDataLayout vd{};
				if (const auto p = REX::W32::GetProcAddress(hmod, "F4SEPlugin_Version"); p && SafeRead(p, &vd, sizeof(vd))) {
					vd.name[255] = vd.author[255] = '\0';
					std::vector<std::string> compat;
					for (const auto c : vd.compatibleVersions) {
						if (!c)
							break;
						compat.push_back(Json::Quote(VersionString(c)));
					}
					j.Str("plugin_name", vd.name)
						.Str("plugin_author", vd.author)
						.Str("plugin_version", VersionString(vd.pluginVersion))
						.UNum("address_independence", vd.addressIndependence)
						.UNum("structure_independence", vd.structureIndependence)
						.Raw("compatible_versions", Json::Array(compat))
						.Str("f4se_minimum", VersionString(vd.seVersionRequired));
					// Ask F4SE itself whether it registered this plugin under that name.
					const auto info = F4SE::GetPluginInfo(vd.name);
					j.Bool("f4se_registered", info != nullptr);
				} else {
					j.Null("plugin_name");
				}
			}
			Emit("plugin", std::move(j));
		}
		Summary(std::format("F4SE plugin folder: {} DLLs, {} in the game process (compare with f4se.log for reasons)", total, loaded));
	}
}
