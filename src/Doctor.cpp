#include "Vitals.h"
#include "Vitals/Formats.h"
#include "Vitals/Rules.h"
#include "Vitals/Share.h"

#include <sstream>

// Plugin doctor (bDoctor=1, on by default). Runs at kPostPostLoad: after every F4SE plugin had its chance to
// load, and BEFORE the game reads its plugins (.esm/.esp/.esl), so it can warn about a load order that is
// about to crash the game. Everything here only reads files; nothing in the game is touched.
//
//  1. F4SE DLLs that are not running: read each file's exports and F4SEPlugin_Version data and say why F4SE
//     skipped it, in plain words (built for another game version, Old-Gen only, not a plugin at all...),
//     plus the matching line from f4se.log.
//  2. Data files a DLL needs (for example CommonLibF4RD's f4rd-runtime.bin) that are missing.
//  3. Load order from plugins.txt: masters that are missing, installed but not active, or loading after the
//     plugin that needs them; plugins listed as active whose file is gone; full/light plugin counts.
//  4. Rules from Data\F4SE\Plugins\Vitals\knowledge\*.txt: known dead DLLs, known conflicts, old loose files
//     that are known to break menus. The rules are data, so the census can grow them without a new DLL.
//
// Severities: "problem" = may crash the game (missing, inactive or late masters, a missing or unusable Address
// Library, rules seen crashing in game); "warning" = a mod that will not work (an F4SE DLL that is not running, a loose-file setup);
// "info" = good to know.
//
// Output: Vitals-Report.txt (plain language, for players), "doctor_issue" lines in Vitals.jsonl, and one Windows
// message box when something new needs attention (iPopupMode: 0 never, 1 or 2 on every launch while there is something to list; only "Don't show" removes plugins from it).
// The loader rules and the popup decision live in include/Vitals/Rules.h, where tests/cpp/rules_test.cpp checks them.

namespace Vitals
{
	namespace
	{
		struct Issue
		{
			std::string severity;  // problem | warning | info
			std::string category;  // plugin_dll | data_file | load_order | known_issue | conflict | loose_file | ini_setup
			std::string subject;
			std::string message;
			std::string advice;
			std::string group;     // optional: lets the message box list similar problems together
			std::string reason;    // plugin_dll: the shared loader reason code (Rules::Loader::Why)
		};

		void*                    g_hook{ nullptr };  // the CBT hook used to relabel the message box buttons
		void*                    g_box{ nullptr };   // the message box window while it is open (clipboard owner)
		std::mutex               g_lock;
		std::vector<Issue>       g_issues;
		std::vector<std::string> g_info;       // short facts for the report header
		std::vector<std::string> g_addrNotes;  // added at game-data-ready

		Issue& Add(std::string a_sev, std::string a_cat, std::string a_subject, std::string a_msg, std::string a_advice = {}, std::string a_group = {})
		{
			return g_issues.emplace_back(Issue{ std::move(a_sev), std::move(a_cat), std::move(a_subject), std::move(a_msg), std::move(a_advice), std::move(a_group), {} });
		}

		using Rules::Split;
		using Rules::Trim;
		namespace Loader = Rules::Loader;

		std::vector<std::uint8_t> ReadFile(const std::filesystem::path& a_file, std::size_t a_max)
		{
			std::vector<std::uint8_t> data;
			std::ifstream             in(a_file, std::ios::binary);
			if (!in)
				return data;
			std::error_code ec;
			const auto      size = std::filesystem::file_size(a_file, ec);
			data.resize(static_cast<std::size_t>(std::min<std::uintmax_t>(ec ? 0 : size, a_max)));
			in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
			data.resize(static_cast<std::size_t>(in.gcount()));
			return data;
		}

		bool Contains(const std::vector<std::uint8_t>& a_hay, std::string_view a_needle)
		{
			if (a_needle.empty() || a_hay.size() < a_needle.size())
				return false;
			return std::search(a_hay.begin(), a_hay.end(), a_needle.begin(), a_needle.end(),
			           [](std::uint8_t a, char b) { return std::tolower(a) == std::tolower(static_cast<unsigned char>(b)); }) != a_hay.end();
		}

		std::string VersionText(std::uint32_t a_packed)
		{
			return std::format("{}.{}.{}", (a_packed >> 24) & 0xFF, (a_packed >> 16) & 0xFF, (a_packed >> 4) & 0xFFF);
		}

		// ---------------- PE reading (file on disk, never loaded): include/Vitals/Formats.h ----------------
		using Formats::Get;
		using Formats::ParsePe;
		using Formats::PeInfo;
		using Formats::RvaToOff;

		std::optional<Loader::VersionData> ReadVersionData(const std::vector<std::uint8_t>& d, const PeInfo& a_pe)
		{
			const auto raw = Formats::ReadVersionDataRaw(d, a_pe);
			return raw ? std::optional{ Formats::ToVersionData(*raw) } : std::nullopt;
		}

		// Second, independent check for "is this plugin outdated": when was the DLL built, compared with the
		// Next-Gen update (2024-04-25) that introduced F4SEPlugin_Version. The first check (F4SE cannot load it) says
		// the plugin is unusable now; this one says whether it is old or just lacks the new data.
		struct Age
		{
			enum class Kind { unknown, before, after } kind{ Kind::unknown };
			std::string text;   // sentence for the report
			std::string group;  // pre | post | unknown
		};

		Age PluginAge(const PeInfo& a_pe, const std::filesystem::path& a_path)
		{
			using namespace std::chrono;
			const auto nextGen = sys_days{ year{ 2024 } / April / 25 };
			Age        a;
			bool       weak = false;
			sys_days   day{};
			const auto now = system_clock::now();
			const auto built = sys_seconds{ seconds{ a_pe.timestamp } };
			if (a_pe.timestamp && !a_pe.reproducible && built >= sys_days{ year{ 2010 } / January / 1 } && built <= now + hours{ 24 }) {
				day = floor<days>(built);
			} else {
				std::error_code ec;
				const auto      t = std::filesystem::last_write_time(a_path, ec);
				if (ec) {
					a.group = "unknown";
					return a;
				}
				const auto s = FileTimeToSys(t);
				if (!s) {
					a.group = "unknown";
					return a;
				}
				day = floor<days>(*s);
				weak = true;  // file date: archives and mod managers can change it
			}
			const auto when = std::format("{:%Y-%m-%d}", day);
			const char* what = weak ? "Its file is dated " : "Built (date inside the DLL) ";
			if (day < nextGen) {
				a.kind = Age::Kind::before;
				a.group = "pre";
				a.text = std::format(" {}{}, before the April 2024 Next-Gen update, so {} made for Old-Gen 1.10.163 or earlier. If it is already the newest file on its mod page, its author has not released a version for your game yet.", what, when,
					weak ? "it was probably" : "it was most likely");
			} else {
				a.kind = Age::Kind::after;
				a.group = "post";
				a.text = std::format(" {}{}, after the Next-Gen update, so it may still be maintained and only lack the new version data. If it is already the newest file on its mod page, the author has not added Next-Gen support yet.", what, when);
			}
			return a;
		}

