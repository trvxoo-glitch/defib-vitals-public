#include "Vitals.h"
#include "Vitals/Rules.h"
#include "Vitals/Scrub.h"
#include "Vitals/Share.h"
#include "Vitals/Zip.h"

#include <intrin.h>
#include <random>
#include <sstream>

// Share pack (opt-in, off unless the player chose it in the installer or set bSharePack=1).
//
// Writes Documents\My Games\Fallout4\F4SE\Vitals\Vitals-Share.zip: a fixed list of files that explain why mods fail,
// each one scrubbed of personal details (include/Vitals/Scrub.h), plus summaries in place of raw files where a raw file
// would carry more than needed (include/Vitals/Share.h). The full list and the reasons are in docs/PRIVACY_AND_SAFETY.md.
//
// Sending (opted-in players only, bShareSend=0 turns it off): the finished zip is sent over HTTPS to the Vitals share
// server (Share::kUploadHost) on the same background thread, the first time, when it holds a crash log not sent
// before, or once a week. The server's code (it deletes the pack) goes into share-sent.txt and the report. WinHTTP is
// loaded only when sending, from Windows' own winhttp.dll; nothing else in Vitals touches the network.
//
// Written twice per launch, on a background thread so the game never waits: at PostPostLoad (so the previous
// session's log and a crash log from it are packed even if this launch never reaches the menu) and at the main menu
// (with this session's address check).

// kernel32 functions CommonLib's REX::W32 does not declare.
extern "C" __declspec(dllimport) int __stdcall GlobalMemoryStatusEx(void* a_status);

namespace Vitals
{
	namespace
	{
		std::mutex g_shareLock;

		std::filesystem::path MarkerFile() { return PluginDir() / L"Vitals" / L"share-optin.txt"; }
		std::filesystem::path PackFile() { return OutputDir() / L"Vitals-Share.zip"; }
		std::filesystem::path IdFile() { return OutputDir() / L"share-id.txt"; }
		std::filesystem::path SentFile() { return OutputDir() / L"share-sent.txt"; }

		std::int64_t UnixNow()
		{
			return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		}

		Share::SendState ReadSendState()
		{
			std::ifstream     in(SentFile(), std::ios::binary);
			std::stringstream ss;
			if (in)
				ss << in.rdbuf();
			return Share::ParseSendState(ss.str());
		}

		void SaveSendState(const Share::SendState& a_st)
		{
			std::ofstream(SentFile(), std::ios::binary | std::ios::trunc) << Share::FormatSendState(a_st);
		}

		// POST the zip to the share server. Returns the HTTP status (0 = no answer) and fills a_reply with the body.
		std::uint32_t Post(const std::string& a_zip, std::string& a_reply)
		{
			using HINTERNET = void*;
			using Open_t = HINTERNET(__stdcall*)(const wchar_t*, std::uint32_t, const wchar_t*, const wchar_t*, std::uint32_t);
			using Timeouts_t = int(__stdcall*)(HINTERNET, int, int, int, int);
			using Connect_t = HINTERNET(__stdcall*)(HINTERNET, const wchar_t*, std::uint16_t, std::uint32_t);
			using Request_t = HINTERNET(__stdcall*)(HINTERNET, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t**, std::uint32_t);
			using Send_t = int(__stdcall*)(HINTERNET, const wchar_t*, std::uint32_t, void*, std::uint32_t, std::uint32_t, std::uintptr_t);
			using Receive_t = int(__stdcall*)(HINTERNET, void*);
			using Query_t = int(__stdcall*)(HINTERNET, std::uint32_t, const wchar_t*, void*, std::uint32_t*, std::uint32_t*);
			using Read_t = int(__stdcall*)(HINTERNET, void*, std::uint32_t, std::uint32_t*);
			using Close_t = int(__stdcall*)(HINTERNET);

			const auto lib = REX::W32::LoadLibraryW(L"winhttp.dll");
			if (!lib)
				return 0;
			auto fn = [&](const char* a_name) { return REX::W32::GetProcAddress(lib, a_name); };
			const auto open = reinterpret_cast<Open_t>(fn("WinHttpOpen"));
			const auto timeouts = reinterpret_cast<Timeouts_t>(fn("WinHttpSetTimeouts"));
			const auto connect = reinterpret_cast<Connect_t>(fn("WinHttpConnect"));
			const auto request = reinterpret_cast<Request_t>(fn("WinHttpOpenRequest"));
			const auto send = reinterpret_cast<Send_t>(fn("WinHttpSendRequest"));
			const auto receive = reinterpret_cast<Receive_t>(fn("WinHttpReceiveResponse"));
			const auto query = reinterpret_cast<Query_t>(fn("WinHttpQueryHeaders"));
			const auto read = reinterpret_cast<Read_t>(fn("WinHttpReadData"));
			const auto close = reinterpret_cast<Close_t>(fn("WinHttpCloseHandle"));
			if (!open || !timeouts || !connect || !request || !send || !receive || !query || !read || !close)
				return 0;

			constexpr std::uint32_t kAutoProxy = 4, kDefaultProxy = 0, kSecure = 0x00800000;
			constexpr std::uint32_t kStatusCode = 19, kFlagNumber = 0x20000000;
			// the user agent says only "Vitals" and its version
			const auto agent = L"Vitals/" + Widen(F4SE::GetPluginVersion().string());
			HINTERNET  session = open(agent.c_str(), kAutoProxy, nullptr, nullptr, 0);
			if (!session)
				session = open(agent.c_str(), kDefaultProxy, nullptr, nullptr, 0);  // Windows before 8.1
			if (!session)
				return 0;
			std::uint32_t status = 0;
			timeouts(session, 15000, 15000, 60000, 30000);
			const std::wstring host(Share::kUploadHost), path(Share::kUploadPath);
			if (const auto conn = connect(session, host.c_str(), 443, 0)) {
				if (const auto req = request(conn, L"POST", path.c_str(), nullptr, nullptr, nullptr, kSecure)) {
					const wchar_t* headers = L"Content-Type: application/zip\r\n";
					if (send(req, headers, static_cast<std::uint32_t>(-1), const_cast<char*>(a_zip.data()), static_cast<std::uint32_t>(a_zip.size()),
							static_cast<std::uint32_t>(a_zip.size()), 0) &&
						receive(req, nullptr)) {
						std::uint32_t len = sizeof(status);
						query(req, kStatusCode | kFlagNumber, nullptr, &status, &len, nullptr);
						char          buf[4096];
						std::uint32_t got = 0;
						while (a_reply.size() < 65536 && read(req, buf, sizeof(buf), &got) && got > 0)
							a_reply.append(buf, got);
					}
					close(req);
				}
				close(conn);
			}
			close(session);
			return status;
		}

