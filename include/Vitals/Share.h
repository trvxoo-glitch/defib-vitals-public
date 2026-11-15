#pragma once
// Share pack helpers that need no game: summaries that replace raw files (Papyrus log, Vortex manifest,
// breadcrumbs), the INI allowlist, and size caps. Plain C++20; tested in tests/cpp/rules_test.cpp.
// src/Share.cpp gathers the files, runs them through Scrub.h and writes the zip (Zip.h).

#include "Vitals/Scrub.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Vitals::Share
{
	inline constexpr std::string_view kPackFormat = "vitals-share 1";
	inline constexpr std::size_t      kMaxFileBytes = 2u << 20;  // per file: the newest part is kept
	inline constexpr std::size_t      kMaxPackBytes = 8u << 20;  // the whole pack

	// Keep the last a_max bytes, cut at a line start, with a note saying how much was dropped.
	inline std::string KeepTail(std::string a_text, std::size_t a_max)
	{
		if (a_text.size() <= a_max)
			return a_text;
		std::size_t from = a_text.size() - a_max;
		const auto  nl = a_text.find('\n', from);
		from = nl == std::string::npos ? from : nl + 1;
		return "[Vitals: " + std::to_string(from) + " older bytes left out; the newest part is kept]\n" + a_text.substr(from);
	}

	// ---------------- INI allowlist ----------------
	// Only these keys are copied from Fallout4.ini, Fallout4Prefs.ini and Fallout4Custom.ini, in this order.
	struct IniKey
	{
		const char* section;
		const char* key;
	};
	inline constexpr std::array<IniKey, 22> kIniKeys{ {
		{ "Archive", "bInvalidateOlderFiles" },
		{ "Archive", "sResourceDataDirsFinal" },
		{ "Archive", "sResourceIndexFileList" },
		{ "Archive", "sResourceStartUpArchiveList" },
		{ "Archive", "SResourceArchiveList" },
		{ "Archive", "SResourceArchiveList2" },
		{ "Archive", "SResourceArchiveMemoryCacheList" },
		{ "Papyrus", "bEnableLogging" },
		{ "Papyrus", "bEnableTrace" },
		{ "Papyrus", "bLoadDebugInformation" },
		{ "Papyrus", "fUpdateBudgetMS" },
		{ "Papyrus", "iMaxAllocatedMemoryBytes" },
		{ "General", "uGridsToLoad" },
		{ "General", "uExterior Cell Buffer" },
		{ "General", "iNumHWThreads" },
		{ "Display", "iSize W" },
		{ "Display", "iSize H" },
		{ "Display", "bFull Screen" },
		{ "Display", "bBorderless" },
		{ "Display", "iPresentInterval" },
		{ "Display", "fMaxFramerate" },
		{ "Display", "iMaxAnisotropy" },
	} };

	// ---------------- Papyrus log -> counts ----------------
	namespace detail
	{
		inline std::string Lower(std::string_view a_s)
		{
			std::string out(a_s);
			for (auto& c : out)
				c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
			return out;
		}

		// Numbers become '#' and quoted text that does not look like a name becomes <text>, so the same error from
		// different objects counts as one line and nothing typed by the player survives.
		inline std::string Normalize(std::string_view a_s)
		{
			std::string out;
			std::size_t i = 0;
			while (i < a_s.size()) {
				const char c = a_s[i];
				if (c == '"') {
					const auto e = a_s.find('"', i + 1);
					if (e != std::string_view::npos) {
						const auto inner = a_s.substr(i + 1, e - i - 1);
						out += Scrub::KeepMemoryString(inner) ? "\"" + std::string(inner) + "\"" : std::string("<text>");
						i = e + 1;
						continue;
					}
				}
				if (c == '0' && i + 1 < a_s.size() && (a_s[i + 1] == 'x' || a_s[i + 1] == 'X')) {
					i += 2;
					while (i < a_s.size() && Scrub::detail::IsHex(a_s[i]))
						++i;
					out += '#';
					continue;
				}
				// a run of hex digits that holds a digit (form ids such as 0001A4D7, plain numbers), not inside a word
				if (Scrub::detail::IsHex(c) && (i == 0 || !Scrub::detail::IsWordChar(a_s[i - 1]))) {
					std::size_t e = i;
					bool        digit = false;
					for (; e < a_s.size() && Scrub::detail::IsHex(a_s[e]); ++e)
						digit = digit || Scrub::detail::IsDigit(a_s[e]);
					if (digit && (e == a_s.size() || !Scrub::detail::IsWordChar(a_s[e]))) {
						out += '#';
						i = e;
						continue;
					}
				}
				out += c;
				++i;
			}
			return out;
		}

		inline std::string_view Trim(std::string_view a_s)
		{
			while (!a_s.empty() && (a_s.front() == ' ' || a_s.front() == '\t'))
				a_s.remove_prefix(1);
			while (!a_s.empty() && (a_s.back() == ' ' || a_s.back() == '\t' || a_s.back() == '\r'))
				a_s.remove_suffix(1);
			return a_s;
		}

		inline std::string_view StripStamp(std::string_view a_line)
		{
			if (!a_line.empty() && a_line.front() == '[') {
				const auto e = a_line.find("] ");
				if (e != std::string_view::npos && e < 40)
					a_line.remove_prefix(e + 2);
			}
			while (!a_line.empty() && (a_line.back() == '\r' || a_line.back() == ' '))
				a_line.remove_suffix(1);
			return a_line;
		}

		// "[Actor < (00000014)>].WorkshopNPCScript.OnLoad() - "WorkshopNPCScript.psc" Line 12" -> "WorkshopNPCScript.OnLoad()"
		inline std::string FrameName(std::string_view a_line)
		{
			auto s = a_line;
			while (!s.empty() && (s.front() == '\t' || s.front() == ' '))
				s.remove_prefix(1);
			if (const auto close = s.find("]."); !s.empty() && s.front() == '[' && close != std::string_view::npos)
				s.remove_prefix(close + 2);
			if (const auto dash = s.find(" - "); dash != std::string_view::npos)
				s = s.substr(0, dash);
			return Normalize(s.substr(0, 160));
		}
	}

	inline std::string PapyrusSummary(std::string_view a_log)
	{
		std::map<std::string, std::size_t> unbound, errors, warnings, missing;
		std::size_t                        lines = 0;
		std::vector<std::string_view>      all;
		for (std::size_t p = 0; p <= a_log.size();) {
			auto e = a_log.find('\n', p);
			if (e == std::string_view::npos)
				e = a_log.size();
			all.push_back(a_log.substr(p, e - p));
			p = e + 1;
		}
		for (std::size_t k = 0; k < all.size(); ++k) {
			const auto line = detail::StripStamp(all[k]);
			if (line.empty())
				continue;
			++lines;
			const auto low = detail::Lower(line.substr(0, 40));
			if (line.find("Cannot open store for class") != std::string_view::npos) {
				++missing[detail::Normalize(line)];
				continue;
			}
			const bool isError = low.starts_with("error:");
			const bool isWarning = low.starts_with("warning:");
			if (!isError && !isWarning)
				continue;
			auto body = std::string(detail::Trim(line.substr(isError ? 6 : 8)));
			// The player is form 00000014: a name printed just before it is the player character's name.
			if (const auto pl = body.find("(00000014)"); pl != std::string::npos) {
				const auto to = body.rfind(" to ", pl);
				const auto on = body.rfind(" on ", pl);
				const auto from = to == std::string::npos ? on : on == std::string::npos ? to : std::max(to, on);
				if (from != std::string::npos && pl - from <= 64)
					body = body.substr(0, from + 4) + "<player> " + body.substr(pl);
			}
			const auto msg = detail::Normalize(body);
			if (line.find("Unbound native function") != std::string_view::npos ||
				(line.find("native function") != std::string_view::npos && line.find("bound") != std::string_view::npos)) {
				++unbound[msg];
				continue;
			}
			// the first stack frame says which script hit it
			std::string where;
			for (std::size_t n = k + 1; n < all.size() && n <= k + 3; ++n) {
				const auto next = detail::StripStamp(all[n]);
				if (next.find("stack:") != std::string_view::npos)
					continue;
				if (!next.empty() && (next.front() == '\t' || next.front() == ' ')) {
					where = detail::FrameName(next);
					break;
				}
				break;
			}
			++(isError ? errors : warnings)[where.empty() ? msg : msg + "  @ " + where];
		}
		auto section = [](std::string& out, const char* title, const std::map<std::string, std::size_t>& m, std::size_t limit) {
			std::vector<std::pair<std::string, std::size_t>> v(m.begin(), m.end());
			std::ranges::stable_sort(v, [](const auto& a, const auto& b) { return a.second > b.second; });
			std::size_t total = 0;
			for (const auto& [k, n] : v)
				total += n;
			out += std::string("\n") + title + " (" + std::to_string(total) + " lines, " + std::to_string(v.size()) + " kinds)\n";
			for (std::size_t i = 0; i < v.size() && i < limit; ++i)
				out += "  " + std::to_string(v[i].second) + "x  " + v[i].first.substr(0, 400) + "\n";
			if (v.size() > limit)
				out += "  ...and " + std::to_string(v.size() - limit) + " more kinds\n";
		};
		std::string out = "Papyrus.0.log summary by Vitals: counts only, the log itself is not included. Numbers are shown as #.\n";
		out += "Lines read: " + std::to_string(lines) + "\n";
		section(out, "UNBOUND NATIVE FUNCTIONS (an F4SE plugin these scripts need is not running)", unbound, 200);
		section(out, "MISSING SCRIPTS (Cannot open store for class)", missing, 100);
		section(out, "ERRORS", errors, 150);
		section(out, "WARNINGS", warnings, 50);
		return out;
	}

	// ---------------- Vortex deployment manifest -> DLL sources ----------------
	struct VortexDlls
	{
		std::string                                      staging;  // stagingPath, used as the <staging> folder
		std::vector<std::pair<std::string, std::string>> dlls;     // F4SE\Plugins\x.dll -> mod folder name
	};

	namespace detail
	{
		// Reads a JSON string starting at the opening quote; returns the end (after the closing quote) or npos.
		inline std::size_t JsonString(std::string_view a_s, std::size_t a_at, std::string& a_out)
		{
			a_out.clear();
			if (a_at >= a_s.size() || a_s[a_at] != '"')
				return std::string_view::npos;
			for (std::size_t i = a_at + 1; i < a_s.size(); ++i) {
				const char c = a_s[i];
				if (c == '"')
					return i + 1;
				if (c != '\\') {
					a_out += c;
					continue;
				}
				if (++i >= a_s.size())
					return std::string_view::npos;
				switch (a_s[i]) {
				case 'n': a_out += '\n'; break;
				case 't': a_out += '\t'; break;
				case 'r': a_out += '\r'; break;
				case 'b': a_out += '\b'; break;
				case 'f': a_out += '\f'; break;
				case 'u': {
					if (i + 4 >= a_s.size())
						return std::string_view::npos;
					unsigned v = 0;
					for (int k = 1; k <= 4; ++k) {
						const char h = a_s[i + k];
						v = v * 16 + static_cast<unsigned>(h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : 0);
					}
					i += 4;
					if (v < 0x80) {
						a_out += static_cast<char>(v);
					} else if (v < 0x800) {
						a_out += static_cast<char>(0xC0 | (v >> 6));
						a_out += static_cast<char>(0x80 | (v & 0x3F));
					} else {  // surrogate halves are kept as-is (3 bytes each): file names only, good enough
						a_out += static_cast<char>(0xE0 | (v >> 12));
						a_out += static_cast<char>(0x80 | ((v >> 6) & 0x3F));
						a_out += static_cast<char>(0x80 | (v & 0x3F));
					}
					break;
				}
				default: a_out += a_s[i]; break;
				}
			}
			return std::string_view::npos;
		}

		// The string value of "key": in a_s[a_from, a_to), or empty.
		inline std::string JsonField(std::string_view a_s, std::string_view a_key, std::size_t a_from, std::size_t a_to)
		{
			const std::string needle = "\"" + std::string(a_key) + "\"";
			auto              k = a_s.substr(0, a_to).find(needle, a_from);
			if (k == std::string_view::npos)
				return {};
			k += needle.size();
			while (k < a_to && (a_s[k] == ' ' || a_s[k] == ':' || a_s[k] == '\t' || a_s[k] == '\r' || a_s[k] == '\n'))
				++k;
			std::string v;
			return JsonString(a_s, k, v) == std::string_view::npos ? std::string{} : v;
		}
	}

	inline VortexDlls ParseVortexManifest(std::string_view a_json)
	{
		VortexDlls out;
		const auto filesAt = a_json.find("\"files\"");
		out.staging = detail::JsonField(a_json, "stagingPath", 0, filesAt == std::string_view::npos ? a_json.size() : filesAt);
		if (filesAt == std::string_view::npos)
			return out;
		// each file entry is a flat object: {"relPath": "...", "source": "...", ...}
		for (std::size_t p = a_json.find('{', filesAt); p != std::string_view::npos; p = a_json.find('{', p + 1)) {
			const auto end = a_json.find('}', p);
			if (end == std::string_view::npos)
				break;
			auto rel = detail::JsonField(a_json, "relPath", p, end);
			std::ranges::replace(rel, '/', '\\');
			const auto low = detail::Lower(rel);
			if (low.starts_with("f4se\\plugins\\") && low.ends_with(".dll") && low.find('\\', 13) == std::string::npos)
				out.dlls.emplace_back(rel.substr(13), detail::JsonField(a_json, "source", p, end));
			p = end;
		}
		std::ranges::sort(out.dlls, [](const auto& a, const auto& b) { return detail::Lower(a.first) < detail::Lower(b.first); });
		return out;
	}

	inline std::string DllSourcesText(const VortexDlls& a_v)
	{
		std::string out = "F4SE DLLs and the Vortex mod folder that deployed each (from Data\\vortex.deployment.json).\n";
		out += "dll | mod folder\n";
		for (const auto& [dll, src] : a_v.dlls)
			out += dll + " | " + (src.empty() ? "?" : src) + "\n";
		return out;
	}

	// ---------------- breadcrumbs.bin -> text ----------------
	// Layout in docs/FILE_FORMATS.md. Only port and step names are kept; the newest a_keep entries, oldest first.
	inline std::string BreadcrumbsText(std::string_view a_bin, std::size_t a_keep = 64)
	{
		auto u32 = [&](std::size_t at) {
			std::uint32_t v = 0;
			std::memcpy(&v, a_bin.data() + at, 4);
			return v;
		};
		auto u64 = [&](std::size_t at) {
			std::uint64_t v = 0;
			std::memcpy(&v, a_bin.data() + at, 8);
			return v;
		};
		if (a_bin.size() < 64 || a_bin.substr(0, 4) != "VTLB")
			return "not a Vitals breadcrumbs file\n";
		const auto cap = u32(8), size = u32(12);
		if (size != 128 || cap == 0 || cap > 65536 || a_bin.size() < 64 + static_cast<std::size_t>(cap) * size)
			return "breadcrumbs file has an unexpected layout\n";
		auto field = [&](std::size_t at, std::size_t n) {
			std::string s(a_bin.substr(at, n));
			s.resize(std::strlen(s.c_str()));
			for (auto& c : s)
				if (static_cast<unsigned char>(c) < 0x20)
					c = '?';
			return s;
		};
		std::vector<std::pair<std::uint64_t, std::string>> rows;
		for (std::uint32_t n = 0; n < cap; ++n) {
			const std::size_t at = 64 + static_cast<std::size_t>(n) * size;
			if (const auto seq = u64(at))
				rows.emplace_back(seq, field(at + 24, 40) + " | " + field(at + 64, 64));
		}
		std::ranges::sort(rows);
		std::string out = "seq | port | where (newest last; the last line ran last)\n";
		for (std::size_t i = rows.size() > a_keep ? rows.size() - a_keep : 0; i < rows.size(); ++i)
			out += std::to_string(rows[i].first) + " | " + rows[i].second + "\n";
		return out;
	}

	// ---------------- automatic sending (opted-in players only) ----------------
	// Where packs go. Empty means "not set up": Vitals then only writes the zip, as before. Filled in once the server
	// (server/README.md) is deployed. HTTPS only.
	inline constexpr std::wstring_view kUploadHost = L"";
	inline constexpr std::wstring_view kUploadPath = L"/v1/pack";
	inline constexpr std::int64_t      kResendSeconds = 7 * 24 * 3600;  // a pack with no new crash goes at most weekly
	inline constexpr std::int64_t      kRetrySeconds = 6 * 3600;        // after a failed send

	// share-sent.txt: what was sent last, so the same thing is not sent every launch.
	struct SendState
	{
		std::int64_t             lastSent{ 0 };     // unix seconds of the last successful send
		std::int64_t             lastTry{ 0 };      // unix seconds of the last failed try
		std::string              code;              // the server's code for the last pack (deletes it)
		std::vector<std::string> crashes;           // crash log names already sent
	};

	inline SendState ParseSendState(std::string_view a_text)
	{
		SendState st;
		auto      num = [](std::string_view v) {
            std::int64_t n = 0;
            for (const char c : v) {
                if (c < '0' || c > '9')
                    return std::int64_t{ 0 };
                n = n * 10 + (c - '0');
                if (n > (std::int64_t{ 1 } << 40))
                    return std::int64_t{ 0 };
            }
            return n;
		};
		while (!a_text.empty()) {
			const auto nl = a_text.find('\n');
			auto       line = a_text.substr(0, nl);
			a_text = nl == std::string_view::npos ? std::string_view{} : a_text.substr(nl + 1);
			if (!line.empty() && line.back() == '\r')
				line.remove_suffix(1);
			const auto eq = line.find('=');
			if (eq == std::string_view::npos)
				continue;
			const auto k = line.substr(0, eq), v = line.substr(eq + 1);
			if (k == "last_sent")
				st.lastSent = num(v);
			else if (k == "last_try")
				st.lastTry = num(v);
			else if (k == "code" && v.size() == 13 && v.starts_with("VTL-"))
				st.code = std::string(v);
			else if (k == "crash" && !v.empty() && v.size() < 200)
				st.crashes.emplace_back(v);
		}
		return st;
	}

	inline std::string FormatSendState(const SendState& a_st)
	{
		std::string out = "# Vitals share pack: what was sent last (delete this file to send again at the next launch)\n";
		out += "last_sent=" + std::to_string(a_st.lastSent) + "\n";
		if (a_st.lastTry)
			out += "last_try=" + std::to_string(a_st.lastTry) + "\n";
		if (!a_st.code.empty())
			out += "code=" + a_st.code + "\n";
		for (const auto& c : a_st.crashes)
			out += "crash=" + c + "\n";
		return out;
	}

	// Why to send now, or empty to wait: never sent, a crash log not sent before, or a week since the last send.
	// A failed try waits kRetrySeconds so an offline PC does not try every launch.
	inline std::string SendReason(const SendState& a_st, std::int64_t a_now, const std::vector<std::string>& a_crashes)
	{
		if (a_st.lastTry && a_now - a_st.lastTry < kRetrySeconds && a_now >= a_st.lastTry)
			return {};
		if (a_st.lastSent == 0)
			return "first";
		for (const auto& c : a_crashes)
			if (std::ranges::find(a_st.crashes, c) == a_st.crashes.end())
				return "new_crash";
		if (a_now - a_st.lastSent >= kResendSeconds || a_now < a_st.lastSent)
			return "weekly";
		return {};
	}

	// The code from the server's reply ({"code":"VTL-XXXX-XXXX",...}), or empty.
	inline std::string ReplyCode(std::string_view a_body)
	{
		const auto at = a_body.find("\"code\"");
		if (at == std::string_view::npos)
			return {};
		const auto q = a_body.find("\"VTL-", at + 6);
		if (q == std::string_view::npos || q + 14 > a_body.size() || a_body[q + 14] != '"')
			return {};
		const auto code = a_body.substr(q + 1, 13);
		for (std::size_t i = 4; i < 13; ++i) {
			const char c = code[i];
			if (i == 8 ? c != '-' : !((c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9')))
				return {};
		}
		return std::string(code);
	}
}
