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
	inline std::string TailNote(std::size_t a_dropped)
	{
		return "[Vitals: " + std::to_string(a_dropped) + " older bytes left out; the newest part is kept]\n";
	}

	inline std::string KeepTail(std::string a_text, std::size_t a_max)
	{
		if (a_text.size() <= a_max)
			return a_text;
		std::size_t from = a_text.size() - a_max;
		const auto  nl = a_text.find('\n', from);
		from = nl == std::string::npos ? from : nl + 1;
		return TailNote(from) + a_text.substr(from);
	}

	// The end of a file read from byte a_from on (the bytes before it were not read): cut at the first line start, and
	// say how many bytes of the FILE were left out.
	inline std::string FileTail(std::string a_read, std::size_t a_from)
	{
		if (a_from == 0)
			return a_read;
		const auto nl = a_read.find('\n');
		const auto cut = nl == std::string::npos ? 0 : nl + 1;
		return TailNote(a_from + cut) + a_read.substr(cut);
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
						const auto inner = a_s.substr(i + 1, e - i - 1);  // Papyrus quotes script, object and function names
						out += Scrub::KeepMemoryString(inner, true) ? "\"" + std::string(inner) + "\"" : std::string("<text>");
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

	namespace detail
	{
		// The player is form 00000014, and Papyrus prints a reference's name just before its form id, in any sentence
		// ("attached to Nora Hale (00000014)", "[Actor <Nora (00000014)>]", "Nate (00000014) is not..."). The word just
		// before is always taken; earlier words only while they start with a capital (a name), up to 64 characters.
		inline std::string HidePlayerName(std::string a_body)
		{
			auto stop = [](char c) { return c == '<' || c == '[' || c == '(' || c == '"' || c == ',' || c == ':' || c == '>' || c == ']' || c == '\'' || c == '='; };
			for (auto pl = a_body.find("(00000014)"); pl != std::string::npos; pl = a_body.find("(00000014)", pl + 10)) {
				std::size_t end = pl;
				while (end > 0 && a_body[end - 1] == ' ')
					--end;
				std::size_t start = end, pos = end;
				for (bool first = true;; first = false) {
					std::size_t w = pos;
					while (w > 0 && a_body[w - 1] != ' ' && !stop(a_body[w - 1]))
						--w;
					if (w == pos || end - w > 64 || (!first && !(a_body[w] >= 'A' && a_body[w] <= 'Z')))
						break;
					start = w;
					if (w >= 2 && a_body[w - 1] == ' ' && a_body[w - 2] != ' ' && !stop(a_body[w - 2]))
						pos = w - 1;  // one space, then another word: maybe more of the name
					else
						break;
				}
				while (start < end && a_body[start] == ' ')
					++start;
				if (start < end) {
					a_body.replace(start, end - start, "<player>");
					pl = start + 8 + (pl - end);
				}
			}
			return a_body;
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
			body = detail::HidePlayerName(std::move(body));
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
		std::string out = "Papyrus log summary by Vitals: counts only, the log itself is not included. Numbers are shown as #.\n";
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
		std::string skipped;
		for (std::size_t p = a_json.find('{', filesAt); p != std::string_view::npos; p = a_json.find('{', p + 1)) {
			// the end of this entry, stepping over strings: a '}' inside a mod name is not the end
			std::size_t end = std::string_view::npos;
			for (std::size_t q = p + 1; q < a_json.size();) {
				if (a_json[q] == '"') {
					q = detail::JsonString(a_json, q, skipped);
					if (q == std::string_view::npos)
						break;
					continue;
				}
				if (a_json[q] == '}') {
					end = q;
					break;
				}
				++q;
			}
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
}