		// Sends the pack when SendReason says so. Runs on the share thread, under g_shareLock.
		void MaybeSend(std::string_view a_when, const std::string& a_zip, const std::vector<std::string>& a_crashes)
		{
			if (Share::kUploadHost.empty() || !GetSettings().shareSend)
				return;
			auto       st = ReadSendState();
			const auto now = UnixNow();
			const auto reason = Share::SendReason(st, now, a_crashes);
			// at start only a new crash is worth sending now; everything else waits for the fuller main-menu pack
			if (reason.empty() || (a_when == "start" && reason != "new_crash"))
				return;
			std::string reply;
			const auto  status = Post(a_zip, reply);
			const auto  code = status == 200 ? Share::ReplyCode(reply) : std::string{};
			if (code.empty()) {
				st.lastTry = now;
				SaveSendState(st);
				Emit("share_upload", Json().Str("result", "failed").Str("reason", reason).UNum("http_status", status));
				return;
			}
			st.lastSent = now;
			st.lastTry = 0;
			st.code = code;
			for (const auto& c : a_crashes)
				if (std::ranges::find(st.crashes, c) == st.crashes.end())
					st.crashes.push_back(c);
			if (st.crashes.size() > 50)
				st.crashes.erase(st.crashes.begin(), st.crashes.end() - 50);
			SaveSendState(st);
			Emit("share_upload", Json().Str("result", "sent").Str("reason", reason).Str("code", code).UNum("bytes", a_zip.size()));
		}
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
			if (from > 0)
				return Share::KeepTail("\n" + data, a_max);  // the leading newline lets KeepTail cut at a line start
			return data;
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
					wchar_t                  buf[512]{};
					const auto               section = Widen(k.section), key = Widen(k.key);
					const auto               n = REX::W32::GetPrivateProfileStringW(section.c_str(), key.c_str(), kMissing, buf, 512, path.c_str());
					const std::wstring       v(buf, n);
					if (v != kMissing)
						out += std::format("  [{}] {}={}\n", k.section, k.key, Narrow(v));
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
			for (const auto& e : std::filesystem::directory_iterator(MyGames() / L"F4SE", ec)) {
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

		std::vector<std::filesystem::path> HistoryFiles()
		{
			std::vector<std::filesystem::path> files;
			std::error_code                    ec;
			for (const auto& e : std::filesystem::directory_iterator(OutputDir() / L"history", ec))
				if (e.path().extension() == L".jsonl")
					files.push_back(e.path());
			std::ranges::sort(files, std::greater<>{});  // names sort by date: newest first
			if (files.size() > 3)
				files.resize(3);
			return files;
		}

		void Write(std::string_view a_when)
		{
			std::scoped_lock l(g_shareLock);
			std::error_code  ec;
			const auto       out = OutputDir();
			const auto       myGames = MyGames();

			// Vortex: which mod folder deployed each DLL, and the staging folder to shorten.
			Share::VortexDlls vortex;
			if (const auto manifest = ReadTail(GameDir() / L"Data" / L"vortex.deployment.json", 64u << 20); !manifest.empty())
				vortex = Share::ParseVortexManifest(manifest);

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
			auto add = [&](std::string a_name, std::string a_text, bool a_crash = false) {
				if (a_text.empty())
					return;
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
				add(std::move(a_name), ReadTail(a_path, Share::kMaxFileBytes), a_crash);
			};

			// most useful first, so the size cap drops old history before the report
			addFile("Vitals-Report.txt", out / L"Vitals-Report.txt");
			addFile("Vitals-summary.txt", out / L"Vitals-summary.txt");
			addFile("status.json", out / L"status.json");
			addFile("f4se.log", myGames / L"F4SE" / L"f4se.log");
			addFile("f4se_loader.log", myGames / L"F4SE" / L"f4se_loader.log");
			for (const auto& c : CrashLogs())
				addFile("crash/" + Narrow(c.filename().wstring()), c, true);
			addFile("plugins.txt", std::filesystem::path(Widen(EnvU(L"LOCALAPPDATA"))) / L"Fallout4" / L"plugins.txt");
			addFile("Fallout4.ccc", GameDir() / L"Fallout4.ccc");
			add("ini-settings.txt", IniText());
			add("system.txt", SystemText());
			if (!vortex.dlls.empty())
				add("dll-sources.txt", Share::DllSourcesText(vortex));
			if (const auto papyrus = myGames / L"Logs" / L"Script" / L"Papyrus.0.log"; std::filesystem::exists(papyrus, ec))
				add("papyrus-summary.txt", Share::PapyrusSummary(ReadTail(papyrus, 32u << 20)));
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
			for (const auto& h : HistoryFiles())
				addFile("history/" + Narrow(h.filename().wstring()), h);

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
			zip.Add("manifest.json", manifest + "\n");
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
			std::filesystem::rename(tmp, PackFile(), ec);
			if (ec) {
				std::filesystem::remove(tmp, ec);
				return;
			}
			std::size_t scrubbed = 0;
			for (const auto& [k, n] : counts)
				scrubbed += n;
			Emit("share_pack", Json().Str("when", a_when).UNum("files", files.size() + 1).UNum("bytes", bytes.size()).UNum("scrubbed", scrubbed).UNum("left_out", leftOut.size()));

			std::vector<std::string> crashes;
			for (const auto& [name, text] : files)
				if (name.starts_with("crash/"))
					crashes.push_back(name.substr(6));
			MaybeSend(a_when, bytes, crashes);
		}
	}

