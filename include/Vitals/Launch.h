#pragma once
// Report text that compares this launch with the last one, and the rules-file bookkeeping that goes with it. Plain
// C++20 and the standard library only, like Rules.h, so tests/cpp/rules_test.cpp checks exactly what ships.
//
//  * Snapshot: what one Vitals.jsonl says about a launch (start time, game and F4SE versions, the DLL records, the
//    active plugin count, the phase records). Lines that are cut off or not JSON are skipped.
//  * Recap: "Last launch (...)" at the top of the report. It only says what the records show; it never says "crashed".
//  * Diff: "CHANGED SINCE LAST LAUNCH", DLLs compared by SHA-256, never by file date.
//  * Rule files: which file's rules win (newest "Generated:" date), the stamp line, the "patchedby" owner lookup.

#include "Vitals/Rules.h"

#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Vitals::Rules::Launch
{
	// ---------------------------------------------------------------- one line of Vitals.jsonl
	// The raw text of a field in a flat JSON object line: a string without its quotes (escapes undone for \" \\ and
	// \/ only), or a number/true/false as written. nullopt when the key is not there. Only for lines Vitals wrote.
	inline std::optional<std::string> Field(std::string_view a_line, std::string_view a_key)
	{
		const std::string needle = "\"" + std::string(a_key) + "\":";
		const auto        at = a_line.find(needle);
		if (at == std::string_view::npos)
			return std::nullopt;
		auto p = at + needle.size();
		if (p >= a_line.size())
			return std::nullopt;
		if (a_line[p] == '"') {
			std::string out;
			for (++p; p < a_line.size(); ++p) {
				const char c = a_line[p];
				if (c == '"')
					return out;
				if (c == '\\' && p + 1 < a_line.size()) {
					const char n = a_line[++p];
					out += (n == 'n' || n == 't' || n == 'r') ? ' ' : n;
				} else {
					out += c;
				}
			}
			return std::nullopt;  // no closing quote: a cut-off line
		}
		const auto end = a_line.find_first_of(",}", p);
		if (end == std::string_view::npos)
			return std::nullopt;
		return Trim(a_line.substr(p, end - p));
	}

	// "2026-11-16T14:27:03.123Z" -> seconds since 1970 (UTC). nullopt for anything else.
	inline std::optional<std::int64_t> ParseIsoUtc(std::string_view a_text)
	{
		if (a_text.size() < 19)
			return std::nullopt;
		auto num = [&](std::size_t from, std::size_t len) -> int {
			int v = 0;
			for (std::size_t i = from; i < from + len; ++i) {
				if (a_text[i] < '0' || a_text[i] > '9')
					return -1;
				v = v * 10 + (a_text[i] - '0');
			}
			return v;
		};
		const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), s = num(17, 2);
		if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 60 ||
			a_text[4] != '-' || a_text[7] != '-' || (a_text[10] != 'T' && a_text[10] != ' ') || a_text[13] != ':' || a_text[16] != ':')
			return std::nullopt;
		// days from civil (Howard Hinnant)
		const std::int64_t yy = y - (mo <= 2);
		const std::int64_t era = yy / 400;
		const std::int64_t yoe = yy - era * 400;
		const std::int64_t doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
		const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
		const std::int64_t days = era * 146097 + doe - 719468;
		return days * 86400 + h * 3600 + mi * 60 + s;
	}

	// ---------------------------------------------------------------- a launch, as its Vitals.jsonl records it
	struct Dll
	{
		std::string file, sha256, version;  // version: the plugin's own version when it was running, else the file version
	};

	struct Snapshot
	{
		bool                       valid{ false };  // a "start" line was found
		bool                       hasEnvironment{ false };
		std::string                startTime;  // ISO text of the start line
		std::string                game, f4se;
		std::vector<Dll>           dlls;
		std::optional<std::size_t> activePlugins;                           // from the "doctor" line (0.4.2 and later)
		std::vector<std::pair<std::string, double>> phases;                 // event, ms since Vitals loaded
	};

	// a_lines: the lines of a Vitals.jsonl. a_outOfTime is asked now and then; once it says true the rest is skipped.
	template <class OutOfTime>
	inline Snapshot Parse(const std::vector<std::string>& a_lines, OutOfTime&& a_outOfTime)
	{
		Snapshot s;
		std::size_t n = 0;
		for (const auto& line : a_lines) {
			if ((++n & 63) == 0 && a_outOfTime())
				break;
			if (line.size() < 8 || line.front() != '{' || line.back() != '}')
				continue;  // cut off (the game ended while it was written), or not one of ours
			const auto kind = Field(line, "kind");
			if (!kind)
				continue;
			if (*kind == "start" && !s.valid) {
				s.valid = true;
				s.startTime = Field(line, "t").value_or("");
			} else if (*kind == "environment") {
				s.hasEnvironment = true;
				s.game = Field(line, "game_version").value_or("");
				s.f4se = Field(line, "f4se_version").value_or("");
			} else if (*kind == "plugin") {
				Dll d;
				d.file = Field(line, "file").value_or("");
				d.sha256 = Field(line, "sha256").value_or("");
				d.version = Field(line, "plugin_version").value_or("");
				if (d.version.empty())
					d.version = Field(line, "file_version").value_or("");
				if (!d.file.empty())
					s.dlls.push_back(std::move(d));
			} else if (*kind == "doctor") {
				if (const auto p = Field(line, "plugins_active"); p && !p->empty() && p->find_first_not_of("0123456789") == std::string::npos && p->size() < 9)
					s.activePlugins = static_cast<std::size_t>(std::strtoull(p->c_str(), nullptr, 10));
			} else if (*kind == "phase") {
				const auto ev = Field(line, "event"), ms = Field(line, "ms_since_vitals_load");
				if (ev && ms && !ms->empty())
					s.phases.emplace_back(*ev, std::strtod(ms->c_str(), nullptr));
			}
		}
		return s;
	}

	// ---------------------------------------------------------------- 1: last-launch recap
	struct CrashLog
	{
		std::string name;
		std::int64_t minutesAfterStart{ 0 };
	};

	// The newest crash*.log (any case) written at or after a_startEpoch. a_files: name and last-write time (epoch seconds).
	inline std::optional<CrashLog> CrashLogAfter(std::int64_t a_startEpoch, const std::vector<std::pair<std::string, std::int64_t>>& a_files)
	{
		std::optional<CrashLog> best;
		std::int64_t            bestTime = 0;
		for (const auto& [name, when] : a_files) {
			std::string low(name);
			for (auto& c : low)
				c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
			if (!low.starts_with("crash") || !low.ends_with(".log") || when < a_startEpoch)
				continue;
			if (!best || when > bestTime) {
				bestTime = when;
				best = CrashLog{ name, (when - a_startEpoch) / 60 };
			}
		}
		return best;
	}

	// One paragraph for the top of the report; empty when there is no earlier launch to talk about.
	inline std::string Recap(const Snapshot& a_prev, const std::optional<CrashLog>& a_crash)
	{
		if (!a_prev.valid)
			return {};
		std::string when = a_prev.startTime.size() >= 16 ? a_prev.startTime.substr(0, 16) : a_prev.startTime;
		if (when.size() > 10 && when[10] == 'T')
			when[10] = ' ';
		auto seconds = [&](std::string_view a_event) -> std::optional<long> {
			for (const auto& [ev, ms] : a_prev.phases)
				if (ev == a_event)
					return static_cast<long>(ms / 1000.0 + 0.5);
			return std::nullopt;
		};
		const auto ready = seconds("GameDataReady"), loaded = seconds("PostLoadGame");
		bool       clean = false;
		for (const auto& p : a_prev.phases)
			clean = clean || p.first == "DeleteGame";
		std::string text = "Last launch (" + when + " UTC): ";
		std::vector<std::string> parts;
		if (ready)
			parts.push_back("game data was ready (the main menu) after " + std::to_string(*ready) + " s");
		if (loaded)
			parts.push_back("a save loaded after " + std::to_string(*loaded) + " s");
		if (clean)
			parts.push_back("the game closed normally");
		if (parts.empty()) {
			text += "Vitals saw no game event.";
		} else {
			for (std::size_t i = 0; i < parts.size(); ++i)
				text += (i ? ", " : "") + parts[i];
			text += ".";
			if (!clean)
				text += " Vitals saw no later event.";
		}
		if (a_crash)
			text += a_crash->minutesAfterStart < 1
						? " A crash log was written less than a minute after that launch started: " + a_crash->name + "."
						: " A crash log was written " + std::to_string(a_crash->minutesAfterStart) + " minute" + (a_crash->minutesAfterStart == 1 ? "" : "s") +
							  " after that launch started: " + a_crash->name + ".";
		return text;
	}

	// ---------------------------------------------------------------- 2: changed since the last launch
	// Lines for the report section. Empty when the earlier launch cannot be compared (no records of its DLLs).
	// a_maxEntries caps the DLL lines; the rest become "and N more".
	inline std::vector<std::string> Changes(const Snapshot& a_prev, const Snapshot& a_now, std::size_t a_maxEntries = 10)
	{
		std::vector<std::string> out;
		if (!a_prev.valid || !a_prev.hasEnvironment || !a_now.hasEnvironment)
			return out;
		auto lower = [](std::string s) {
			for (auto& c : s)
				c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
			return s;
		};
		std::map<std::string, const Dll*> before, after;
		for (const auto& d : a_prev.dlls)
			before.emplace(lower(d.file), &d);
		for (const auto& d : a_now.dlls)
			after.emplace(lower(d.file), &d);
		auto shown = [](const Dll& d) { return d.file + (d.version.empty() ? "" : " " + d.version); };
		std::vector<std::string> replaced, added, removed;
		for (const auto& [key, now] : after) {
			const auto it = before.find(key);
			if (it == before.end()) {
				added.push_back("New: " + shown(*now));
			} else if (!it->second->sha256.empty() && !now->sha256.empty() && it->second->sha256 != now->sha256) {
				const bool sameVersion = it->second->version == now->version;
				replaced.push_back("Replaced: " + now->file + " (" +
								   (sameVersion ? (now->version.empty() ? "different file" : "version " + now->version + ", different file")
												: (it->second->version.empty() ? "?" : it->second->version) + " -> " + (now->version.empty() ? "?" : now->version)) +
								   ")");
			}
		}
		for (const auto& [key, was] : before)
			if (!after.contains(key))
				removed.push_back("Removed: " + was->file + (was->version.empty() ? "" : " " + was->version));
		std::vector<std::string> all;
		for (auto* v : { &replaced, &added, &removed })
			all.insert(all.end(), v->begin(), v->end());
		for (std::size_t i = 0; i < all.size() && i < a_maxEntries; ++i)
			out.push_back(all[i]);
		if (all.size() > a_maxEntries)
			out.push_back("and " + std::to_string(all.size() - a_maxEntries) + " more");

		const bool gameKnown = !a_prev.game.empty() && !a_now.game.empty(), f4seKnown = !a_prev.f4se.empty() && !a_now.f4se.empty();
		bool       otherChange = false;
		if (gameKnown && a_prev.game != a_now.game) {
			out.push_back("The game changed: " + a_prev.game + " -> " + a_now.game);
			otherChange = true;
		}
		if (f4seKnown && a_prev.f4se != a_now.f4se) {
			out.push_back("F4SE changed: " + a_prev.f4se + " -> " + a_now.f4se);
			otherChange = true;
		}
		if (a_prev.activePlugins && a_now.activePlugins && *a_prev.activePlugins != *a_now.activePlugins) {
			out.push_back("Active plugins: " + std::to_string(*a_prev.activePlugins) + " -> " + std::to_string(*a_now.activePlugins));
			otherChange = true;
		}
		if (all.empty() && !otherChange)
			out.insert(out.begin(), "Nothing changed since the last launch.");
		if (gameKnown && f4seKnown && a_prev.game == a_now.game && a_prev.f4se == a_now.f4se && !all.empty())
			out.push_back("The game and F4SE are the same as last launch.");
		return out;
	}

	// ---------------------------------------------------------------- rule files
	struct RuleFile
	{
		std::string                name;
		std::optional<std::string> generated;  // "YYYY-MM-DD" from a "# Generated: ..." header line
		std::size_t                used{ 0 };  // rules taken from it, for the stamp
	};

	// "# Generated: 2026-11-17" (any case, "Generated on" too) in a comment line; the first one wins. Cap: 40 lines.
	inline std::optional<std::string> GeneratedDate(const std::vector<std::string>& a_lines)
	{
		std::size_t n = 0;
		for (const auto& raw : a_lines) {
			if (++n > 40)
				break;
			auto line = Trim(raw);
			if (line.empty() || (line[0] != '#' && line[0] != ';'))
				continue;
			line = Trim(std::string_view(line).substr(1));
			std::string low = line;
			for (auto& c : low)
				c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
			if (!low.starts_with("generated"))
				continue;
			auto p = line.find_first_of("0123456789", 9);  // "Generated: " or "Generated on ", then the date
			if (p == std::string::npos || p > 15)
				continue;
			if (line.size() < p + 10)
				continue;
			const auto date = line.substr(p, 10);
			bool       ok = date[4] == '-' && date[7] == '-';
			for (std::size_t i = 0; i < 10 && ok; ++i)
				ok = (i == 4 || i == 7) || (date[i] >= '0' && date[i] <= '9');
			const int month = ok ? std::stoi(date.substr(5, 2)) : 0, day = ok ? std::stoi(date.substr(8, 2)) : 0;
			if (ok && month >= 1 && month <= 12 && day >= 1 && day <= 31)
				return date;
		}
		return std::nullopt;
	}

	// The order the rule files are read in; a rule that appears in two files is taken from the first one read.
	// Files with no date come first, in name order (as before 0.4.2: a file a player wrote themselves is not outranked by
	// a generated one). Then the dated files, newest date first, equal dates in name order. Returns indices into a_files.
	inline std::vector<std::size_t> RuleFileOrder(const std::vector<RuleFile>& a_files)
	{
		std::vector<std::size_t> order(a_files.size());
		for (std::size_t i = 0; i < order.size(); ++i)
			order[i] = i;
		std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
			const auto &fa = a_files[a], &fb = a_files[b];
			if (fa.generated.has_value() != fb.generated.has_value())
				return !fa.generated.has_value();
			if (fa.generated && *fa.generated != *fb.generated)
				return *fa.generated > *fb.generated;
			return fa.name < fb.name;
		});
		return order;
	}

	// "Knowledge rules: 16 from defib-census.txt (generated 2026-11-17), 6 from defib-known.txt" in read order.
	inline std::string RulesStamp(const std::vector<RuleFile>& a_files, const std::vector<std::size_t>& a_order)
	{
		std::string out;
		std::size_t total = 0;
		for (const auto i : a_order) {
			const auto& f = a_files[i];
			if (!f.used)
				continue;
			total += f.used;
			out += (out.empty() ? "" : ", ") + std::to_string(f.used) + " from " + f.name + (f.generated ? " (generated " + *f.generated + ")" : "");
		}
		return "Knowledge rules: " + (out.empty() ? std::string("none loaded") : out) + (total && a_order.size() > 1 ? " (the first file listed wins when two files have the same rule)" : "");
	}

	// ---------------------------------------------------------------- 5: who normally patches a function
	struct Owner
	{
		std::string family, dll;  // family: part of a function name, any case
	};

	// The first rule whose family is part of a_function and whose DLL a_running says is running.
	inline std::optional<std::string> PatchOwner(const std::vector<Owner>& a_owners, std::string_view a_function, const std::function<bool(const std::string&)>& a_running)
	{
		auto lower = [](std::string_view s) {
			std::string o(s);
			for (auto& c : o)
				c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
			return o;
		};
		const auto fn = lower(a_function);
		for (const auto& o : a_owners)
			if (!o.family.empty() && fn.find(lower(o.family)) != std::string::npos && a_running(o.dll))
				return o.dll;
		return std::nullopt;
	}

	// ---------------------------------------------------------------- 3: a rule that names one build of a DLL
	// F4SE packs a plugin version as major<<24 | minor<<16 | build<<4 | sub; "0.0.4.13" is major 0, minor 0, build 4, sub 13.
	inline std::string PackedVersionText(std::uint32_t a_packed)
	{
		return std::to_string((a_packed >> 24) & 0xFF) + "." + std::to_string((a_packed >> 16) & 0xFF) + "." + std::to_string((a_packed >> 4) & 0xFFF) + "." + std::to_string(a_packed & 0xF);
	}

	// "dllbuild | SUP_F4SE.dll | 0.0.4.13 | ...": true when the DLL's plugin version is that one. A shorter rule version
	// ("0.0.4") matches every sub-build of it. Only a version the DLL reports about itself counts, never a file date.
	inline bool DllBuildMatches(std::string_view a_ruleVersion, std::string_view a_pluginVersion)
	{
		const auto want = Split(a_ruleVersion, '.'), have = Split(a_pluginVersion, '.');
		if (want.empty() || want.size() > have.size() || want.size() > 4)
			return false;
		for (std::size_t i = 0; i < want.size(); ++i)
			if (want[i].empty() || want[i] != have[i])
				return false;
		return true;
	}
}
