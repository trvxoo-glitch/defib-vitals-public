#include "Vitals.h"
#include "Vitals/Rules.h"
#include "Vitals/Scrub.h"
#include "Vitals/Share.h"
#include "Vitals/Zip.h"

#include <intrin.h>
#include <random>

// Share pack (opt-in, off unless the player chose it in the installer or set bSharePack=1).
//
// Writes Documents\My Games\Fallout4\F4SE\Vitals\Vitals-Share.zip: a fixed list of files that explain why mods fail,
// each one scrubbed of personal details (include/Vitals/Scrub.h), plus summaries in place of raw files where a raw file
// would carry more than needed (include/Vitals/Share.h). Vitals has no network code: the player decides whether to send
// the zip and where. The full list and the reasons are in docs/PRIVACY_AND_SAFETY.md.
//
// Written twice per launch, on a background thread so the game never waits: at PostPostLoad, right after the report
// is written and before the start-up box shows (so the previous session's logs and a crash log from it are packed
// even if this launch never reaches the menu, or the player presses "Stop game"), and again at the main menu (with this session's address check). Both Papyrus logs are summed up,
// so the second write does not lose the previous session's Papyrus errors.

// kernel32 functions CommonLib's REX::W32 does not declare.
extern "C" __declspec(dllimport) int __stdcall GlobalMemoryStatusEx(void* a_status);

namespace Vitals
{
	namespace
	{
		std::mutex g_shareLock;
		const auto g_started = std::filesystem::file_time_type::clock::now();  // when Vitals was loaded: this launch

		std::filesystem::path MarkerFile() { return PluginDir() / L"Vitals" / L"share-optin.txt"; }
		std::filesystem::path PackFile() { return OutputDir() / L"Vitals-Share.zip"; }
		std::filesystem::path IdFile() { return OutputDir() / L"share-id.txt"; }
		// Documents\My Games\Fallout4 (OutputDir is <that>\F4SE\Vitals)
		std::filesystem::path MyGames() { return OutputDir().parent_path().parent_path(); }

		std::string EnvU(const wchar_t* a_name)
		{
			wchar_t    buf[1024]{};
			const auto n = REX::W32::GetEnvironmentVariableW(a_name, buf, 1024);
			return (n > 0 && n < 1024) ? Narrow(std::wstring(buf, n)) : std::string{};
		}

		// The last a_max bytes of a file (the newest part of a log), cut at a line start.
		std::string ReadTail(const std::filesystem::path& a_file, std::size_t a_max)
		{
			std::ifstream in(a_file, std::ios::binary);
			if (!in)
				return {};
			std::error_code ec;
			const auto      size = static_cast<std::size_t>(std::filesystem::file_size(a_file, ec));
			if (ec)
				return {};
			const std::size_t from = size > a_max ? size - a_max : 0;
			in.seekg(static_cast<std::streamoff>(from));
			std::string data(size - from, '\0');
			in.read(data.data(), static_cast<std::streamsize>(data.size()));
			data.resize(static_cast<std::size_t>(in.gcount()));
			return Share::FileTail(std::move(data), from);
		}

		// A random id, made once, so packs from the same install can be counted together without saying whose they
		// are. Not derived from the PC; deleting share-id.txt makes a new one.
		std::string InstallId()
		{
			std::ifstream in(IdFile());
			std::string   id;
			if (in && std::getline(in, id)) {
				id = Rules::Trim(id);
				if (id.size() == 32 && std::ranges::all_of(id, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }))
					return id;
			}
			std::random_device rd;
			id.clear();
			for (int i = 0; i < 4; ++i)
				id += std::format("{:08x}", rd());
			std::ofstream(IdFile(), std::ios::trunc) << id << "\n";
			return id;
		}