	bool SharePackEnabled()
	{
		const auto setting = GetSettings().sharePack;
		if (setting >= 0)
			return setting == 1;
		std::error_code ec;
		return std::filesystem::exists(MarkerFile(), ec);
	}

	void WriteSharePack(std::string_view a_when)
	{
		if (!SharePackEnabled()) {
			// opted out: remove what an earlier opt-in left behind
			std::scoped_lock l(g_shareLock);
			std::error_code  ec;
			std::filesystem::remove(PackFile(), ec);
			std::filesystem::remove(IdFile(), ec);
			return;
		}
		std::thread([when = std::string(a_when)] { Guarded("share_pack", [&] { Write(when); }); }).detach();
	}

	std::string ShareReportText()
	{
		if (!SharePackEnabled())
			return {};
		std::string out = "\nSHARE PACK (you chose to help improve Vitals)\n"
		                  "  Vitals-Share.zip in this folder holds this report, F4SE's logs, your load order and recent crash logs, with your\n"
		                  "  user name, PC name, emails, IP addresses, keys and private folder paths removed (scrub-report.txt inside says how many).\n";
		if (Share::kUploadHost.empty()) {
			out += "  Sending is not set up in this version yet, so nothing is sent. If you want to share it, attach it to a GitHub issue at\n"
			       "  https://github.com/trvxoo-glitch/defib-vitals-public/issues or link it in a bug report on the Nexus page.\n";
		} else if (!GetSettings().shareSend) {
			out += "  Sending is off (bShareSend=0), so the zip stays on this PC.\n";
		} else {
			out += std::format("  Vitals sends it to https://{} the first time, after a new crash, and at most weekly otherwise.\n", Narrow(std::wstring(Share::kUploadHost)));
			if (const auto st = ReadSendState(); !st.code.empty())
				out += std::format("  Last pack sent: {} (enter it at https://{} to delete it; packs are deleted after 90 days anyway).\n", st.code,
					Narrow(std::wstring(Share::kUploadHost)));
			out += "  Keep the zip but stop sending: set bShareSend=0 in Vitals.ini.\n";
		}
		out += "  Turn it all off: set bSharePack=0 in Vitals.ini, or delete Data\\F4SE\\Plugins\\Vitals\\share-optin.txt.\n";
		return out;
	}
}
