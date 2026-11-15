#include "Vitals.h"
#include "Vitals/Formats.h"
#include "Vitals/Rules.h"

#include "REX/HASH.h"

// Two kernel32 functions CommonLib's REX::W32 does not declare.
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();
extern "C" __declspec(dllimport) int __stdcall ReadProcessMemory(void* a_process, const void* a_base, void* a_buffer, std::size_t a_size, std::size_t* a_read);

namespace Vitals
{
	namespace
	{
		Settings      g_settings;
		std::mutex    g_outLock;
		std::ofstream g_jsonl;
		std::ofstream g_summary;

		std::wstring EnvW(const wchar_t* a_name)
		{
			wchar_t buf[512]{};
			const auto n = REX::W32::GetEnvironmentVariableW(a_name, buf, 512);
			return (n > 0 && n < 512) ? std::wstring(buf, n) : std::wstring{};
		}

		void ReplaceAllI(std::string& a_text, const std::string& a_from, const std::string& a_to)
		{
			if (a_from.size() < 2)
				return;
			auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
			const auto from = lower(a_from);
			std::size_t pos = 0;
			while (true) {
				const auto hay = lower(a_text);
				pos = hay.find(from, pos);
				if (pos == std::string::npos)
					break;
				// whole path only: C:\Users\Rob must not eat the start of C:\Users\Robert
				if (const auto end = pos + from.size(); end < a_text.size() && std::isalnum(static_cast<unsigned char>(a_text[end]))) {
					pos = end;
					continue;
				}
				a_text.replace(pos, a_from.size(), a_to);
				pos += a_to.size();
			}
		}
	}

	// ---------------- settings ----------------
	const Settings& GetSettings() { return g_settings; }

	void LoadSettings()
	{
		const auto ini = (PluginDir() / L"Vitals.ini").wstring();
		auto get = [&](const wchar_t* a_key, int a_def) {
			return static_cast<int>(REX::W32::GetPrivateProfileIntW(L"Vitals", a_key, a_def, ini.c_str()));
		};
		g_settings.environment = get(L"bEnvironmentReport", 1) != 0;
		g_settings.addressCheck = get(L"bAddressCheck", 1) != 0;
		g_settings.breadcrumbs = get(L"bBreadcrumbs", 1) != 0;
		g_settings.papyrusNatives = get(L"bPapyrusNatives", 0) != 0;
		g_settings.layoutProbes = get(L"bLayoutProbes", 0) != 0;
		g_settings.captureBaseline = get(L"bCaptureBaseline", 0) != 0;
		g_settings.timeline = get(L"bTimeline", 1) != 0;
		g_settings.exitAfterMenuSeconds = static_cast<std::uint32_t>(std::clamp(get(L"iExitAfterMenuSeconds", 0), 0, 3600));
		g_settings.exitAfterLoadSeconds = static_cast<std::uint32_t>(std::clamp(get(L"iExitAfterLoadSeconds", 0), 0, 3600));
		g_settings.bytesToRead = static_cast<std::uint32_t>(std::clamp(get(L"iBytesToRead", 16), 4, 64));
		g_settings.doctor = get(L"bDoctor", 1) != 0;
		g_settings.popupMode = static_cast<std::uint32_t>(std::clamp(get(L"iPopupMode", 1), 0, 2));
		g_settings.keepSessions = static_cast<std::uint32_t>(std::clamp(get(L"iKeepSessions", 5), 0, 50));
		g_settings.sharePack = std::clamp(get(L"bSharePack", -1), -1, 1);
		g_settings.shareSend = get(L"bShareSend", 1) != 0;
	}

	// ---------------- paths ----------------
	std::filesystem::path GameDir()
	{
		wchar_t buf[1024]{};
		REX::W32::GetModuleFileNameW(nullptr, buf, 1024);
		return std::filesystem::path(buf).parent_path();
	}

	std::filesystem::path PluginDir() { return GameDir() / L"Data" / L"F4SE" / L"Plugins"; }