		// Why F4SE is not running a DLL, in plain words. The decision itself is Rules::Loader::Check, the same code
		// tools/f4se_dll_scan.py is tested against (tests/loader_cases.txt). f4se.log, when it names the DLL, says
		// what happened to a DLL the rules would accept.
		struct Explanation
		{
			Loader::Verdict verdict;
			std::string     text;
			std::string     advice;
		};

		constexpr auto kDefaultAdvice = "Look for a version of this mod for your game version, or a replacement. Until then its features (and mods that need it) will not work.";

		// What F4SE checks before it loads a plugin that sets the Address Library flag: only that the file exists.
		bool AddressLibraryPresent() { return AddressLibrary().present; }

		Explanation Explain(const PeInfo& a_pe, const std::optional<Loader::VersionData>& a_vd, const std::string& a_f4seSays)
		{
			Loader::Input in;
			const int     rt = RuntimeIndex();
			in.era = rt == 0 ? Loader::Era::OG : rt == 1 ? Loader::Era::NG : Loader::Era::AE;
			in.runtime = REX::FModule::GetExecutingModule().GetFileVersion().pack() & ~0xFu;
			in.f4se = F4SE::GetF4SEVersion().pack() & ~0xFu;
			in.hasAnyExport = std::ranges::any_of(a_pe.exports, [](const auto& e) { return e.first.starts_with("F4SEPlugin_"); });
			in.hasQuery = a_pe.exports.contains("F4SEPlugin_Query");
			in.vd = a_vd;
			in.addressLibraryPresent = AddressLibraryPresent();
			Explanation e{ Loader::Check(in), {}, kDefaultAdvice };
			const auto  log = Lower(a_f4seSays);
			std::string list;
			if (a_vd)
				for (const auto c : a_vd->compat)
					list += (list.empty() ? "" : ", ") + VersionText(c);
			using W = Loader::Why;
			switch (e.verdict.why) {
			case W::not_plugin:
				e.text = "it is not an F4SE plugin (no F4SEPlugin_ exports). It may be a helper library another DLL loads; if nothing needs it, it does not belong in this folder";
				e.advice.clear();
				break;
			case W::og_no_query:
				e.text = "it was built for Next-Gen/Anniversary only (no F4SEPlugin_Query, which the Old-Gen F4SE 0.6.x needs)";
				e.advice = "Look for an Old-Gen (1.10.163) version of this mod. Until then its features will not work.";
				break;
			case W::og_query_decides:
				e.text = "F4SE asked it (F4SEPlugin_Query) and it declined this game version, or it failed while loading";
				break;
			case W::no_version_data_query_only:
				e.text = "it is an older-style plugin (Query only) without F4SEPlugin_Version data, which F4SE 0.7.x needs, so F4SE skips it";
				break;
			case W::no_version_data:
				e.text = "it has no F4SEPlugin_Version data, which F4SE 0.7.x needs";
				break;
			case W::bad_version_data:
				e.text = "its F4SEPlugin_Version data is damaged (dataVersion 0), so F4SE refuses it";
				e.advice = "Reinstall the mod. If it still happens, the file itself is broken: tell the mod's author.";
				break;
			case W::no_name:
				e.text = "its F4SEPlugin_Version data has no plugin name, so F4SE refuses it";
				e.advice = "Reinstall the mod. If it still happens, tell the mod's author.";
				break;
			case W::address_library_missing:
				e.text = "it uses the Address Library, and the Address Library file for this game version is missing";
				e.advice = "Install Address Library for F4SE Plugins (Nexus 47327), the file for your exact game version.";
				break;
			case W::ng_flags_only:
				e.text = std::format("it was built for Next-Gen 1.10.980/984 only (F4SE 0.7.5+ needs the Anniversary flags){}", list.empty() ? ""s : " (lists " + list + ")");
				e.advice = "Look for an Anniversary (1.11.x) build of this mod, or a port. Until then its features will not work.";
				break;
			case W::other_builds:
				e.text = std::format("it was built for other game versions only: {}", list);
				e.advice = "Look for a version of this mod for your game version. Until then its features will not work.";
				break;
			case W::not_declared:
				e.text = "its version data does not declare support for this game version";
				break;
			case W::needs_newer_f4se:
				e.text = std::format("it needs F4SE {} or newer, and this is F4SE {}", a_vd ? VersionText(a_vd->f4seRequired) : "?"s, F4SE::GetF4SEVersion().string());
				e.advice = "Update F4SE (Script Extender) for your game version.";
				break;
			case W::independent:
			case W::listed:
				if (log.find("reported as incompatible during load") != std::string::npos) {
					e.text = "F4SE accepted its version data, but its own start-up code declined to run (usually it does not support this exact game version, or something it needs is missing)";
					e.advice = "Check the mod page for requirements and for a version for your game version.";
				} else if (log.find("fatal error") != std::string::npos) {
					e.text = "it crashed while loading and F4SE caught the error, so it was switched off";
					e.advice = "Check the mod's requirements and reinstall it; if that does not help, report it to the author with f4se.log.";
				} else {
					e.text = "it declares support for this game version, so F4SE should have loaded it. Check f4se.log, and whether another DLL it needs is missing";
				}
				break;
			}
			return e;
		}

		// f4se.log lines that mention a DLL (F4SE explains its own decisions there)
		std::vector<std::string> F4seLogLines()
		{
			std::vector<std::string> lines;
			std::ifstream            in(OutputDir().parent_path() / L"f4se.log");
			std::string              line;
			while (std::getline(in, line))
				lines.push_back(Trim(line));
			return lines;
		}

		std::string F4seSays(const std::vector<std::string>& a_log, const std::string& a_dll)
		{
			const auto key = Lower(a_dll);
			std::string best;
			for (const auto& l : a_log) {
				const auto low = Lower(l);
				if (low.find(key) == std::string::npos)
					continue;
				if (low.find("disabled") != std::string::npos || low.find("incompatible") != std::string::npos ||
					low.find("couldn't") != std::string::npos || low.find("could not") != std::string::npos ||
					low.find("failed") != std::string::npos || low.find("reported as") != std::string::npos ||
					low.find("not loaded") != std::string::npos)
					best = l;
			}
			return best;
		}

		// ---------------- knowledge rules ----------------
		struct Rule
		{
			std::string kind, match, extra, severity, message, source;
		};