		std::string WindowsVersion()
		{
			struct OsVersion
			{
				std::uint32_t size, major, minor, build, platform;
				wchar_t       csd[128];
			} v{};
			v.size = sizeof(v);
			using RtlGetVersion_t = long(__stdcall*)(OsVersion*);
			const auto ntdll = REX::W32::GetModuleHandleW(L"ntdll.dll");
			const auto fn = reinterpret_cast<RtlGetVersion_t>(ntdll ? REX::W32::GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
			if (!fn || fn(&v) != 0)
				return "unknown";
			// Windows 11 still reports 10.0; build 22000 and later is 11
			return std::format("Windows {} (10.0.{})", v.major == 10 && v.build >= 22000 ? 11 : v.major, v.build);
		}

		std::string CpuName()
		{
			int  regs[4]{};
			char brand[49]{};
			__cpuidex(regs, static_cast<int>(0x80000000), 0);
			if (static_cast<unsigned>(regs[0]) < 0x80000004u)
				return "unknown";
			for (int i = 0; i < 3; ++i) {
				__cpuidex(regs, static_cast<int>(0x80000002 + i), 0);
				std::memcpy(brand + i * 16, regs, 16);
			}
			return Rules::Trim(brand);
		}

		std::vector<std::string> GpuNames()
		{
			struct DisplayDevice
			{
				std::uint32_t cb;
				wchar_t       name[32];
				wchar_t       text[128];
				std::uint32_t flags;
				wchar_t       id[128];
				wchar_t       key[128];
			};
			using Enum_t = int(__stdcall*)(const wchar_t*, std::uint32_t, DisplayDevice*, std::uint32_t);
			const auto               user32 = REX::W32::GetModuleHandleW(L"user32.dll");
			const auto               fn = reinterpret_cast<Enum_t>(user32 ? REX::W32::GetProcAddress(user32, "EnumDisplayDevicesW") : nullptr);
			std::vector<std::string> out;
			for (std::uint32_t i = 0; fn && i < 16; ++i) {
				DisplayDevice d{};
				d.cb = sizeof(d);
				if (!fn(nullptr, i, &d, 0))
					break;
				constexpr std::uint32_t kAttached = 0x1;
				const auto              n = Narrow(d.text);
				if ((d.flags & kAttached) && !n.empty() && std::ranges::find(out, n) == out.end())
					out.push_back(n);
			}
			return out;
		}

		std::string SystemText()
		{
			struct MemoryStatus
			{
				std::uint32_t length, load;
				std::uint64_t totalPhys, availPhys, totalPage, availPage, totalVirtual, availVirtual, availExtended;
			} m{};
			m.length = sizeof(m);
			const auto ramGb = GlobalMemoryStatusEx(&m) ? (m.totalPhys + (512ull << 20)) >> 30 : 0;
			std::string gpus;
			for (const auto& g : GpuNames())
				gpus += (gpus.empty() ? "" : ", ") + g;
			std::error_code ec;
			const bool      mo2 = REX::W32::GetModuleHandleW(L"usvfs_x64.dll") != nullptr;
			const bool      vortex = std::filesystem::exists(GameDir() / L"Data" / L"vortex.deployment.json", ec);
			return std::format(
				"System summary by Vitals (nothing that identifies the PC)\n"
				"Windows: {}\nCPU: {}\nLogical cores: {}\nRAM: {} GB\nGPU: {}\nMod manager: {}\n",
				WindowsVersion(), CpuName(), std::thread::hardware_concurrency(), ramGb, gpus.empty() ? "unknown" : gpus,
				mo2 ? "Mod Organizer 2" : vortex ? "Vortex" : "none found");
		}

		std::string IniText()
		{
			std::string out = "Game INI settings by Vitals: only the keys Vitals checks, nothing else is copied. Later files win in game.\n";
			for (const wchar_t* file : { L"Fallout4.ini", L"Fallout4Prefs.ini", L"Fallout4Custom.ini" }) {
				const auto path = MyGames() / file;
				std::error_code ec;
				if (!std::filesystem::exists(path, ec)) {
					out += std::format("\n{}: not found\n", Narrow(file));
					continue;
				}
				out += std::format("\n{}\n", Narrow(file));
				for (const auto& k : Share::kIniKeys) {
					constexpr const wchar_t* kMissing = L"\x01";
					const auto               section = Widen(k.section), key = Widen(k.key);
					// GetPrivateProfileStringW cuts a long value without saying so (it returns size - 1): grow the
					// buffer until the value fits. SResourceArchiveList and friends run to several KB on a big setup.
					std::wstring v;
					bool         cut = true;
					for (std::uint32_t size = 1024; size <= (64u << 10) && cut; size *= 4) {
						std::vector<wchar_t> buf(size);
						const auto           n = REX::W32::GetPrivateProfileStringW(section.c_str(), key.c_str(), kMissing, buf.data(), size, path.c_str());
						v.assign(buf.data(), n);
						cut = n >= size - 1;
					}
					if (v != kMissing)
						out += std::format("  [{}] {}={}{}\n", k.section, k.key, Narrow(v), cut ? "  [Vitals: value longer than 64K characters, the rest was cut]" : "");
				}
			}
			return out;
		}

		// Buffout 4-style crash logs (crash-*.log next to f4se.log): up to 3, from the last 14 days, newest first.
		std::vector<std::filesystem::path> CrashLogs()
		{
			std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> found;
			std::error_code                                                               ec;
			const auto                                                                    now = std::filesystem::file_time_type::clock::now();
			for (const auto& e : ListDir(MyGames() / L"F4SE")) {
				const auto name = Lower(Narrow(e.path().filename().wstring()));
				if (!name.starts_with("crash-") || !name.ends_with(".log"))
					continue;
				const auto when = e.last_write_time(ec);
				if (!ec && now - when < std::chrono::days(14))
					found.emplace_back(when, e.path());
			}
			std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first > b.first; });
			std::vector<std::filesystem::path> out;
			for (std::size_t i = 0; i < found.size() && i < 3; ++i)
				out.push_back(found[i].second);
			return out;
		}

		void Write(std::string_view a_when)
		{
			std::scoped_lock l(g_shareLock);
			std::error_code  ec;
			const auto       out = OutputDir();
			const auto       myGames = MyGames();

			// Vortex: which mod folder deployed each DLL, and the staging folder to shorten.
			Share::VortexDlls vortex;
			if (const auto manifest = ReadBytes(GameDir() / L"Data" / L"vortex.deployment.json", 128u << 20); !manifest.empty())
				vortex = Share::ParseVortexManifest(std::string_view(reinterpret_cast<const char*>(manifest.data()), manifest.size()));

			Scrub::Context ctx;
			ctx.user = EnvU(L"USERNAME");
			ctx.pc = EnvU(L"COMPUTERNAME");
			ctx.domain = EnvU(L"USERDOMAIN");
			ctx.roots = {
				{ Narrow(GameDir().wstring()), "<game>" },
				{ Narrow(myGames.parent_path().parent_path().wstring()), "<docs>" },  // Documents
				{ EnvU(L"LOCALAPPDATA"), "<localappdata>" },
				{ EnvU(L"APPDATA"), "<appdata>" },
				{ EnvU(L"USERPROFILE"), "%USERPROFILE%" },
			};
			if (!vortex.staging.empty())
				ctx.roots.emplace_back(vortex.staging, "<staging>");

			Scrub::Counts                                    counts;
			std::vector<std::pair<std::string, std::string>> files;  // name in zip, scrubbed text
			std::vector<std::string>                         leftOut;
			std::size_t                                      total = 0;
			auto add = [&](std::string a_name, std::string a_text, bool a_crash = false, bool a_trimmed = false) {
				if (a_text.empty())
					return;
				if (!a_trimmed)  // a file's tail is already cut, and says how much of the file was left out
					a_text = Share::KeepTail(std::move(a_text), Share::kMaxFileBytes);
				auto clean = a_crash ? Scrub::CrashLog(a_text, ctx, counts) : Scrub::Text(a_text, ctx, counts);
				if (total + clean.size() > Share::kMaxPackBytes) {
					leftOut.push_back(std::move(a_name));
					return;
				}
				total += clean.size();
				files.emplace_back(std::move(a_name), std::move(clean));
			};
			auto addFile = [&](std::string a_name, const std::filesystem::path& a_path, bool a_crash = false) {
				add(std::move(a_name), ReadTail(a_path, Share::kMaxFileBytes), a_crash, true);
			};

			// most useful first, so the size cap drops the oldest data before the report
			addFile("Vitals-Report.txt", out / L"Vitals-Report.txt");
			addFile("Vitals-summary.txt", out / L"Vitals-summary.txt");
			addFile("status.json", out / L"status.json");
			addFile("f4se.log", myGames / L"F4SE" / L"f4se.log");
			addFile("f4se_loader.log", myGames / L"F4SE" / L"f4se_loader.log");
			{
				// Zip entry names are not scrubbed text: keep a logger's name only when it is just a date
				// (crash-2026-11-15-10-42-18.log); anything else (a character or user name in it) becomes crash-N.log.
				int n = 0;
				for (const auto& c : CrashLogs()) {
					++n;
					auto name = Lower(Narrow(c.filename().wstring()));
					const auto stem = std::string_view(name).substr(6, name.size() - 10);  // between "crash-" and ".log"
					if (stem.empty() || !std::ranges::all_of(stem, [](char ch) { return (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'; }))
						name = std::format("crash-{}.log", n);
					addFile("crash/" + name, c, true);
				}
			}
			addFile("plugins.txt", std::filesystem::path(Widen(EnvU(L"LOCALAPPDATA"))) / L"Fallout4" / L"plugins.txt");
			addFile("Fallout4.ccc", GameDir() / L"Fallout4.ccc");
			add("ini-settings.txt", IniText());
			add("system.txt", SystemText());
			if (!vortex.dlls.empty())
				add("dll-sources.txt", Share::DllSourcesText(vortex));
			{
				// The game moves Papyrus.0.log to Papyrus.1.log when its script engine starts, which is before the main
				// menu: at the main-menu write, the session that had the problems is Papyrus.1.log. Both are summed up.
				std::string papyrus;
				for (const wchar_t* name : { L"Papyrus.0.log", L"Papyrus.1.log" }) {
					const auto file = myGames / L"Logs" / L"Script" / name;
					if (!std::filesystem::exists(file, ec))
						continue;
					const auto when = std::filesystem::last_write_time(file, ec);
					papyrus += std::format("== {} ({}) ==\n{}\n", Narrow(name), !ec && when >= g_started ? "this launch" : "an earlier launch",
						Share::PapyrusSummary(ReadTail(file, 32u << 20)));
				}
				add("papyrus-summary.txt", papyrus);
			}
			{
				std::string crumbs;
				for (const auto* name : { L"breadcrumbs.prev.bin", L"breadcrumbs.bin" }) {
					const auto bin = ReadTail(out / name, 1u << 20);
					if (!bin.empty())
						crumbs += std::format("== {} ==\n{}\n", Narrow(name), Share::BreadcrumbsText(bin));
				}
				add("breadcrumbs.txt", crumbs);
			}
			addFile("popup_disabled.txt", out / L"popup_disabled.txt");
			addFile("Vitals.jsonl", out / L"Vitals.jsonl");
			addFile("Vitals.prev.jsonl", out / L"Vitals.prev.jsonl");

			files.emplace_back("scrub-report.txt", Scrub::Report(counts));

			const auto now = std::chrono::system_clock::now();
			const auto today = std::chrono::year_month_day{ std::chrono::floor<std::chrono::days>(now) };
			std::vector<std::string> list;
			for (const auto& [name, text] : files)
				list.push_back(Json().Str("name", name).UNum("bytes", text.size()).Str("crc32", std::format("{:08x}", Zip::Crc32(text))).Build());
			std::vector<std::string> left;
			for (const auto& n : leftOut)
				left.push_back(Json::Quote(n));
			const auto manifest = Json()
			                          .Str("format", Share::kPackFormat)
			                          .Str("vitals_version", F4SE::GetPluginVersion().string())
			                          .Str("game_version", REX::FModule::GetExecutingModule().GetFileVersion().string())
			                          .Str("runtime", RuntimeName())
			                          .Str("f4se_version", F4SE::GetF4SEVersion().string())
			                          .Str("install_id", InstallId())
			                          .Str("date", std::format("{:%Y-%m-%d}", std::chrono::sys_days{ today }))
			                          .Str("written_at", a_when)
			                          .Raw("files", Json::Array(list))
			                          .Raw("left_out_for_size", Json::Array(left))
			                          .Build();

			Zip::Writer zip(Zip::DosTime::From(static_cast<int>(today.year()), static_cast<int>(static_cast<unsigned>(today.month())),
				static_cast<int>(static_cast<unsigned>(today.day()))));
			Scrub::Counts manifestCounts;  // versions, file names and the install id; scrubbed like every other file
			zip.Add("manifest.json", Scrub::Text(manifest, ctx, manifestCounts) + "\n");
			for (const auto& [name, text] : files)
				zip.Add(name, text);
			const auto bytes = zip.Finish();

			const auto tmp = out / L"Vitals-Share.zip.tmp";
			{
				std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
				f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				if (!f)
					return;
			}
			const auto fallback = out / L"Vitals-Share-new.zip";
			std::filesystem::rename(tmp, PackFile(), ec);
			if (ec) {
				// Vitals-Share.zip is open in another program (Explorer's zip view, Discord's upload): keep this launch's
				// pack under another name and say so, rather than leave last launch's pack looking current.
				std::error_code ec2;
				std::filesystem::rename(tmp, fallback, ec2);
				if (ec2)
					std::filesystem::remove(tmp, ec2);
				Summary(std::format("Vitals: could not replace Vitals-Share.zip ({}; is it open in another program?). {}", ec.message(),
					ec2 ? "This launch's pack was not saved." : "This launch's pack is Vitals-Share-new.zip."));
				Emit("error", Json().Str("step", "share_pack").Str("what", "rename: " + ec.message()).Bool("saved_as_new", !ec2));
				return;
			}
			std::filesystem::remove(fallback, ec);
			std::size_t scrubbed = 0;
			for (const auto& [k, n] : counts)
				scrubbed += n;
			Emit("share_pack", Json().Str("when", a_when).UNum("files", files.size() + 1).UNum("bytes", bytes.size()).UNum("scrubbed", scrubbed).UNum("left_out", leftOut.size()));
		}
	}

	bool SharePackEnabled()
	{
		const auto setting = GetSettings().sharePack;
		if (setting >= 0)
			return setting == 1;
		// The installer writes the marker for both answers: "share=1" for Yes, "share=0" for No thanks (so a reinstall
		// that picks No replaces an earlier Yes). Only share=1 turns the pack on.
		std::ifstream in(MarkerFile());
		std::string   line;
		for (int i = 0; i < 20 && std::getline(in, line); ++i) {
			std::erase_if(line, [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
			if (Lower(line) == "share=1")
				return true;
		}
		return false;
	}

	void WriteSharePack(std::string_view a_when)
	{
		if (!SharePackEnabled()) {
			// opted out: remove what an earlier opt-in left behind
			std::scoped_lock l(g_shareLock);
			std::error_code  ec;
			std::filesystem::remove(PackFile(), ec);
			std::filesystem::remove(OutputDir() / L"Vitals-Share-new.zip", ec);
			std::filesystem::remove(IdFile(), ec);
			return;
		}
		std::thread([when = std::string(a_when)] { Guarded("share_pack", [&] { Write(when); }); }).detach();
	}

	std::string ShareReportText()
	{
		if (!SharePackEnabled())
			return {};
		return "\nHOW TO HELP (you chose to help improve Vitals)\n"
		       "  Vitals writes a log pack with your personal details removed, Vitals-Share.zip, a few seconds after this report\n"
		       "  (while the start-up box is open) and again when the main menu is up. Vitals never sends it for you. To share it:\n"
		       "    1. Join https://discord.gg/XghtBZUFNW and press the Upload button.\n"
		       "    2. Drag Vitals-Share.zip (it is in this same folder) into the private channel that opens.\n"
		       "  Only the Vitals maintainer can see that channel. Open the zip first if you want to see what is in it.\n"
		       "  No Discord? Attach the zip to an issue at https://github.com/trvxoo-glitch/defib-vitals-public/issues instead.\n"
		       "  If Vitals-Share.zip was open in another program, this launch's pack is Vitals-Share-new.zip instead.\n"
		       "  Stop making the pack: set bSharePack=0 in Vitals.ini, or delete Data\\F4SE\\Plugins\\Vitals\\share-optin.txt.\n";
	}
}