	std::filesystem::path OutputDir()
	{
		wchar_t* known{ nullptr };
		const auto hr = REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &known);
		std::filesystem::path docs = (hr == 0 && known) ? std::filesystem::path(known) : GameDir();
		if (known)
			REX::W32::CoTaskMemFree(known);
		auto folder = F4SE::GetSaveFolderName();  // 0.7.1+; empty on older F4SE
		const std::string save = folder.empty() ? "Fallout4"s : std::string(folder);
		return docs / L"My Games" / save / L"F4SE" / L"Vitals";
	}

	std::string Narrow(const std::wstring& a_wide)
	{
		if (a_wide.empty())
			return {};
		const int n = REX::W32::WideCharToMultiByte(65001, 0, a_wide.c_str(), static_cast<int>(a_wide.size()), nullptr, 0, nullptr, nullptr);
		if (n <= 0)
			return "?";
		std::string out(static_cast<std::size_t>(n), '\0');
		REX::W32::WideCharToMultiByte(65001, 0, a_wide.c_str(), static_cast<int>(a_wide.size()), out.data(), n, nullptr, nullptr);
		return out;
	}

	std::wstring Widen(std::string_view a_utf8)
	{
		if (a_utf8.empty())
			return {};
		const int n = REX::W32::MultiByteToWideChar(65001, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
		if (n <= 0) {
			// Not convertible as UTF-8 (should not happen: invalid bytes become U+FFFD). Fall back to the Windows code
			// page instead of an empty path or message.
			const int m = REX::W32::MultiByteToWideChar(0 /*CP_ACP*/, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), nullptr, 0);
			std::wstring out(static_cast<std::size_t>(std::max(m, 0)), L'\0');
			if (m > 0)
				REX::W32::MultiByteToWideChar(0, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), out.data(), m);
			return out;
		}
		std::wstring out(static_cast<std::size_t>(n), L'\0');
		REX::W32::MultiByteToWideChar(65001, 0, a_utf8.data(), static_cast<int>(a_utf8.size()), out.data(), n);
		return out;
	}

	std::string Lower(std::string a_text)
	{
		for (auto& c : a_text)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return a_text;
	}

	std::string Redact(std::string a_text)
	{
		static const std::string profile = Narrow(EnvW(L"USERPROFILE"));
		static const std::string user = Narrow(EnvW(L"USERNAME"));
		if (!profile.empty()) {
			ReplaceAllI(a_text, profile, "%USERPROFILE%");
			auto alt = profile;
			std::ranges::replace(alt, '\\', '/');
			ReplaceAllI(a_text, alt, "%USERPROFILE%");
		}
		// The user name alone only where it is a whole folder name in a path (a profile on another drive, a OneDrive
		// folder). Never as a bare substring: "Rob" would turn "PROBLEMS" into "P<user>LEMS".
		return Rules::RedactUserFolder(std::move(a_text), user);
	}

	// ---------------- JSON ----------------
	std::string Json::Quote(std::string_view a_text)
	{
		std::string out = "\"";
		for (const unsigned char c : a_text) {
			switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (c < 0x20)
					out += std::format("\\u{:04x}", c);
				else
					out += static_cast<char>(c);
			}
		}
		return out + "\"";
	}

	std::string Json::Array(const std::vector<std::string>& a_items)
	{
		std::string out = "[";
		for (std::size_t i = 0; i < a_items.size(); ++i) {
			if (i)
				out += ",";
			out += a_items[i];
		}
		return out + "]";
	}

	void  Json::Key(std::string_view a_key) { _body += (_body.empty() ? "" : ",") + Quote(a_key) + ":"; }
	Json& Json::Str(std::string_view k, std::string_view v) { Key(k); _body += Quote(Redact(std::string(v))); return *this; }
	Json& Json::Num(std::string_view k, std::int64_t v) { Key(k); _body += std::to_string(v); return *this; }
	Json& Json::UNum(std::string_view k, std::uint64_t v) { Key(k); _body += std::to_string(v); return *this; }
	Json& Json::Bool(std::string_view k, bool v) { Key(k); _body += v ? "true" : "false"; return *this; }
	Json& Json::Raw(std::string_view k, std::string_view v) { Key(k); _body += v; return *this; }
	Json& Json::Null(std::string_view k) { Key(k); _body += "null"; return *this; }

	// ---------------- report files ----------------
	void OpenReport()
	{
		std::error_code ec;
		const auto dir = OutputDir();
		std::filesystem::create_directories(dir, ec);
		const auto cur = dir / L"Vitals.jsonl";
		if (std::filesystem::exists(cur, ec)) {
			// Keep dated copies of earlier sessions (iKeepSessions), so --compare can use any of them.
			if (const auto keep = GetSettings().keepSessions; keep > 0) {
				const auto hist = dir / L"history";
				std::filesystem::create_directories(hist, ec);
				const auto when = std::chrono::clock_cast<std::chrono::system_clock>(std::filesystem::last_write_time(cur, ec));
				const auto name = std::format("Vitals-{:%Y%m%d-%H%M%S}.jsonl", std::chrono::floor<std::chrono::seconds>(when));
				std::filesystem::copy_file(cur, hist / name, std::filesystem::copy_options::overwrite_existing, ec);
				std::vector<std::filesystem::path> old;
				for (const auto& e : std::filesystem::directory_iterator(hist, ec))
					if (e.path().extension() == L".jsonl")
						old.push_back(e.path());
				std::ranges::sort(old);  // names sort by date
				while (old.size() > keep) {
					std::filesystem::remove(old.front(), ec);
					old.erase(old.begin());
				}
			}
			std::filesystem::rename(cur, dir / L"Vitals.prev.jsonl", ec);
		}
		std::scoped_lock l(g_outLock);
		g_jsonl.open(cur, std::ios::out | std::ios::trunc | std::ios::binary);
		g_summary.open(dir / L"Vitals-summary.txt", std::ios::out | std::ios::trunc);
	}

	void Emit(std::string_view a_kind, Json a_json)
	{
		const auto now = std::chrono::system_clock::now();
		Json head;
		head.Str("t", std::format("{:%FT%T}Z", std::chrono::floor<std::chrono::milliseconds>(now)));
		head.Str("kind", a_kind);
		auto line = head.Build();
		const auto body = a_json.Build();
		if (body.size() > 2)
			line = line.substr(0, line.size() - 1) + "," + body.substr(1);
		std::scoped_lock l(g_outLock);
		if (g_jsonl.is_open()) {
			g_jsonl << line << '\n';
			g_jsonl.flush();  // flush every line: if the game crashes, everything up to here is on disk
		}
	}

	void Summary(std::string_view a_line)
	{
		const auto line = Redact(std::string(a_line));
		REX::INFO("{}", line);
		std::scoped_lock l(g_outLock);
		if (g_summary.is_open()) {
			g_summary << line << '\n';
			g_summary.flush();
		}
	}

	// ---------------- safe reads ----------------
	bool IsReadable(const void* a_addr, std::size_t a_len)
	{
		auto p = reinterpret_cast<std::uintptr_t>(a_addr);
		const auto end = p + a_len;
		if (p < 0x10000 || end < p)
			return false;
		while (p < end) {
			REX::W32::MEMORY_BASIC_INFORMATION mbi{};
			if (!REX::W32::VirtualQuery(reinterpret_cast<const void*>(p), &mbi, sizeof(mbi)))
				return false;
			if (mbi.state != REX::W32::MEM_COMMIT)
				return false;
			constexpr std::uint32_t readable = REX::W32::PAGE_READONLY | REX::W32::PAGE_READWRITE | REX::W32::PAGE_WRITECOPY |
			                                   REX::W32::PAGE_EXECUTE_READ | REX::W32::PAGE_EXECUTE_READWRITE | 0x80u /*EXECUTE_WRITECOPY*/;
			if (!(mbi.protect & readable) || (mbi.protect & 0x100u /*GUARD*/))
				return false;
			p = reinterpret_cast<std::uintptr_t>(mbi.baseAddress) + mbi.regionSize;
		}
		return true;
	}

	bool IsExecutable(const void* a_addr)
	{
		REX::W32::MEMORY_BASIC_INFORMATION mbi{};
		if (!REX::W32::VirtualQuery(a_addr, &mbi, sizeof(mbi)) || mbi.state != REX::W32::MEM_COMMIT)
			return false;
		return (mbi.protect & (0x10u | 0x20u | 0x40u | 0x80u)) != 0;  // PAGE_EXECUTE*, any flavour
	}

	bool SafeRead(const void* a_addr, void* a_out, std::size_t a_len)
	{
		// VirtualQuery first (skips guard pages and unmapped memory), then ReadProcessMemory on our own process rather
		// than memcpy: if another thread frees or re-protects the page in between, the read fails instead of crashing.
		if (!IsReadable(a_addr, a_len))
			return false;
		std::size_t got = 0;
		return ::ReadProcessMemory(REX::W32::GetCurrentProcess(), a_addr, a_out, a_len, &got) && got == a_len;
	}

	std::uint32_t ProcessId() { return static_cast<std::uint32_t>(::GetCurrentProcessId()); }

	std::string Hex(const std::uint8_t* a_data, std::size_t a_len)
	{
		std::string out;
		for (std::size_t i = 0; i < a_len; ++i)
			out += std::format("{:02X}", a_data[i]);
		return out;
	}

	std::string ModuleNameForAddress(const void* a_addr)
	{
		const auto p = ModulePathForAddress(a_addr);
		return p.empty() ? "?"s : Narrow(p.filename().wstring());
	}

	std::filesystem::path ModulePathForAddress(const void* a_addr)
	{
		REX::W32::MEMORY_BASIC_INFORMATION mbi{};
		if (!a_addr || !REX::W32::VirtualQuery(a_addr, &mbi, sizeof(mbi)) || !mbi.allocationBase || mbi.type != 0x1000000 /*MEM_IMAGE*/)
			return {};
		wchar_t buf[1024]{};
		const auto n = REX::W32::GetModuleFileNameW(static_cast<REX::W32::HMODULE>(mbi.allocationBase), buf, 1024);
		return n ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::path{};
	}

	// ---------------- Address Library ----------------
	// Vitals reads the Address Library file itself and never constructs CommonLib's REL::IDDB on its own: IDDB ends the
	// game (a message box, then TerminateProcess) when the file is missing, unreadable or one of the files CommonLib
	// blocks, and it trusts the entry count of the file (a truncated file reads past the end). Features that go
	// through CommonLib's lookups (RE:: singletons) run only when AddressLibrary().usable says CommonLib would accept
	// the same file, and the ID they use is in it.
	namespace
	{
		struct ALData
		{
			AddressLibraryInfo        info;
			std::vector<std::uint8_t> bytes;
		};

		std::filesystem::path ThisPluginDir()
		{
			wchar_t    buf[1024]{};
			const auto n = REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buf, 1024);
			return n && n < 1024 ? std::filesystem::path(std::wstring(buf, n)).parent_path() : PluginDir();
		}

		// Files CommonLib refuses (REL::IDDB::validate_file at the pinned commit): game version -> SHA-512.
		std::string_view BlockedHash(const REL::Version& a_v)
		{
			if (a_v == REL::Version{ 1, 10, 980 })
				return "2AD60B95388F1B6E77A6F86F17BEB51D043CF95A341E91ECB2E911A393E45FE8156D585D2562F7B14434483D6E6652E2373B91589013507CABAE596C26A343F1";
			if (a_v == REL::Version{ 1, 11, 159 })
				return "686D40387F638ED75AD43BB76CA14170576F1A30E91144F280987D13A3012B1CA6A4E04E6BE7A5B99E46C50332C49BE40C3D9448038E17D3D31C40E72A90AE26";
			if (a_v == REL::Version{ 1, 11, 240 })
				return "5A18BE865BEB3BB3D6A6BE8C50D424D012E5CCF73A83A7837865BE31D4C1D587F74A98F82EEECAEA55B83F5C657D8DC6525DFAD250C9492E958ACF4716575094";
			return {};
		}

		ALData LoadAddressLibrary()
		{
			ALData     d;
			auto&      i = d.info;
			const auto v = REX::FModule::GetExecutingModule().GetFileVersion();
			i.file = std::format("version-{}-{}-{}-{}.bin", v.major(), v.minor(), v.patch(), v.build());
			const auto      path = ThisPluginDir() / std::filesystem::path(Widen(i.file));
			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) {
				i.state = "missing";
				i.problem = "the file is missing";
				return d;
			}
			i.present = true;
			const auto size = std::filesystem::file_size(path, ec);
			std::ifstream in(path, std::ios::binary);
			if (ec || !in || size > (512ull << 20)) {
				i.state = "unreadable";
				i.problem = "the file cannot be read";
				return d;
			}
			d.bytes.resize(static_cast<std::size_t>(size));
			in.read(reinterpret_cast<char*>(d.bytes.data()), static_cast<std::streamsize>(d.bytes.size()));
			if (static_cast<std::uint64_t>(in.gcount()) != size) {
				d.bytes.clear();
				i.state = "unreadable";
				i.problem = "the file cannot be read";
				return d;
			}
			const auto t = Formats::AddressLib::Check(d.bytes);
			i.count = t.count;
			i.state = Formats::AddressLib::Name(t.state);
			switch (t.state) {
			case Formats::AddressLib::State::ok:
				break;
			case Formats::AddressLib::State::truncated:
				i.problem = std::format("the file is cut short (it lists {} entries but holds only {})", t.count, (d.bytes.size() - 8) / 16);
				break;
			case Formats::AddressLib::State::unsorted:
				i.problem = "the file is damaged (its entries are out of order)";
				break;
			default:
				i.problem = "the file is empty or damaged";
				break;
			}
			if (i.problem.empty())
				if (const auto bad = BlockedHash(v); !bad.empty()) {
					const auto sha = REX::SHA512({ reinterpret_cast<const std::byte*>(d.bytes.data()), d.bytes.size() });
					if (sha && *sha == bad) {
						i.state = "blocked";
						i.problem = "it is an old Address Library file that CommonLib-based plugins refuse (they show an error and close the game)";
					}
				}
			i.usable = i.problem.empty();
			if (!i.usable)
				d.bytes.clear();
			return d;
		}

		const ALData& AL()
		{
			static const ALData d = LoadAddressLibrary();
			return d;
		}
	}

	const AddressLibraryInfo& AddressLibrary() { return AL().info; }

	std::optional<std::uint64_t> LookupOffset(std::uint64_t a_id)
	{
		const auto& d = AL();
		if (!d.info.usable)
			return std::nullopt;
		return Formats::AddressLib::Find(d.bytes, d.info.count, a_id);
	}

	std::optional<std::chrono::sys_seconds> FileTimeToSys(std::filesystem::file_time_type a_t)
	{
		// MSVC's file_clock counts 100 ns ticks since 1601-01-01 (a FILETIME). Converted by hand: clock_cast goes
		// through utc_clock and the time zone data, which can throw.
		using ticks = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
		const auto t = std::chrono::duration_cast<ticks>(a_t.time_since_epoch()).count();
		constexpr std::int64_t k1601To1970 = 116'444'736'000'000'000;
		if (t < k1601To1970)
			return std::nullopt;
		return std::chrono::sys_seconds{ std::chrono::seconds{ (t - k1601To1970) / 10'000'000 } };
	}

	int RuntimeIndex() { return static_cast<int>(REX::FModule::GetRuntimeIndex()); }

	const char* RuntimeName()
	{
		switch (RuntimeIndex()) {
		case 0: return "OG";
		case 1: return "NG";
		default: return "AE";
		}
	}
}