		// Every .txt in Vitals\knowledge, in name order. The same rule (kind, match, extra) in two files is used once,
		// so a rule copied into defib-known.txt and defib-census.txt is not reported twice.
		std::vector<Rule> LoadRules()
		{
			std::vector<Rule>                   rules;
			std::error_code                    ec;
			std::vector<std::filesystem::path> files;
			for (const auto& e : std::filesystem::directory_iterator(PluginDir() / L"Vitals" / L"knowledge", ec))
				if (e.path().extension() == L".txt")
					files.push_back(e.path());
			std::ranges::sort(files);
			std::set<std::string> seen;
			for (const auto& path : files) {
				std::ifstream in(path);
				std::string   line;
				while (std::getline(in, line)) {
					line = Trim(line);
					if (line.empty() || line[0] == '#' || line[0] == ';')
						continue;
					auto f = Split(line, '|');
					if (f.size() < 5)
						continue;
					// a message may itself contain '|': join the rest
					for (std::size_t i = 5; i < f.size(); ++i)
						f[4] += " | " + f[i];
					// "oldfile@NG,AE": the rule applies only to those runtimes (Rules::RuleRuntimes)
					std::string kind;
					if (!Rules::RuleRuntimes(f[0], RuntimeName(), kind))
						continue;
					if (!seen.insert(Lower(f[0]) + "|" + Lower(f[1]) + "|" + Lower(f[2])).second)
						continue;
					// Only three severities exist. Anything else would be counted nowhere and never shown, which would
					// silently switch a crash rule off: use "warning" and say so.
					auto sev = Lower(f[3]);
					if (sev == "warn")
						sev = "warning";
					if (sev != "problem" && sev != "warning" && sev != "info") {
						Summary(std::format("Knowledge rule with unknown severity \"{}\" in {} (used as warning): {}", f[3], Narrow(path.filename().wstring()), line));
						sev = "warning";
					}
					rules.push_back({ kind, f[1], f[2], sev, f[4], Narrow(path.filename().wstring()) });
				}
			}
			return rules;
		}

		bool EnbInstalled()
		{
			std::error_code ec;
			const auto      g = GameDir();
			const bool      cfg = std::filesystem::exists(g / L"enbseries.ini", ec) || std::filesystem::exists(g / L"enblocal.ini", ec);
			const bool      dll = std::filesystem::exists(g / L"d3d11.dll", ec) || std::filesystem::exists(g / L"dxgi.dll", ec);
			return cfg && dll;
		}

		bool GameFileExists(const std::string& a_rel)
		{
			if (a_rel == "{ENB}")
				return EnbInstalled();
			if (a_rel == "{ADDRESS_LIBRARY}")
				return AddressLibraryPresent();
			std::error_code ec;
			return std::filesystem::exists(GameDir() / std::filesystem::path(Widen(a_rel)), ec);
		}

		std::optional<std::chrono::sys_days> ParseDate(const std::string& a_text)
		{
			const auto f = Split(a_text, '-');
			int        y = 0;
			unsigned   m = 0, d = 0;
			if (f.size() != 3 || std::from_chars(f[0].data(), f[0].data() + f[0].size(), y).ec != std::errc{} ||
				std::from_chars(f[1].data(), f[1].data() + f[1].size(), m).ec != std::errc{} ||
				std::from_chars(f[2].data(), f[2].data() + f[2].size(), d).ec != std::errc{})
				return std::nullopt;
			const std::chrono::year_month_day ymd{ std::chrono::year{ y }, std::chrono::month{ m }, std::chrono::day{ d } };
			if (!ymd.ok())
				return std::nullopt;
			return std::chrono::sys_days{ ymd };
		}

		// Which Vortex mod deployed a DLL (Data\vortex.deployment.json), read once, only when a DLL is not running.
		std::string VortexSource(const std::string& a_dll)
		{
			static const Share::VortexDlls v = [] {
				std::ifstream     in(GameDir() / L"Data" / L"vortex.deployment.json", std::ios::binary);
				std::stringstream ss;
				if (in)
					ss << in.rdbuf();
				return Share::ParseVortexManifest(ss.str());
			}();
			for (const auto& [dll, src] : v.dlls)
				if (Lower(dll) == Lower(a_dll))
					return src;
			return {};
		}

