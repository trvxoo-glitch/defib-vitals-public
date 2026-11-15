#pragma once
// Vitals rules that do not need the game: plain C++20 and the standard library only, so they can be compiled and
// tested on any machine (tests/cpp/rules_test.cpp, run in CI). The plugin includes this header too, so what the
// tests check is exactly what ships.
//
//  * Loader: the F4SE plugin loader's decision for one DLL. Mirrors F4SE's PluginManager
//    (0.6.x: Query decides; 0.7.x: CheckPluginCompatibility) and tools/f4se_dll_scan.py verdict().
//    tests/loader_cases.txt feeds the same cases to both, so they cannot drift apart.
//  * Text: Trim, Split, and plugin-name decoding (plugins.txt and .esp headers are often Windows-1252, not UTF-8).
//  * Code: does a function start with a stub that returns at once (an in-place patch, not a jump hook)?
//  * Popup: which problems are new enough to open the start-up message box, and the key files that remember them.
//  * Player text: UTF-8-safe cuts, and the Windows user name replaced only where it is a folder name in a path.
//  * Load order: the order the game really loads plugins in (master-flagged ones first).
// File readers (PE, Address Library, plugin headers) are in Formats.h.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace Vitals::Rules
{
	// ---------------------------------------------------------------- text
	inline std::string Trim(std::string_view a_s)
	{
		const auto b = a_s.find_first_not_of(" \t\r\n");
		if (b == std::string_view::npos)
			return {};
		const auto e = a_s.find_last_not_of(" \t\r\n");
		return std::string(a_s.substr(b, e - b + 1));
	}

	inline std::vector<std::string> Split(std::string_view a_line, char a_sep)
	{
		std::vector<std::string> out;
		std::size_t              start = 0;
		while (true) {
			const auto pos = a_line.find(a_sep, start);
			out.push_back(Trim(a_line.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start)));
			if (pos == std::string_view::npos)
				break;
			start = pos + 1;
		}
		return out;
	}

	inline bool IsValidUtf8(std::string_view a_s)
	{
		for (std::size_t i = 0; i < a_s.size();) {
			const auto c = static_cast<unsigned char>(a_s[i]);
			std::size_t n = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 99;
			if (n == 99 || (n == 1 && c < 0xC2) || i + n >= a_s.size() + (n ? 0 : 1))
				return false;
			for (std::size_t k = 1; k <= n; ++k)
				if ((static_cast<unsigned char>(a_s[i + k]) >> 6) != 0x2)
					return false;
			i += n + 1;
		}
		return true;
	}

	// Plugin names come from plugins.txt, Fallout4.ccc and MAST records in .esp/.esm/.esl headers. The game and most
	// tools write them in the Windows code page (1252); some managers write UTF-8. Valid UTF-8 is kept as is,
	// anything else is read as Windows-1252, so an accented name still finds its file.
	inline std::string PluginNameToUtf8(std::string_view a_raw)
	{
		if (IsValidUtf8(a_raw))
			return std::string(a_raw);
		static constexpr char16_t k80[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
			0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
		std::string out;
		for (const char ch : a_raw) {
			const auto     c = static_cast<unsigned char>(ch);
			const char32_t u = c < 0x80 ? c : c < 0xA0 ? k80[c - 0x80] : c;
			if (u < 0x80) {
				out += static_cast<char>(u);
			} else if (u < 0x800) {
				out += static_cast<char>(0xC0 | (u >> 6));
				out += static_cast<char>(0x80 | (u & 0x3F));
			} else {
				out += static_cast<char>(0xE0 | (u >> 12));
				out += static_cast<char>(0x80 | ((u >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (u & 0x3F));
			}
		}
		return out;
	}

	// An INI number the way the game reads it: the leading integer ("1 ; set by BethINI" is 1).
	inline std::optional<long> IniInt(std::string_view a_value)
	{
		const auto t = Trim(a_value);
		std::size_t i = (!t.empty() && (t[0] == '-' || t[0] == '+')) ? 1 : 0;
		if (i >= t.size() || t[i] < '0' || t[i] > '9')
			return std::nullopt;
		long v = 0;
		for (; i < t.size() && t[i] >= '0' && t[i] <= '9' && v < 1000000; ++i)
			v = v * 10 + (t[i] - '0');
		return t[0] == '-' ? -v : v;
	}

	// An INI string with a trailing "; comment" removed.
	inline std::string IniString(std::string_view a_value)
	{
		return Trim(a_value.substr(0, a_value.find(';')));
	}

	// ---------------------------------------------------------------- code
	// Does a function start with a stub that returns at once? Fix mods that switch a game function off write one
	// over its first bytes in place ("ret", "xor eax,eax; ret", "mov al,1; ret"...). That is a patch, not a jump
	// hook, so no owner can be named from the bytes. Returns a short description, or empty.
	inline std::string ReturnStub(const std::uint8_t* a_b, std::size_t a_len)
	{
		auto at = [&](std::size_t i) { return i < a_len ? a_b[i] : 0x100; };
		if (at(0) == 0xC3)
			return "ret";
		if (at(0) == 0xC2)
			return "ret imm16";
		if ((at(0) == 0x33 || at(0) == 0x31 || at(0) == 0x32 || at(0) == 0x30) && at(1) == 0xC0 && at(2) == 0xC3)
			return "xor eax,eax; ret";
		if (at(0) == 0x48 && (at(1) == 0x33 || at(1) == 0x31) && at(2) == 0xC0 && at(3) == 0xC3)
			return "xor rax,rax; ret";
		if (at(0) == 0xB0 && at(2) == 0xC3)
			return "mov al,imm8; ret";
		if (at(0) == 0xB8 && at(5) == 0xC3)
			return "mov eax,imm32; ret";
		return {};
	}

	// ---------------------------------------------------------------- loader
	namespace Loader
	{
		enum class Era { OG, NG, AE };
		enum class Status { yes, maybe, no };
		enum class Why
		{
			not_plugin,                  // no F4SEPlugin_ export at all (helper library, or not a plugin)
			og_query_decides,            // OG: exports Query, its own code accepts or declines at run time (maybe)
			og_no_query,                 // OG: no Query, so F4SE 0.6.x never loads it (made for Next-Gen/Anniversary)
			no_version_data_query_only,  // NG/AE: an Old-Gen style plugin (Query only)
			no_version_data,             // NG/AE: no F4SEPlugin_Version export
			bad_version_data,            // dataVersion is 0
			no_name,                     // empty plugin name
			address_library_missing,     // uses the current Address Library and the file for this build is missing
			independent,                 // declares address and structure independence for this era (yes)
			listed,                      // lists this exact game build (yes)
			ng_flags_only,               // AE: only Next-Gen flags, which F4SE 0.7.5+ ignores
			other_builds,                // lists other game builds only
			not_declared,                // no independence flags and an empty list
			needs_newer_f4se,            // compatible, but needs a newer F4SE than the one running
		};

		inline const char* Name(Why a_w)
		{
			switch (a_w) {
			case Why::not_plugin: return "not_plugin";
			case Why::og_query_decides: return "og_query_decides";
			case Why::og_no_query: return "og_no_query";
			case Why::no_version_data_query_only: return "no_version_data_query_only";
			case Why::no_version_data: return "no_version_data";
			case Why::bad_version_data: return "bad_version_data";
			case Why::no_name: return "no_name";
			case Why::address_library_missing: return "address_library_missing";
			case Why::independent: return "independent";
			case Why::listed: return "listed";
			case Why::ng_flags_only: return "ng_flags_only";
			case Why::other_builds: return "other_builds";
			case Why::not_declared: return "not_declared";
			case Why::needs_newer_f4se: return "needs_newer_f4se";
			}
			return "?";
		}

		inline const char* Name(Status a_s) { return a_s == Status::yes ? "yes" : a_s == Status::maybe ? "maybe" : "no"; }

		// F4SEPluginVersionData flag bits (f4se/PluginAPI.h)
		constexpr std::uint32_t kAddrSignatures = 1u << 0, kAddrAL_1_10_980 = 1u << 1, kAddrAL_1_11_137 = 1u << 2;
		constexpr std::uint32_t kStructNone = 1u << 0, kStruct_1_10_980 = 1u << 1, kStruct_1_11_137 = 1u << 2;

		struct VersionData
		{
			std::uint32_t              dataVersion{ 1 };
			bool                       hasName{ true };
			std::uint32_t              addr{ 0 };
			std::uint32_t              structs{ 0 };
			std::vector<std::uint32_t> compat;  // packed game versions, stops at the first 0
			std::uint32_t              f4seRequired{ 0 };
			std::uint32_t              reservedBreaking{ 0 };
		};

		struct Input
		{
			Era                        era{ Era::AE };
			std::uint32_t              runtime{ 0 };       // packed game version (major<<24 | minor<<16 | build<<4)
			std::uint32_t              f4se{ 0 };          // packed running F4SE version; 0 = unknown, skip that check
			bool                       hasAnyExport{ false };  // any F4SEPlugin_* export
			bool                       hasQuery{ false };
			std::optional<VersionData> vd;
			bool                       addressLibraryPresent{ true };
		};

		struct Verdict
		{
			Status status{ Status::no };
			Why    why{ Why::not_declared };
		};

		inline Verdict Check(const Input& a_in)
		{
			if (!a_in.hasAnyExport)
				return { Status::no, Why::not_plugin };
			if (a_in.era == Era::OG)
				return a_in.hasQuery ? Verdict{ Status::maybe, Why::og_query_decides } : Verdict{ Status::no, Why::og_no_query };
			if (!a_in.vd)
				return { Status::no, a_in.hasQuery ? Why::no_version_data_query_only : Why::no_version_data };
			const auto& v = *a_in.vd;
			// The order below is F4SE 0.7.x CheckPluginCompatibility's order.
			if (!v.dataVersion)
				return { Status::no, Why::bad_version_data };
			if (!v.hasName)
				return { Status::no, Why::no_name };
			const bool          ng = a_in.era == Era::NG;
			const std::uint32_t currentAL = ng ? kAddrAL_1_10_980 : kAddrAL_1_11_137;
			const bool          addrOk = v.addr & (kAddrSignatures | currentAL);
			const bool          structOk = v.structs & (kStructNone | (ng ? kStruct_1_10_980 : kStruct_1_11_137));
			const bool          independent = addrOk && structOk && !v.reservedBreaking;
			if ((v.addr & currentAL) && !a_in.addressLibraryPresent)
				return { Status::no, Why::address_library_missing };
			bool listed = false;
			if (!independent) {
				listed = std::ranges::find(v.compat, a_in.runtime) != v.compat.end();
				if (!listed) {
					if (!ng && (v.addr & kAddrAL_1_10_980) && !(v.addr & kAddrAL_1_11_137))
						return { Status::no, Why::ng_flags_only };
					return { Status::no, v.compat.empty() ? Why::not_declared : Why::other_builds };
				}
			}
			if (a_in.f4se && v.f4seRequired > a_in.f4se)
				return { Status::no, Why::needs_newer_f4se };
			return { Status::yes, independent ? Why::independent : Why::listed };
		}
	}

	// ---------------------------------------------------------------- popup
	// Which problems open the start-up message box (iPopupMode=1). Items are "category:subject" keys.
	//  * crash: problems that may crash the game. never muted by "Don't show".
	//  * notRunning: F4SE plugins that are not running. Muted by "Don't show".
	//  * seen: keys shown before (popup_seen.txt); muted: keys the player chose not to see again (popup_disabled.txt);
	//    mutedAll: an older popup_disabled.txt without keys (0.3.0), which mutes every not-running plugin.
	// The box opens when an item that can be shown is not in seen. Afterwards the caller stores NextSeen():
	// items that went away are forgotten, so they warn again if they come back.
	struct PopupState
	{
		std::vector<std::string> show;    // items to list in the box (empty: no box)
		bool                     open{ false };
	};

	inline PopupState DecidePopup(int a_mode, const std::vector<std::string>& a_crash, const std::vector<std::string>& a_notRunning,
		const std::set<std::string>& a_seen, const std::set<std::string>& a_muted, bool a_mutedAll)
	{
		PopupState s;
		if (a_mode <= 0)
			return s;
		for (const auto& k : a_crash)
			s.show.push_back(k);
		for (const auto& k : a_notRunning)
			if (!a_mutedAll && !a_muted.contains(k))
				s.show.push_back(k);
		if (a_mode >= 2)
			s.open = !s.show.empty();
		else
			s.open = std::ranges::any_of(s.show, [&](const std::string& k) { return !a_seen.contains(k); });
		return s;
	}

	// a_shownAndKept: the box was shown and the player did not stop the game.
	inline std::set<std::string> NextSeen(const std::set<std::string>& a_seen, const std::vector<std::string>& a_current, const PopupState& a_state, bool a_shownAndKept)
	{
		std::set<std::string> next;
		for (const auto& k : a_current)
			if (a_seen.contains(k))
				next.insert(k);
		if (a_shownAndKept)
			next.insert(a_state.show.begin(), a_state.show.end());
		return next;
	}

	// ---------------------------------------------------------------- popup key files
	// popup_seen.txt and popup_disabled.txt: a first line kKeyFileMagic, then one "category:subject" key per line,
	// written and read verbatim (a DLL name may contain spaces or ';'). Lines starting with '#' are comments; a key
	// always starts with its category, so it never does.
	// Files without the magic line come from 0.3.0 or early 0.3.1 builds: keys split on ';' and, because the 0.3.0
	// popup_disabled.txt was a sentence, words with a space were dropped. A legacy file with no key in it is the 0.3.0
	// "never show plugins" file (legacyNoKeys).
	inline constexpr std::string_view kKeyFileMagic = "# vitals-popup-keys 2";

	struct KeyFile
	{
		std::set<std::string> keys;
		bool                  versioned{ false };
		bool                  legacyNoKeys{ false };
	};

	inline KeyFile ParseKeyFile(std::string_view a_text)
	{
		KeyFile     f;
		std::size_t start = 0;
		bool        first = true;
		while (start <= a_text.size()) {
			const auto  nl = a_text.find('\n', start);
			std::string line(a_text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
			start = nl == std::string_view::npos ? a_text.size() + 1 : nl + 1;
			while (!line.empty() && (line.back() == '\r'))
				line.pop_back();
			if (first && line.starts_with("\xEF\xBB\xBF"))
				line.erase(0, 3);
			if (first && Trim(line) == kKeyFileMagic) {
				f.versioned = true;
				first = false;
				continue;
			}
			first = false;
			if (f.versioned) {
				if (!line.empty() && line[0] != '#' && line.find(':') != std::string::npos)
					f.keys.insert(line);
				continue;
			}
			for (auto& k : Split(line, ';'))
				if (!k.empty() && k.find(':') != std::string::npos && k.find(' ') == std::string::npos)
					f.keys.insert(k);
		}
		f.legacyNoKeys = !f.versioned && f.keys.empty();
		return f;
	}

	// a_comment: extra '#' lines after the magic line (each ending in '\n'), or empty.
	inline std::string FormatKeyFile(const std::set<std::string>& a_keys, std::string_view a_comment)
	{
		std::string out(kKeyFileMagic);
		out += "\n";
		out += a_comment;
		for (const auto& k : a_keys)
			if (k.find('\n') == std::string::npos && k.find('\r') == std::string::npos)
				out += k + "\n";
		return out;
	}

	// ---------------------------------------------------------------- text shown to the player
	// At most a_max bytes of a UTF-8 string, never cutting a character in half.
	inline std::string Utf8Prefix(std::string_view a_s, std::size_t a_max)
	{
		if (a_s.size() <= a_max)
			return std::string(a_s);
		std::size_t n = a_max;
		while (n > 0 && (static_cast<unsigned char>(a_s[n]) & 0xC0) == 0x80)
			--n;
		return std::string(a_s.substr(0, n));
	}

	// Replaces the Windows user name where it is a whole folder name in a path ("\Rob\", "/Rob/", or "\Rob" at the end
	// of a path), ignoring case. Never inside a word: a user called Rob must not turn "PROBLEMS" into "P<user>LEMS".
	// The profile folder itself is replaced before this, by the caller.
	inline std::string RedactUserFolder(std::string a_text, std::string_view a_user, std::string_view a_with = "<user>")
	{
		if (a_user.size() < 2)
			return a_text;
		auto lower = [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); };
		auto isSep = [](char c) { return c == '\\' || c == '/'; };
		auto ends = [](char c) { return c == '\\' || c == '/' || c == '"' || c == '\'' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';' || c == ',' || c == ')' || c == ']'; };
		std::string out;
		std::size_t i = 0;
		while (i < a_text.size()) {
			bool hit = false;
			if (isSep(a_text[i]) && a_text.size() - i - 1 >= a_user.size()) {
				hit = true;
				for (std::size_t k = 0; k < a_user.size() && hit; ++k)
					hit = lower(static_cast<unsigned char>(a_text[i + 1 + k])) == lower(static_cast<unsigned char>(a_user[k]));
				const auto after = i + 1 + a_user.size();
				hit = hit && (after == a_text.size() || ends(a_text[after]));
			}
			if (hit) {
				out += a_text[i];
				out += a_with;
				i += 1 + a_user.size();
			} else {
				out += a_text[i++];
			}
		}
		return out;
	}

	// ---------------------------------------------------------------- load order
	// The order the game really loads plugins in: the implicit files (Fallout4.esm, the DLCs, Fallout4.ccc) first, then
	// every master-flagged plugin (ESM flag, .esm, .esl) in plugins.txt order, then every other plugin in plugins.txt
	// order. An ESM-flagged .esp listed after normal .esp files still loads before them.
	// a_isMaster[i] belongs to a_order[i]; the first a_implicit entries are the implicit files. Returns indices.
	inline std::vector<std::size_t> ActualLoadOrder(std::size_t a_implicit, const std::vector<bool>& a_isMaster)
	{
		std::vector<std::size_t> out;
		for (std::size_t i = 0; i < a_isMaster.size() && i < a_implicit; ++i)
			out.push_back(i);
		for (std::size_t i = a_implicit; i < a_isMaster.size(); ++i)
			if (a_isMaster[i])
				out.push_back(i);
		for (std::size_t i = a_implicit; i < a_isMaster.size(); ++i)
			if (!a_isMaster[i])
				out.push_back(i);
		return out;
	}
}