		// ---------------- 1 + 2: DLLs ----------------
		void CheckDlls(const std::vector<Rule>& a_rules)
		{
			const auto      log = F4seLogLines();
			std::error_code ec;
			std::size_t     total = 0, running = 0, refused = 0;
			std::map<std::size_t, std::vector<std::string>> needs;  // index of a 'needs' rule -> DLLs that contain its text
			const bool anyNeeds = std::ranges::any_of(a_rules, [](const Rule& r) { return r.kind == "needs"; });

			for (const auto& entry : std::filesystem::directory_iterator(PluginDir(), ec)) {
				if (!entry.is_regular_file() || Lower(Narrow(entry.path().extension().wstring())) != ".dll")
					continue;
				++total;
				const auto file = Narrow(entry.path().filename().wstring());
				const bool inProcess = REX::W32::GetModuleHandleW(entry.path().filename().c_str()) != nullptr;
				running += inProcess;

				// 'dll' rules: one for a DLL that is not running ("refused") becomes the advice of its plugin_dll finding
				// below (it knows this mod: a replacement, the file to use instead); any other is its own finding.
				const Rule* refusedRule = nullptr;
				for (const auto& r : a_rules) {
					if (r.kind != "dll" || Lower(r.match) != Lower(file))
						continue;
					if (r.extra == "refused")
						refusedRule = inProcess ? refusedRule : &r;
					else
						Add(r.severity, "known_issue", file, r.message);
				}
				// The file is read only when something needs its contents: a 'needs' rule, or a DLL that is not running.
				if (!anyNeeds && inProcess)
					continue;
				const auto data = ReadFile(entry.path(), 96u << 20);
				for (std::size_t i = 0; i < a_rules.size(); ++i)
					if (a_rules[i].kind == "needs" && Contains(data, a_rules[i].match))
						needs[i].push_back(file);
				if (inProcess)
					continue;

				++refused;
				const auto f4se = F4seSays(log, file);
				const auto pe = ParsePe(data);
				if (!pe.ok) {
					Add("warning", "plugin_dll", file, std::format("{} is in Data\\F4SE\\Plugins but is not running: it is {}.", file, pe.error),
						"Remove it, or reinstall the mod that provides it.");
					continue;
				}
				auto        e = Explain(pe, ReadVersionData(data, pe), f4se);
				std::string group;
				const auto  why = e.verdict.why;
				if (why == Loader::Why::no_version_data || why == Loader::Why::no_version_data_query_only) {
					// Two checks agree or add detail: F4SE cannot load it (above), and when it was built.
					const auto age = PluginAge(pe, entry.path());
					e.text += "." + age.text;
					group = age.group;
					if (e.text.back() == '.')
						e.text.pop_back();
					if (age.kind == Age::Kind::after)
						e.advice = "Check the mod page for a newer file. If you already have the newest, nothing is wrong with your setup: F4SE 0.7.x skips this build, so the mod does not work on this game version for now.";
					else if (age.kind == Age::Kind::before)
						e.advice = "Check the mod page for a version made for your game version. If this is already the newest file, nothing is wrong with your setup: that mod does not support this game version yet, so look for a replacement. Until then its features (and mods that need it) will not work.";
				}
				const bool notPlugin = why == Loader::Why::not_plugin;
				// What the player needs to tell "old mod" from "old copy": the DLL's own version number to compare with
				// the mod page, and (Vortex) which mod's copy is the one deployed. Another mod bundling an older copy of
				// a shared DLL, and winning the file conflict, looks exactly like an outdated mod otherwise.
				std::string where;
				if (const auto ver = FileVersion(data); !ver.empty())
					where += " File version " + ver + ".";
				if (const auto src = VortexSource(file); !src.empty())
					where += " Installed by the Vortex mod \"" + src + "\".";
				if (!notPlugin && (why == Loader::Why::no_version_data || why == Loader::Why::no_version_data_query_only))
					e.advice += " If you just installed the newest file and still see this, another mod may be overwriting it with an older copy: check which mod provides this DLL in your mod manager (Vortex: the mod's Conflicts; MO2: the Data tab).";
				// Not running is a mod that does not work, not a crash: a warning. Crash risks (masters, the Address
				// Library) are reported as problems by their own checks, or by a rule that says so.
				std::string severity = notPlugin ? "info" : "warning";
				if (refusedRule) {
					e.advice = refusedRule->message;
					if (refusedRule->severity == "problem" || (refusedRule->severity == "warning" && notPlugin))
						severity = refusedRule->severity;
				}
				Add(severity, "plugin_dll", file,
					std::format("{} is in Data\\F4SE\\Plugins but is not running: {}.{}{}", file, e.text, where, f4se.empty() ? ""s : " f4se.log: \"" + f4se + "\""),
					e.advice, group)
					.reason = Loader::Name(why);
			}

			for (const auto& [index, dlls] : needs) {
				const auto& r = a_rules[index];
				if (GameFileExists(r.extra))
					continue;
				std::string list;
				for (const auto& d : dlls)
					list += (list.empty() ? "" : ", ") + d;
				const auto what = r.extra == "{ADDRESS_LIBRARY}" ? "The Address Library file for this game version"s : r.extra;
				Add(r.severity, "data_file", r.extra, std::format("{} is missing, and {} need{} it. {}", what, list, dlls.size() == 1 ? "s" : "", r.message));
			}
			// Most F4SE plugins find game functions through the Address Library (its file name is built at run time,
			// so it cannot be found by scanning the DLLs). Without the file for this exact build they cannot work.
			if (const auto& al = AddressLibrary(); total > 1 && !al.present) {
				Add("problem", "data_file", "Address Library",
					std::format("The Address Library file for this game version (Data\\F4SE\\Plugins\\{}) is missing.", al.file),
					"Install Address Library for F4SE Plugins (Nexus 47327), the file for your exact game version. Most F4SE plugins cannot work without it.");
			} else if (al.present && !al.usable) {
				// F4SE loads plugins when the file merely exists; CommonLib-based ones then show an error and close the game.
				Add("problem", "data_file", "Address Library",
					std::format("The Address Library file for this game version (Data\\F4SE\\Plugins\\{}) is there, but {}. F4SE plugins that use it will fail, and many of them close the game with an error.", al.file, al.problem),
					"Download Address Library for F4SE Plugins (Nexus 47327) again, the file for your exact game version, and replace this file.").reason = al.state;
			}
			g_info.push_back(std::format("F4SE plugins: {} DLLs in Data\\F4SE\\Plugins, {} running, {} not running", total, running, refused));
		}

		// ---------------- 3: load order ----------------
		using Formats::PluginHeader;

		PluginHeader ReadHeader(const std::filesystem::path& a_file)
		{
			const auto d = ReadFile(a_file, 1u << 20);
			return Formats::ParseTes4(d, Lower(Narrow(a_file.extension().wstring())));
		}

		std::filesystem::path PluginsTxt()
		{
			wchar_t buf[512]{};
			const auto n = REX::W32::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, 512);
			if (!n || n >= 512)
				return {};
			return std::filesystem::path(std::wstring(buf, n)) / L"Fallout4" / L"plugins.txt";
		}

		void CheckLoadOrder()
		{
			std::error_code ec;
			const auto      data = GameDir() / L"Data";
			const auto      txt = PluginsTxt();
			std::ifstream   in(txt);
			if (!in) {
				g_info.push_back("Load order: plugins.txt not found, load-order checks skipped");
				return;
			}

			// The game always loads these first when present, then the Creation Club files listed in Fallout4.ccc.
			std::vector<std::string> order;
			for (const char* base : { "Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm",
					 "DLCworkshop03.esm", "DLCNukaWorld.esm", "DLCUltraHighResolution.esm" })
				if (std::filesystem::exists(data / base, ec))
					order.emplace_back(base);
			{
				std::ifstream ccc(GameDir() / L"Fallout4.ccc");
				std::string   l;
				while (std::getline(ccc, l))
					if (l = Rules::PluginNameToUtf8(Trim(l)); !l.empty() && std::filesystem::exists(data / std::filesystem::path(Widen(l)), ec))
						order.push_back(l);
			}
			const std::size_t     implicitCount = order.size();
			std::set<std::string> implicitLower;
			for (const auto& o : order)
				implicitLower.insert(Lower(o));

			std::string line;
			bool        firstLine = true;
			while (std::getline(in, line)) {
				if (firstLine && line.starts_with("\xEF\xBB\xBF"))
					line.erase(0, 3);
				firstLine = false;
				line = Rules::PluginNameToUtf8(Trim(line));  // UTF-8 or the Windows code page, depending on what wrote it
				if (line.empty() || line[0] == '#' || line[0] != '*')
					continue;
				const auto name = line.substr(1);
				if (!implicitLower.contains(Lower(name)))
					order.push_back(name);
			}

			// Read every active plugin's header once.
			struct Entry
			{
				bool         exists{ false };
				PluginHeader h;
			};
			std::vector<Entry> entries(order.size());
			std::vector<bool>  isMaster(order.size());
			std::size_t        missingFiles = 0;
			for (std::size_t i = 0; i < order.size(); ++i) {
				const auto path = data / std::filesystem::path(Widen(order[i]));
				auto&      e = entries[i];
				e.exists = std::filesystem::exists(path, ec);
				if (!e.exists) {
					++missingFiles;
					Add("warning", "load_order", order[i], std::format("{} is active in plugins.txt but the file is not in Data.", order[i]),
						"Re-deploy your mod manager, or deactivate it. Plugins that need it as a master will crash the game.");
					continue;
				}
				e.h = ReadHeader(path);
				if (!e.h.ok)
					Add("warning", "load_order", order[i], std::format("{} does not start with a valid plugin header (TES4).", order[i]), "Reinstall the mod that provides it.");
				const auto ext = Lower(Narrow(path.extension().wstring()));
				isMaster[i] = e.h.ok ? e.h.master : (ext == ".esm" || ext == ".esl");
			}

			// The order the game really uses: master-flagged plugins (ESM flag, .esm, .esl) load before every other
			// plugin, whatever plugins.txt says. Checking masters against plugins.txt order would miss an ESM-flagged
			// .esp that needs a normal .esp listed above it.
			const auto actual = Rules::ActualLoadOrder(implicitCount, isMaster);
			std::map<std::string, std::size_t> position;  // lower name -> real load position (files that exist)
			for (std::size_t p = 0; p < actual.size(); ++p)
				if (entries[actual[p]].exists)
					position.emplace(Lower(order[actual[p]]), p);

			std::size_t full = 0, light = 0;
			for (std::size_t p = 0; p < actual.size(); ++p) {
				const auto  i = actual[p];
				const auto& name = order[i];
				const auto& e = entries[i];
				if (!e.exists || !e.h.ok)
					continue;
				(e.h.light ? light : full) += 1;
				for (const auto& m : e.h.masters) {
					const auto it = position.find(Lower(m));
					if (it == position.end()) {
						const bool present = std::filesystem::exists(data / std::filesystem::path(Widen(m)), ec);
						Add("problem", "load_order", name,
							present ? std::format("{} needs the master {}, which is installed but NOT active.", name, m)
									: std::format("{} needs the master {}, which is NOT installed.", name, m),
							present ? std::format("Activate {}, or deactivate {}. A plugin whose master is inactive typically crashes the game while it loads (seen on 1.11.240 as a crash in Actor::InitItemImpl during new-game start).", m, name)
									: std::format("Install the mod that provides {}, or deactivate {}. The game usually crashes at startup otherwise.", m, name));
					} else if (it->second > p) {
						// The game does not move a master forward. The plugin's records then point into a file that is not
						// loaded yet: the same crash as an inactive master.
						const auto mi = actual[it->second];
						const bool flagged = isMaster[i] && !isMaster[mi] && mi < i;  // listed first, loads later because of the ESM flag
						Add("problem", "load_order", name,
							flagged ? std::format("{} loads before its master {}: {} has the master (ESM) flag, so the game loads it before every normal plugin, including {}.", name, m, name, m)
									: std::format("{} loads before its master {}.", name, m),
							flagged ? std::format("A master-flagged plugin cannot depend on a normal plugin. Deactivate {}, or get a version of it that does not need {}. Like an inactive master, this typically crashes the game while plugins load.", name, m)
									: "Sort the load order (LOOT, or your mod manager's sort) so masters load first. Like an inactive master, this typically crashes the game while plugins load.");
					}
				}
			}
			g_info.push_back(std::format("Load order: {} active plugins ({} full, {} light), from {}", order.size() - missingFiles, full, light, Narrow(txt.wstring())));
			// Full plugins get load indexes 00-FD (254, counted here with Fallout4.esm, the DLCs and Creation Club
			// files); FE holds the light plugins, FF is the game's own.
			constexpr std::size_t kFullSlots = 254, kLightSlots = 4096;
			if (full > kFullSlots)
				Add("problem", "load_order", "plugin count", std::format("{} full (non-light) plugins are active, counting Fallout4.esm, the DLCs and Creation Club files; the game has {} full-plugin slots.", full, kFullSlots),
					"Turn some plugins into light plugins (ESL-flag) where safe, or merge or remove some.");
			if (light > kLightSlots)
				Add("problem", "load_order", "light plugin count", std::format("{} light plugins are active; the limit is {}.", light, kLightSlots));
		}

		// ---------------- 5: file rules ----------------
		// ---------------- 4: loose-file setup (INI) ----------------
		// Mods that ship loose meshes, textures, sounds or interface files only work when the game is told to read loose
		// files: [Archive] bInvalidateOlderFiles=1 and sResourceDataDirsFinal= (empty). Without them the game silently
		// ignores those files. Read-only; reports what it found.
		//
		// The INI files are read with GetPrivateProfileStringW, the way the game reads them: it understands the UTF-16
		// files the game and BethINI write as well as ANSI ones. Under Mod Organizer 2 the read goes through its
		// virtual file system like the game's own, so it sees the profile's INI files when MO2 uses them.
		std::optional<std::string> IniValue(const std::filesystem::path& a_file, const wchar_t* a_section, const wchar_t* a_key)
		{
			std::error_code ec;
			if (!std::filesystem::exists(a_file, ec))
				return std::nullopt;
			constexpr const wchar_t* kMissing = L"\x01vitals-missing\x01";
			wchar_t                  buf[1024]{};
			const auto               n = REX::W32::GetPrivateProfileStringW(a_section, a_key, kMissing, buf, 1024, a_file.c_str());
			const std::wstring       v(buf, std::min<std::uint32_t>(n, 1023));
			if (v == kMissing)
				return std::nullopt;
			return Narrow(v);
		}

		// Is there at least one loose asset file the archive settings decide about? Empty folders left behind by a mod
		// manager do not count. Scripts are left out: the game reads loose .pex files either way (F4SE ships some).
		bool LooseAssetsInstalled(const std::filesystem::path& a_data)
		{
			static constexpr const wchar_t* kDirs[] = { L"Meshes", L"Textures", L"Materials", L"Sound", L"Music", L"Interface", L"Vis", L"LODSettings" };
			std::error_code ec;
			std::size_t     visited = 0;
			for (const auto* dir : kDirs) {
				const auto root = a_data / dir;
				if (!std::filesystem::is_directory(root, ec))
					continue;
				for (auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
					 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
					if (++visited > 50000)
						return true;  // a huge tree of folders: assume files are in there somewhere
					if (it->is_regular_file(ec))
						return true;
				}
				ec.clear();
			}
			return false;
		}

		void CheckLooseFileSetup()
		{
			const auto data = GameDir() / L"Data";
			if (!LooseAssetsInstalled(data))
				return;
			// The game reads Fallout4.ini, then Fallout4Prefs.ini, then Fallout4Custom.ini; a later file wins.
			const auto myGames = OutputDir().parent_path().parent_path();  // ...\My Games\Fallout4
			const auto effective = [&](const wchar_t* a_key) -> std::optional<std::string> {
				for (const auto* file : { L"Fallout4Custom.ini", L"Fallout4Prefs.ini", L"Fallout4.ini" })
					if (auto v = IniValue(myGames / file, L"Archive", a_key))
						return v;
				return std::nullopt;
			};
			const auto invalidate = effective(L"bInvalidateOlderFiles");
			const auto dirs = effective(L"sResourceDataDirsFinal");
			const bool invalidateOk = invalidate && Rules::IniInt(*invalidate) == 1;  // "1 ; set by BethINI" counts, as in game
			const bool dirsOk = dirs && Rules::IniString(*dirs).empty();
			if (invalidateOk && dirsOk)
				return;
			std::string missing;
			if (!invalidateOk)
				missing += "bInvalidateOlderFiles=1";
			if (!dirsOk)
				missing += std::string(missing.empty() ? "" : " and ") + "an empty sResourceDataDirsFinal=";
			Add("warning", "ini_setup", "Fallout4Custom.ini",
				std::format("Loose mod files (meshes, textures, sounds or interface files) are installed, but the game's INI settings do not turn on loose files: missing {} under [Archive]. The game will silently ignore those files.", missing),
				"Add to Documents\\My Games\\Fallout4\\Fallout4Custom.ini (with Mod Organizer 2 and profile-specific INI files, the profile's Fallout4Custom.ini):\n    [Archive]\n    bInvalidateOlderFiles=1\n    sResourceDataDirsFinal=\nMost mod managers and BethINI can do this for you.");
		}

		void CheckFileRules(const std::vector<Rule>& a_rules)
		{
			std::error_code ec;
			for (const auto& r : a_rules) {
				if (r.kind == "conflict") {
					if (GameFileExists(r.match) && GameFileExists(r.extra))
						Add(r.severity, "conflict", r.match + " + " + r.extra, r.message);
				} else if (r.kind == "oldfile") {
					const auto path = GameDir() / std::filesystem::path(Widen(r.match));
					const auto cutoff = ParseDate(r.extra);
					if (!cutoff || !std::filesystem::exists(path, ec))
						continue;
					const auto when = FileTimeToSys(std::filesystem::last_write_time(path, ec));
					if (!ec && when && *when < *cutoff)
						Add(r.severity, "loose_file", r.match,
							std::format("{} is a loose file dated {:%Y-%m-%d}, older than {}. {}", r.match, std::chrono::floor<std::chrono::days>(*when), r.extra, r.message));
				} else if (r.kind == "file") {
					if (GameFileExists(r.match))
						Add(r.severity, "known_issue", r.match, r.message);
				}
			}
		}

		// ---------------- output ----------------
		std::string ReportPathText() { return Redact(Narrow((OutputDir() / L"Vitals-Report.txt").wstring())); }

		void WriteReport()
		{
			std::ofstream out(OutputDir() / L"Vitals-Report.txt", std::ios::trunc);
			if (!out)
				return;
			const auto count = [](std::string_view s) { return std::ranges::count_if(g_issues, [&](const Issue& i) { return i.severity == s; }); };
			out << std::format("Vitals {} report | game {} ({}) | F4SE {}\n", F4SE::GetPluginVersion().string(),
				REX::FModule::GetExecutingModule().GetFileVersion().string(), RuntimeName(), F4SE::GetF4SEVersion().string());
			out << std::format("{} problem(s), {} warning(s), {} note(s). Problems may crash the game; warnings are mods that will not work as installed.\n\n",
				count("problem"), count("warning"), count("info"));
			for (const auto& i : g_info)
				out << "  " << Redact(i) << "\n";
			for (const char* sev : { "problem", "warning", "info" }) {
				bool header = false;
				for (const auto& i : g_issues) {
					if (i.severity != sev)
						continue;
					if (!header) {
						out << "\n" << (std::string_view(sev) == "problem" ? "PROBLEMS" : std::string_view(sev) == "warning" ? "WARNINGS" : "NOTES") << "\n";
						header = true;
					}
					out << "  - " << Redact(i.message) << "\n";
					if (!i.advice.empty())
						out << "    What to do: " << Redact(i.advice) << "\n";
					out << "\n";  // a blank line between entries
				}
			}
			if (!g_addrNotes.empty()) {
				out << "\nADDRESS CHECK (main menu)\n";
				for (const auto& n : g_addrNotes)
					out << "  " << Redact(n) << "\n";
			}
			out << ShareReportText();
			out << "\nDetails: Vitals.jsonl and Vitals-summary.txt in this folder. Vitals only reads; it changes nothing in your game.\n";
		}

		// MessageBox buttons cannot be renamed directly; a short-lived CBT hook on this thread relabels them when the
		// box opens. Abort/Retry/Ignore are used because that style has no Cancel button, so Esc or the close
		// button can never stop the game by accident (Ignore = Continue is the default).
		std::intptr_t __stdcall RenameButtons(int a_code, std::uintptr_t a_wParam, std::intptr_t a_lParam)
		{
			using SetDlgItemTextW_t = std::int32_t(__stdcall*)(void*, std::int32_t, const wchar_t*);
			using Unhook_t = std::int32_t(__stdcall*)(void*);
			using CallNext_t = std::intptr_t(__stdcall*)(void*, int, std::uintptr_t, std::intptr_t);
			constexpr int kActivate = 5;  // HCBT_ACTIVATE
			const auto    user32 = REX::W32::GetModuleHandleW(L"user32.dll");
			const auto    setText = reinterpret_cast<SetDlgItemTextW_t>(user32 ? REX::W32::GetProcAddress(user32, "SetDlgItemTextW") : nullptr);
			const auto    unhook = reinterpret_cast<Unhook_t>(user32 ? REX::W32::GetProcAddress(user32, "UnhookWindowsHookEx") : nullptr);
			const auto    callNext = reinterpret_cast<CallNext_t>(user32 ? REX::W32::GetProcAddress(user32, "CallNextHookEx") : nullptr);
			if (a_code == kActivate && setText) {
				const auto dlg = reinterpret_cast<void*>(a_wParam);
				g_box = dlg;
				setText(dlg, 3, L"Stop game");          // IDABORT
				setText(dlg, 4, L"Don't show");          // IDRETRY
				setText(dlg, 5, L"Continue");           // IDIGNORE
				setText(dlg, 9, L"Copy report");        // IDHELP
			}
			const auto hook = g_hook;  // the handle is ignored by CallNextHookEx on current Windows, but pass the real one
			const auto r = callNext ? callNext(hook, a_code, a_wParam, a_lParam) : 0;
			if (a_code == kActivate && setText) {
				if (unhook && g_hook)
					unhook(g_hook);
				g_hook = nullptr;
			}
			return r;
		}

		// Copy report (the box's Help button): puts Vitals-Report.txt on the clipboard and leaves the box open. The
		// report is already scrubbed of the user name. Windows calls this for the Help button instead of closing the box.
		void __stdcall CopyReport(void* /*a_helpInfo*/)
		{
			using Open_t = int(__stdcall*)(void*);
			using Empty_t = int(__stdcall*)();
			using Set_t = void*(__stdcall*)(std::uint32_t, void*);
			using Close_t = int(__stdcall*)();
			using SetText_t = int(__stdcall*)(void*, const wchar_t*);
			using Alloc_t = void*(__stdcall*)(std::uint32_t, std::size_t);
			using Lock_t = void*(__stdcall*)(void*);
			using Unlock_t = int(__stdcall*)(void*);
			using Free_t = void*(__stdcall*)(void*);
			using Item_t = void*(__stdcall*)(void*, int);
			const auto user32 = REX::W32::GetModuleHandleW(L"user32.dll");
			const auto kernel32 = REX::W32::GetModuleHandleW(L"kernel32.dll");
			if (!user32 || !kernel32)
				return;
			const auto open = reinterpret_cast<Open_t>(REX::W32::GetProcAddress(user32, "OpenClipboard"));
			const auto empty = reinterpret_cast<Empty_t>(REX::W32::GetProcAddress(user32, "EmptyClipboard"));
			const auto set = reinterpret_cast<Set_t>(REX::W32::GetProcAddress(user32, "SetClipboardData"));
			const auto close = reinterpret_cast<Close_t>(REX::W32::GetProcAddress(user32, "CloseClipboard"));
			const auto setText = reinterpret_cast<SetText_t>(REX::W32::GetProcAddress(user32, "SetWindowTextW"));
			const auto alloc = reinterpret_cast<Alloc_t>(REX::W32::GetProcAddress(kernel32, "GlobalAlloc"));
			const auto lock = reinterpret_cast<Lock_t>(REX::W32::GetProcAddress(kernel32, "GlobalLock"));
			const auto unlock = reinterpret_cast<Unlock_t>(REX::W32::GetProcAddress(kernel32, "GlobalUnlock"));
			const auto release = reinterpret_cast<Free_t>(REX::W32::GetProcAddress(kernel32, "GlobalFree"));
			const auto item = reinterpret_cast<Item_t>(REX::W32::GetProcAddress(user32, "GetDlgItem"));
			if (!open || !empty || !set || !close || !alloc || !lock || !unlock || !release)
				return;
			std::ifstream in(OutputDir() / L"Vitals-Report.txt", std::ios::binary);
			const std::string report((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::wstring text;
			for (const auto c : Widen(report)) {  // the clipboard wants Windows line ends
				if (c == L'\n' && (text.empty() || text.back() != L'\r'))
					text += L'\r';
				text += c;
			}
			constexpr std::uint32_t kMoveable = 0x2, kUnicodeText = 13;
			const auto              mem = alloc(kMoveable, (text.size() + 1) * sizeof(wchar_t));
			if (!mem)
				return;
			if (const auto p = lock(mem)) {
				std::memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
				unlock(mem);
			}
			bool done = false;
			// a window must own the clipboard, or SetClipboardData fails: the message box itself
			if (open(g_box)) {
				empty();
				done = set(kUnicodeText, mem) != nullptr;
				close();
			}
			if (!done)
				release(mem);  // on success the clipboard owns it
			// relabel the button itself (the help info may name the box, whose title must stay)
			if (const auto button = (item && g_box) ? item(g_box, 9) : nullptr; setText && button)
				setText(button, done ? L"Copied" : L"Copy failed");
			Emit("doctor_popup_copy", Json().Bool("copied", done).UNum("chars", text.size()));
		}

		// 3 = Stop game, 4 = Don't show, 5 = Continue (Copy report keeps the box open)
		int ShowPopup(const std::wstring& a_text)
		{
			using SetHook_t = void*(__stdcall*)(int, std::intptr_t(__stdcall*)(int, std::uintptr_t, std::intptr_t), void*, std::uint32_t);
			const auto user32 = REX::W32::GetModuleHandleW(L"user32.dll");
			const auto setHook = reinterpret_cast<SetHook_t>(user32 ? REX::W32::GetProcAddress(user32, "SetWindowsHookExW") : nullptr);
			if (setHook)
				g_hook = setHook(5 /*WH_CBT*/, &RenameButtons, nullptr, REX::W32::GetCurrentThreadId());
			constexpr std::uint32_t kAbortRetryIgnore = 0x2, kWarning = 0x30, kDefButton3 = 0x200, kHelp = 0x4000, kSetForeground = 0x10000, kTopmost = 0x40000;
			// MessageBoxIndirectW: the only message box with a callback for its Help button (Copy report)
			struct MsgBoxParams
			{
				std::uint32_t  size;
				void*          owner;
				void*          instance;
				const wchar_t* text;
				const wchar_t* caption;
				std::uint32_t  style;
				const wchar_t* icon;
				std::uintptr_t contextHelpId;
				void(__stdcall* callback)(void*);
				std::uint32_t  languageId;
			};
			using Indirect_t = int(__stdcall*)(const MsgBoxParams*);
			const auto indirect = reinterpret_cast<Indirect_t>(user32 ? REX::W32::GetProcAddress(user32, "MessageBoxIndirectW") : nullptr);
			constexpr std::uint32_t style = kAbortRetryIgnore | kWarning | kDefButton3 | kSetForeground | kTopmost;
			int r = 0;
			if (indirect) {
				const MsgBoxParams p{ sizeof(MsgBoxParams), nullptr, nullptr, a_text.c_str(), L"Vitals - mod setup problems", style | kHelp, nullptr, 0, &CopyReport, 0 };
				r = indirect(&p);
			} else {
				r = REX::W32::MessageBoxW(nullptr, a_text.c_str(), L"Vitals - mod setup problems", style);
			}
			g_box = nullptr;
			if (g_hook) {  // the box never activated (should not happen): do not leave the hook installed
				using Unhook_t = std::int32_t(__stdcall*)(void*);
				if (const auto unhook = reinterpret_cast<Unhook_t>(user32 ? REX::W32::GetProcAddress(user32, "UnhookWindowsHookEx") : nullptr))
					unhook(g_hook);
				g_hook = nullptr;
			}
			return r;
		}

		std::string PopupKey(const Issue& a_i) { return a_i.category + ":" + Lower(a_i.subject); }

		// Keys in popup_disabled.txt: Rules::ParseKeyFile / FormatKeyFile (a versioned file, one key per
		// line, read verbatim so DLL names with spaces survive). Older files are still read.
		Rules::KeyFile ReadKeys(const std::filesystem::path& a_file, bool& a_exists)
		{
			std::ifstream in(a_file, std::ios::binary);
			a_exists = static_cast<bool>(in);
			if (!in)
				return {};
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			return Rules::ParseKeyFile(text);
		}

		void WriteKeys(const std::filesystem::path& a_file, const std::set<std::string>& a_keys, std::string_view a_comment)
		{
			std::error_code ec;
			if (a_keys.empty() && a_comment.empty()) {
				std::filesystem::remove(a_file, ec);
				return;
			}
			std::ofstream out(a_file, std::ios::binary | std::ios::trunc);
			out << Rules::FormatKeyFile(a_keys, a_comment);
		}

		void MaybePopup()
		{
			const auto mode = static_cast<int>(GetSettings().popupMode);
			if (!mode)
				return;
			const auto offFile = OutputDir() / L"popup_disabled.txt";
			// What may open the box: problems that may crash the game, and F4SE plugins that are not running.
			std::vector<std::string>         crash, notRunning;
			std::map<std::string, const Issue*> byKey;
			for (const auto& i : g_issues) {
				const bool isCrash = i.severity == "problem";
				const bool isDll = i.severity == "warning" && i.category == "plugin_dll";
				if (!isCrash && !isDll)
					continue;
				const auto k = PopupKey(i);
				if (!byKey.emplace(k, &i).second)
					continue;
				(isCrash ? crash : notRunning).push_back(k);
			}
			bool       offExists = false;
			const auto offFileKeys = ReadKeys(offFile, offExists);
			const auto muted = offFileKeys.keys;
			const bool mutedAll = offExists && offFileKeys.legacyNoKeys;  // the 0.3.0 file: it muted every plugin
			const auto state = Rules::DecidePopup(mode, crash, notRunning, muted, mutedAll);
			if (!state.open)
				return;

			std::vector<const Issue*> crashShown, dllShown;
			for (const auto& k : state.show)
				(byKey[k]->severity == "problem" ? crashShown : dllShown).push_back(byKey[k]);
			std::string text;
			if (!crashShown.empty()) {
				text += std::format("MAY CRASH THE GAME ({}):\n", crashShown.size());
				for (std::size_t i = 0; i < crashShown.size() && i < 6; ++i)
					text += "- " + Rules::Utf8Prefix(Redact(crashShown[i]->message), 300) + "\n";
				if (crashShown.size() > 6)
					text += std::format("...and {} more.\n", crashShown.size() - 6);
				text += "\n";
			}
			if (!dllShown.empty()) {
				// Plugins F4SE skips for lack of version data are listed together, split by when they were built.
				struct Grp { const char* name; const char* title; std::string list; std::size_t n{ 0 }; };
				std::array<Grp, 4> groups{ { { "pre", "Built before April 2024 (made for Old-Gen)", {}, 0 },
					{ "post", "Built after April 2024, but without Next-Gen data (check the mod page)", {}, 0 },
					{ "unknown", "Build date unknown", {}, 0 },
					{ "", "Other reasons (see the report)", {}, 0 } } };
				for (const auto* p : dllShown)
					for (auto& g : groups)
						if (p->group == g.name || !*g.name) {
							g.list += (g.list.empty() ? "" : ", ") + p->subject;
							++g.n;
							break;
						}
				text += std::format("MODS NOT RUNNING: {} F4SE plugin(s) are installed but F4SE did not load them, so their features will not work.\n", dllShown.size());
				for (const auto& g : groups)
					if (g.n)
						text += std::format("\n{} ({}):\n    {}\n", g.title, g.n, g.list.size() > 300 ? Rules::Utf8Prefix(g.list, 300) + ", ..." : g.list);
				text += "\n";
			}
			text += "Full report with what to do: " + ReportPathText() +
			        "\n\nContinue = start the game (shows again next launch)\nDon't show = stop listing these plugins\nStop game = close now and fix this first\nCopy report = copy the full report for a bug report";
			Emit("doctor_popup", Json().UNum("crash_risks", crashShown.size()).UNum("not_running", dllShown.size()));
			const auto choice = ShowPopup(Widen(text));
			if (choice == 4) {
				auto keys = muted;
				for (const auto* p : dllShown)
					keys.insert(PopupKey(*p));
				if (mutedAll)  // 0.3.0 file: it already muted every plugin; keep that
					for (const auto& k : notRunning)
						keys.insert(k);
				WriteKeys(offFile, keys, "# Vitals will not show these F4SE plugins in the start-up box again (problems that may crash the game still show).\n# Delete this file to show them again.\n");
				Emit("doctor_popup_choice", Json().Str("choice", "dont_show_again"));
			} else if (choice == 3) {
				Emit("doctor_popup_choice", Json().Str("choice", "stop_game"));
				Summary("Stopped by the user from the Vitals problem box");
				REX::W32::TerminateProcess(REX::W32::GetCurrentProcess(), 0);
			} else {
				Emit("doctor_popup_choice", Json().Str("choice", "continue"));
			}
		}
	}

	void RunDoctor()
	{
		Crumb("Vitals", "doctor");
		std::scoped_lock l(g_lock);
		const auto       started = std::chrono::steady_clock::now();
		const auto rules = LoadRules();
		CheckDlls(rules);
		CheckLoadOrder();
		CheckFileRules(rules);
		CheckLooseFileSetup();
		std::size_t problems = 0, warnings = 0;
		for (const auto& i : g_issues) {
			Json j;
			j.Str("severity", i.severity).Str("category", i.category).Str("subject", i.subject).Str("message", i.message).Str("advice", i.advice);
			if (!i.reason.empty())
				j.Str("reason", i.reason);
			if (!i.group.empty())
				j.Str("built", i.group);
			Emit("doctor_issue", std::move(j));
			problems += i.severity == "problem";
			warnings += i.severity == "warning";
		}
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
		Emit("doctor", Json().UNum("rules", rules.size()).UNum("problems", problems).UNum("warnings", warnings).UNum("issues", g_issues.size())
			.UNum("ms", static_cast<std::uint64_t>(ms)));
		Summary(std::format("Doctor: {} problem(s), {} warning(s), {} rules loaded. See Vitals-Report.txt", problems, warnings, rules.size()));
		WriteReport();
		MaybePopup();
	}

	void DoctorAddressNote(std::string_view a_line)
	{
		if (!GetSettings().doctor)
			return;
		std::scoped_lock l(g_lock);
		g_addrNotes.emplace_back(a_line);
		WriteReport();
	}
}
