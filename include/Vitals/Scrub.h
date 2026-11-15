#pragma once
// Vitals share-pack scrubber: removes personal details from text before it goes into Vitals-Share.zip.
// Plain C++20 and the standard library only, like Rules.h, so tests/cpp/rules_test.cpp checks exactly what ships.
// server/scrub.js is a line-by-line port used for the server's second pass; tests/scrub_cases.txt feeds the same
// cases to both, so they cannot drift apart.
//
// Order matters (each step sees the previous step's output):
//   1. known folders (game, Documents, AppData, mod manager staging)  -> <game> <docs> <appdata> <localappdata> <staging>
//   2. the Windows user name as a folder name, any \Users\<name> or /home/<name> -> <user>
//   3. Mod Organizer 2 profile names (\profiles\<name>\)              -> <profile>
//   4. Steam account ids (userdata\<n>, 7656119xxxxxxxxxx)            -> <account>
//   5. Windows SIDs and GUIDs                                         -> <sid> <guid>
//   6. email addresses                                                -> <email>
//   7. key=value secrets (apikey, token, password...), Bearer tokens  -> <secret>
//   8. long random-looking tokens (API keys)                          -> <secret>
//   9. IPv4 (not version numbers), IPv6 and MAC addresses             -> <ip> <mac>
//  10. the user name and PC name as whole words                       -> <user> <pc>
//  11. any other full path keeps only its last two names              -> <path>\Folder\File.dll
// Crash logs also go through CrashMemoryStrings first: quoted text read from game memory is replaced with <text>
// unless it looks like a file, form or script name.
//
// Mod and plugin file names stay: they are the point of the pack. SHA-256 hashes (64 hex digits) stay.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Vitals::Scrub
{
	struct Context
	{
		std::string user;  // Windows user name (USERNAME)
		std::string pc;    // COMPUTERNAME
		std::string domain;  // USERDOMAIN (ignored when equal to the PC name)
		// Known folders, longest first is not required: they are sorted here. {"D:\\Games\\Fallout 4", "<game>"}
		std::vector<std::pair<std::string, std::string>> roots;
	};

	using Counts = std::map<std::string, std::size_t>;

	namespace detail
	{
		inline bool IsAlnum(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
		inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
		inline bool IsHex(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
		inline bool IsUpper(char c) { return c >= 'A' && c <= 'Z'; }
		inline bool IsLower(char c) { return c >= 'a' && c <= 'z'; }
		inline bool IsAlpha(char c) { return IsUpper(c) || IsLower(c); }
		inline bool IsSep(char c) { return c == '\\' || c == '/'; }
		inline char Low(char c) { return IsUpper(c) ? static_cast<char>(c + 32) : c; }
		inline bool IsWordChar(char c) { return IsAlnum(c) || c == '_'; }

		inline bool EqI(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
				return false;
			for (std::size_t i = 0; i < a.size(); ++i)
				if (Low(a[i]) != Low(b[i]))
					return false;
			return true;
		}

		inline bool StartsI(std::string_view s, std::size_t at, std::string_view what)
		{
			return at + what.size() <= s.size() && EqI(s.substr(at, what.size()), what);
		}

		// Ends a name inside a path (a folder or user name).
		inline bool NameEnd(char c)
		{
			return IsSep(c) || c == '"' || c == '\'' || c == '\r' || c == '\n' || c == '\t' || c == ';' || c == ',' || c == ')' ||
			       c == ']' || c == '<' || c == '>' || c == '|';
		}

		// Folder variants as they appear in text: as written, with '/' and with JSON-escaped '\\'.
		inline std::vector<std::string> RootVariants(const std::string& a_root)
		{
			std::vector<std::string> v{ a_root };
			std::string slash = a_root, json;
			for (auto& c : slash)
				if (c == '\\')
					c = '/';
			for (const char c : a_root)
				json += c == '\\' ? std::string("\\\\") : std::string(1, c);
			for (const auto& s : { slash, json })
				if (s != a_root)
					v.push_back(s);
			return v;
		}
	}

	// 1. Known folders. A folder matches only where the next character cannot continue its last name.
	inline std::string Roots(std::string_view a_in, const Context& a_ctx, Counts& a_n)
	{
		using namespace detail;
		std::vector<std::pair<std::string, std::string>> all;
		for (const auto& [root, token] : a_ctx.roots) {
			std::string r = root;
			while (r.size() > 3 && IsSep(r.back()))
				r.pop_back();
			if (r.size() < 4)  // never "C:\" alone
				continue;
			for (auto& v : RootVariants(r))
				all.emplace_back(std::move(v), token);
		}
		std::ranges::stable_sort(all, [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			bool hit = false;
			for (const auto& [root, token] : all) {
				if (!StartsI(a_in, i, root))
					continue;
				const auto after = i + root.size();
				// "D:\Games\Fallout 4 Backup" is another folder
				if (after < a_in.size() && (IsWordChar(a_in[after]) || a_in[after] == '-' ||
											   (a_in[after] == ' ' && after + 1 < a_in.size() && IsAlnum(a_in[after + 1]))))
					continue;
				out += token;
				i = after;
				++a_n["folder"];
				hit = true;
				break;
			}
			if (!hit)
				out += a_in[i++];
		}
		return out;
	}

	// 2. User folder names: the known user name after any separator, and the name after \Users\ or /home/.
	inline std::string UserFolders(std::string_view a_in, const Context& a_ctx, Counts& a_n)
	{
		using namespace detail;
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			const char c = a_in[i];
			if (!IsSep(c)) {
				out += a_in[i++];
				continue;
			}
			// a run of separators (JSON doubles them)
			std::size_t j = i;
			while (j < a_in.size() && IsSep(a_in[j]))
				++j;
			out.append(a_in.substr(i, j - i));
			// the known user name as a whole folder name
			if (a_ctx.user.size() >= 2 && StartsI(a_in, j, a_ctx.user)) {
				const auto after = j + a_ctx.user.size();
				if (after == a_in.size() || NameEnd(a_in[after]) || a_in[after] == ' ') {
					out += "<user>";
					++a_n["user"];
					i = after;
					continue;
				}
			}
			// \Users\<name> and /home/<name>
			std::size_t k = 0;
			if (StartsI(a_in, j, "Users"))
				k = j + 5;
			else if (StartsI(a_in, j, "home"))
				k = j + 4;
			if (k && k < a_in.size() && IsSep(a_in[k])) {
				std::size_t s = k;
				while (s < a_in.size() && IsSep(a_in[s]))
					++s;
				std::size_t e = s;
				while (e < a_in.size() && !NameEnd(a_in[e]))
					++e;
				const auto name = a_in.substr(s, e - s);
				const bool keep = name.empty() || name.front() == '<' || name.front() == '%' || EqI(name, "Public") ||
				                  EqI(name, "Default") || EqI(name, "Default User") || EqI(name, "All Users");
				out.append(a_in.substr(j, s - j));
				if (keep) {
					out.append(name);
				} else {
					out += "<user>";
					++a_n["user"];
				}
				i = e;
				continue;
			}
			i = j;
		}
		return out;
	}

	// 3. Mod Organizer 2 profile names.
	inline std::string Profiles(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			if (IsSep(a_in[i]) && StartsI(a_in, i + 1, "profiles") && i + 9 < a_in.size() && IsSep(a_in[i + 9])) {
				std::size_t s = i + 9;
				while (s < a_in.size() && IsSep(a_in[s]))
					++s;
				// profile names often hold an apostrophe or spaces ("Robin's Survival Run")
				std::size_t e = s;
				while (e < a_in.size() && !IsSep(a_in[e]) && a_in[e] != '"' && a_in[e] != '\r' && a_in[e] != '\n' && a_in[e] != '\t' &&
					   a_in[e] != '|' && a_in[e] != '<' && a_in[e] != '>')
					++e;
				out.append(a_in.substr(i, s - i));
				// only a folder (followed by a separator): "profiles\Default\plugins.txt"
				if (e > s && e < a_in.size() && IsSep(a_in[e]) && a_in[s] != '<') {
					out += "<profile>";
					++a_n["profile"];
				} else {
					out.append(a_in.substr(s, e - s));
				}
				i = e;
				continue;
			}
			out += a_in[i++];
		}
		return out;
	}

	// 4. Steam account ids.
	inline std::string SteamIds(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			// userdata\123456
			if (StartsI(a_in, i, "userdata") && i + 8 < a_in.size() && IsSep(a_in[i + 8])) {
				std::size_t s = i + 8;
				while (s < a_in.size() && IsSep(a_in[s]))
					++s;
				std::size_t e = s;
				while (e < a_in.size() && IsDigit(a_in[e]))
					++e;
				if (e > s && (e == a_in.size() || !IsAlnum(a_in[e]))) {
					out.append(a_in.substr(i, s - i));
					out += "<account>";
					++a_n["account"];
					i = e;
					continue;
				}
			}
			// SteamID64: 7656119 + 10 digits
			if (a_in[i] == '7' && (i == 0 || !IsAlnum(a_in[i - 1])) && a_in.substr(i, 7) == "7656119") {
				std::size_t e = i;
				while (e < a_in.size() && IsDigit(a_in[e]))
					++e;
				if (e - i == 17 && (e == a_in.size() || !IsAlnum(a_in[e]))) {
					out += "<account>";
					++a_n["account"];
					i = e;
					continue;
				}
			}
			out += a_in[i++];
		}
		return out;
	}

	// 5. Windows SIDs (S-1-5-21-a-b-c[-rid]) and GUIDs (8-4-4-4-12 hex digits).
	inline std::string SidsAndGuids(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		std::string out;
		std::size_t i = 0;
		auto hexRun = [&](std::size_t at, std::size_t n) {
			if (at + n > a_in.size())
				return false;
			for (std::size_t k = 0; k < n; ++k)
				if (!IsHex(a_in[at + k]))
					return false;
			return true;
		};
		while (i < a_in.size()) {
			const bool boundary = i == 0 || !IsAlnum(a_in[i - 1]);
			if (boundary && a_in.substr(i, 9) == "S-1-5-21-") {
				std::size_t e = i + 9, parts = 0;
				while (e < a_in.size() && IsDigit(a_in[e])) {
					while (e < a_in.size() && IsDigit(a_in[e]))
						++e;
					++parts;
					if (e + 1 < a_in.size() && a_in[e] == '-' && IsDigit(a_in[e + 1]))
						++e;
					else
						break;
				}
				if (parts >= 3) {
					out += "<sid>";
					++a_n["sid"];
					i = e;
					continue;
				}
			}
			if (boundary && hexRun(i, 8) && i + 36 <= a_in.size() && a_in[i + 8] == '-' && hexRun(i + 9, 4) && a_in[i + 13] == '-' &&
				hexRun(i + 14, 4) && a_in[i + 18] == '-' && hexRun(i + 19, 4) && a_in[i + 23] == '-' && hexRun(i + 24, 12) &&
				(i + 36 == a_in.size() || !IsAlnum(a_in[i + 36]))) {
				out += "<guid>";
				++a_n["guid"];
				i += 36;
				continue;
			}
			out += a_in[i++];
		}
		return out;
	}

	// 6. Email addresses.
	inline std::string Emails(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		auto isLocal = [](char c) { return IsAlnum(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-'; };
		auto isDomain = [](char c) { return IsAlnum(c) || c == '.' || c == '-'; };
		std::string out;
		std::size_t done = 0;  // a_in[0, done) is already in out
		for (std::size_t at = 0; at < a_in.size(); ++at) {
			if (a_in[at] != '@' || at < done)
				continue;
			std::size_t s = at;
			while (s > done && isLocal(a_in[s - 1]))
				--s;
			std::size_t e = at + 1;
			while (e < a_in.size() && isDomain(a_in[e]))
				++e;
			while (e > at + 1 && (a_in[e - 1] == '.' || a_in[e - 1] == '-'))
				--e;
			const auto domain = a_in.substr(at + 1, e - at - 1);
			const auto dot = domain.rfind('.');
			bool ok = s < at && dot != std::string_view::npos && dot > 0 && domain.size() - dot - 1 >= 2;
			if (ok)
				for (std::size_t k = dot + 1; k < domain.size(); ++k)
					ok = ok && IsAlpha(domain[k]);
			if (!ok)
				continue;
			out.append(a_in.substr(done, s - done));
			out += "<email>";
			++a_n["email"];
			done = e;
			at = e - 1;
		}
		out.append(a_in.substr(done));
		return out;
	}

	// 7. key=value secrets and Bearer tokens.
	inline std::string KeyValueSecrets(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		static constexpr std::array<std::string_view, 12> kKeys{ "apikey", "api_key", "api-key", "access_token", "refresh_token",
			"token", "password", "passwd", "secret", "client_secret", "authorization", "bearer" };
		auto valueEnd = [](char c) { return c == ' ' || c == '"' || c == '\'' || c == '&' || c == ',' || c == ';' || c == '\r' || c == '\n' || c == '\t' || c == '}' || c == ')'; };
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			bool hit = false;
			if (i == 0 || !IsWordChar(a_in[i - 1])) {
				for (const auto key : kKeys) {
					if (!StartsI(a_in, i, key))
						continue;
					std::size_t p = i + key.size();
					if (p < a_in.size() && (IsWordChar(a_in[p]) || a_in[p] == '-'))
						continue;
					const bool bearer = key == "bearer";
					// key, optional closing quote and spaces, then '=' or ':' (Bearer: a space)
					while (p < a_in.size() && (a_in[p] == '"' || a_in[p] == '\''))
						++p;
					while (p < a_in.size() && a_in[p] == ' ')
						++p;
					if (!bearer) {
						if (p >= a_in.size() || (a_in[p] != '=' && a_in[p] != ':'))
							continue;
						++p;
						while (p < a_in.size() && (a_in[p] == ' ' || a_in[p] == '"' || a_in[p] == '\''))
							++p;
						// "Authorization: Bearer x": let the bearer rule take it
						if (StartsI(a_in, p, "bearer "))
							continue;
					} else if (p == i + key.size()) {
						continue;  // "Bearer" needs a space before its token
					}
					std::size_t e = p;
					while (e < a_in.size() && !valueEnd(a_in[e]))
						++e;
					if (e - p < 4 || a_in[p] == '<')
						continue;
					out.append(a_in.substr(i, p - i));
					out += "<secret>";
					++a_n["secret"];
					i = e;
					hit = true;
					break;
				}
			}
			if (!hit)
				out += a_in[i++];
		}
		return out;
	}

	// 8. Long random-looking tokens: 32+ characters of [A-Za-z0-9+/=_-] with at least 4 upper, 4 lower and 4 digits,
	// high entropy, not all hex (SHA-256 hashes stay). Words joined by '_' have far less entropy.
	inline bool LooksRandom(std::string_view a_tok)
	{
		using namespace detail;
		if (a_tok.size() < 32)
			return false;
		std::size_t up = 0, lo = 0, dg = 0, hex = 0;
		std::array<std::size_t, 256> freq{};
		for (const char c : a_tok) {
			up += IsUpper(c);
			lo += IsLower(c);
			dg += IsDigit(c);
			hex += IsHex(c) || c == '-';
			++freq[static_cast<unsigned char>(c)];
		}
		if (hex == a_tok.size() || up < 4 || lo < 4 || dg < 4)
			return false;
		double h = 0;
		for (const auto f : freq)
			if (f) {
				const double p = static_cast<double>(f) / static_cast<double>(a_tok.size());
				h -= p * std::log2(p);
			}
		return h >= 4.0;
	}

	inline std::string LongTokens(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		auto isTok = [](char c) { return IsAlnum(c) || c == '+' || c == '/' || c == '=' || c == '_' || c == '-'; };
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			if (!isTok(a_in[i])) {
				out += a_in[i++];
				continue;
			}
			std::size_t e = i;
			while (e < a_in.size() && isTok(a_in[e]))
				++e;
			const auto tok = a_in.substr(i, e - i);
			// plugin, archive and library names (ccFSVFO4001-ModularMilitaryBackpack.esl) look random but are the data we want
			auto fileName = [&] {
				if (e + 4 > a_in.size() || a_in[e] != '.')
					return false;
				for (const char* ext : { "esl", "esp", "esm", "ba2", "dll" })
					if (EqI(a_in.substr(e + 1, 3), ext) && (e + 4 == a_in.size() || !IsAlnum(a_in[e + 4])))
						return true;
				return false;
			};
			if (LooksRandom(tok) && !fileName()) {
				out += "<secret>";
				++a_n["secret"];
			} else {
				out.append(tok);
			}
			i = e;
		}
		return out;
	}

	// 9. IP and MAC addresses. IPv4 needs a first number of 10 or more and no "version" just before it, so game and
	// plugin versions such as 1.11.240.0 or "file_version":"2.3.1.0" stay.
	inline std::string Addresses(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		std::string out;
		std::size_t i = 0;
		auto versionBefore = [&](std::size_t at) {
			const std::size_t from = at > 24 ? at - 24 : 0;
			const auto        win = a_in.substr(from, at - from);
			for (std::size_t k = 0; k + 7 <= win.size(); ++k)
				if (EqI(win.substr(k, 7), "version"))
					return true;
			return false;
		};
		while (i < a_in.size()) {
			const char c = a_in[i];
			const char prev = i ? a_in[i - 1] : ' ';
			// IPv4
			if (IsDigit(c) && !IsAlnum(prev) && prev != '.' && prev != '_' && prev != '-') {
				std::array<int, 4> oct{};
				std::size_t       p = i, n = 0;
				bool              ok = true;
				while (n < 4) {
					const std::size_t s = p;
					int               v = 0;
					while (p < a_in.size() && IsDigit(a_in[p]) && p - s < 4)
						v = v * 10 + (a_in[p++] - '0');
					if (p == s || p - s > 3 || v > 255) {
						ok = false;
						break;
					}
					oct[n++] = v;
					if (n < 4) {
						if (p + 1 < a_in.size() && a_in[p] == '.' && IsDigit(a_in[p + 1]))
							++p;
						else {
							ok = false;
							break;
						}
					}
				}
				const bool tailOk = p == a_in.size() || (!IsAlnum(a_in[p]) && !(a_in[p] == '.' && p + 1 < a_in.size() && IsDigit(a_in[p + 1])));
				if (ok && tailOk && oct[0] >= 10 && !versionBefore(i)) {
					out += "<ip>";
					++a_n["ip"];
					i = p;
					continue;
				}
			}
			// IPv6 / MAC: a run of hex digits and ':' (or MAC with '-')
			if (IsHex(c) && !IsWordChar(prev) && prev != ':' && prev != '.') {
				std::size_t e = i;
				while (e < a_in.size() && (IsHex(a_in[e]) || a_in[e] == ':'))
					++e;
				const bool endOk = e == a_in.size() || (!IsWordChar(a_in[e]) && !(a_in[e] == '.' && e + 1 < a_in.size() && IsHex(a_in[e + 1])));
				if (endOk) {
					std::vector<std::string_view> groups;
					std::size_t                   colons = 0, s = i;
					for (std::size_t k = i; k <= e; ++k)
						if (k == e || a_in[k] == ':') {
							if (k > s)
								groups.push_back(a_in.substr(s, k - s));
							colons += k < e;
							s = k + 1;
						}
					bool        groupsOk = !groups.empty(), letter = false, len2 = groups.size() == 6, long3 = false;
					for (const auto g : groups) {
						groupsOk = groupsOk && g.size() <= 4;
						len2 = len2 && g.size() == 2;
						long3 = long3 || g.size() >= 3;
						for (const char h : g)
							letter = letter || IsAlpha(h);
					}
					const bool mac = len2 && colons == 5;
					const bool v6 = groupsOk && colons >= 2 && groups.size() >= 3 && letter && long3;
					if (mac || v6) {
						out += mac ? "<mac>" : "<ip>";
						++a_n[mac ? "mac" : "ip"];
						i = e;
						continue;
					}
				}
				// MAC with '-': aa-bb-cc-dd-ee-ff
				if (i + 17 <= a_in.size()) {
					bool m = true;
					for (std::size_t k = 0; k < 17 && m; ++k)
						m = (k % 3 == 2) ? a_in[i + k] == '-' : IsHex(a_in[i + k]);
					if (m && (i + 17 == a_in.size() || (!IsAlnum(a_in[i + 17]) && a_in[i + 17] != '-'))) {
						out += "<mac>";
						++a_n["mac"];
						i += 17;
						continue;
					}
				}
			}
			out += a_in[i++];
		}
		return out;
	}

	// 10. The user name and PC name as whole words. Very short or common names are skipped, so a user called "Data"
	// does not erase every "Data" folder.
	inline bool CommonWord(std::string_view a_w)
	{
		static constexpr std::array<std::string_view, 20> kCommon{ "admin", "administrator", "user", "owner", "player", "gamer", "games",
			"game", "data", "mods", "steam", "test", "home", "default", "public", "guest", "fallout", "desktop", "pc", "windows" };
		for (const auto w : kCommon)
			if (detail::EqI(a_w, w))
				return true;
		return false;
	}

	inline std::string Words(std::string_view a_in, std::string_view a_word, std::size_t a_min, std::string_view a_token, Counts& a_n, const char* a_key)
	{
		using namespace detail;
		if (a_word.size() < a_min || CommonWord(a_word))
			return std::string(a_in);
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			if ((i == 0 || !IsAlnum(a_in[i - 1])) && StartsI(a_in, i, a_word) &&
				(i + a_word.size() == a_in.size() || !IsAlnum(a_in[i + a_word.size()]))) {
				out += a_token;
				++a_n[a_key];
				i += a_word.size();
				continue;
			}
			out += a_in[i++];
		}
		return out;
	}

	// 11. Any other full path (D:\..., D:/..., or JSON D:\\...) keeps only its last two names.
	// A path ends at a quote, bracket, ';', ',', a control character, another drive, "://", or a space right after a
	// file name with an extension ("...\X.dll was loaded").
	inline std::string Paths(std::string_view a_in, Counts& a_n)
	{
		using namespace detail;
		auto drive = [&](std::size_t at) {
			return at + 2 < a_in.size() && IsAlpha(a_in[at]) && (at == 0 || !IsAlnum(a_in[at - 1])) && a_in[at + 1] == ':' && IsSep(a_in[at + 2]);
		};
		auto hard = [](char c) {
			return c == '"' || c == '\'' || c == '<' || c == '>' || c == '|' || c == '*' || c == '?' || c == '\r' || c == '\n' || c == '\t' ||
			       c == ';' || c == ',' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
		};
		// a token an earlier step wrote ("<user>") is part of the path, not its end
		auto token = [&](std::size_t at) {
			if (a_in[at] != '<')
				return std::size_t{ 0 };
			std::size_t k = at + 1;
			while (k < a_in.size() && IsLower(a_in[k]))
				++k;
			return k > at + 1 && k < a_in.size() && a_in[k] == '>' ? k + 1 - at : std::size_t{ 0 };
		};
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			if (!drive(i)) {
				out += a_in[i++];
				continue;
			}
			// find the end of the path
			std::size_t e = i + 2, lastSep = i + 2;
			while (e < a_in.size()) {
				const char c = a_in[e];
				if (const auto t = token(e)) {
					e += t;
					continue;
				}
				if (hard(c) || (e > i + 2 && drive(e)) || (c == ':' && e + 2 < a_in.size() && a_in[e + 1] == '/' && a_in[e + 2] == '/'))
					break;
				if (IsSep(c))
					lastSep = e;
				if (c == ' ') {
					// a file name with an extension just before the space ends the path
					const auto name = a_in.substr(lastSep + 1, e - lastSep - 1);
					const auto dot = name.rfind('.');
					if (dot != std::string_view::npos && name.size() - dot - 1 >= 1 && name.size() - dot - 1 <= 4) {
						bool ext = true;
						for (std::size_t k = dot + 1; k < name.size(); ++k)
							ext = ext && IsAlnum(name[k]);
						if (ext)
							break;
					}
				}
				++e;
			}
			// separator runs inside [i + 2, e)
			std::vector<std::pair<std::size_t, std::size_t>> runs;
			for (std::size_t k = i + 2; k < e;) {
				if (IsSep(a_in[k])) {
					std::size_t s = k;
					while (k < e && IsSep(a_in[k]))
						++k;
					runs.emplace_back(s, k);
				} else {
					++k;
				}
			}
			// a trailing separator ("D:\Mods\") is not a name
			if (!runs.empty() && runs.back().second == e && runs.size() > 1)
				runs.pop_back();
			const std::size_t keep = runs.size() >= 2 ? runs.size() - 2 : 0;
			out += "<path>";
			out.append(a_in.substr(runs[keep].first, runs[keep].second - runs[keep].first));
			out.append(a_in.substr(runs[keep].second, e - runs[keep].second));
			++a_n["path"];
			i = e;
		}
		return out;
	}

	// Crash logs: quoted text read from game memory (Buffout 4 prints strings next to registers and stack slots, such
	// as a character's Name: "..."). Kept only when it looks like a file, form or script name: no spaces, at most 160
	// characters of [A-Za-z0-9_.:\/-], and either a '.', ':', '\', '/' or '_' in it, a digit, or a capital after
	// the first letter (EditorIDs such as WorkshopNPCScript). Everything else becomes <text>.
	inline bool KeepMemoryString(std::string_view a_s)
	{
		using namespace detail;
		if (a_s.empty())
			return true;
		if (a_s.size() > 160)
			return false;
		bool marker = false, digit = false, innerCap = false;
		for (std::size_t k = 0; k < a_s.size(); ++k) {
			const char c = a_s[k];
			if (!(IsAlnum(c) || c == '_' || c == '.' || c == ':' || c == '\\' || c == '/' || c == '-'))
				return false;
			marker = marker || c == '.' || c == ':' || c == '\\' || c == '/' || c == '_';
			digit = digit || IsDigit(c);
			innerCap = innerCap || (k > 0 && IsUpper(c));
		}
		return marker || digit || innerCap;
	}

	inline std::string CrashMemoryStrings(std::string_view a_in, Counts& a_n)
	{
		std::string out;
		std::size_t i = 0;
		while (i < a_in.size()) {
			if (a_in[i] != '"') {
				out += a_in[i++];
				continue;
			}
			std::size_t e = i + 1;
			while (e < a_in.size() && a_in[e] != '"' && a_in[e] != '\n')
				++e;
			if (e >= a_in.size() || a_in[e] != '"') {  // no closing quote on this line: leave it
				out += a_in[i++];
				continue;
			}
			const auto s = a_in.substr(i + 1, e - i - 1);
			if (KeepMemoryString(s)) {
				out.append(a_in.substr(i, e - i + 1));
			} else {
				out += "<text>";
				++a_n["text"];
			}
			i = e + 1;
		}
		return out;
	}

	// The whole scrub, in order.
	inline std::string Text(std::string_view a_in, const Context& a_ctx, Counts& a_n)
	{
		auto s = Roots(a_in, a_ctx, a_n);
		s = UserFolders(s, a_ctx, a_n);
		s = Profiles(s, a_n);
		s = SteamIds(s, a_n);
		s = SidsAndGuids(s, a_n);
		s = Emails(s, a_n);
		s = KeyValueSecrets(s, a_n);
		s = LongTokens(s, a_n);
		s = Addresses(s, a_n);
		s = Words(s, a_ctx.user, 4, "<user>", a_n, "user");
		s = Words(s, a_ctx.pc, 3, "<pc>", a_n, "pc");
		if (!detail::EqI(a_ctx.domain, a_ctx.pc))
			s = Words(s, a_ctx.domain, 3, "<pc>", a_n, "pc");
		return Paths(s, a_n);
	}

	inline std::string CrashLog(std::string_view a_in, const Context& a_ctx, Counts& a_n)
	{
		return Text(CrashMemoryStrings(a_in, a_n), a_ctx, a_n);
	}

	// scrub-report.txt: counts only, never the values.
	inline std::string Report(const Counts& a_n)
	{
		static constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kNames{ {
			{ "folder", "known folders (game, Documents, AppData, mod manager) shortened" },
			{ "user", "Windows user names" },
			{ "pc", "PC or domain names" },
			{ "profile", "Mod Organizer 2 profile names" },
			{ "account", "Steam account ids" },
			{ "sid", "Windows account ids (SIDs)" },
			{ "guid", "GUIDs" },
			{ "email", "email addresses" },
			{ "secret", "keys, tokens and passwords" },
			{ "ip", "IP addresses" },
			{ "mac", "network card (MAC) addresses" },
			{ "path", "other folder paths shortened to their last two names" },
			{ "text", "crash-log text read from game memory" },
			{ "", "" },
		} };
		std::string out = "Vitals scrubbed these before writing the share pack (counts only, the values are not kept):\n";
		std::size_t total = 0;
		for (const auto& [key, name] : kNames) {
			if (key.empty())
				continue;
			const auto it = a_n.find(std::string(key));
			const auto n = it == a_n.end() ? 0 : it->second;
			total += n;
			out += "  " + std::to_string(n) + "  " + std::string(name) + "\n";
		}
		out += "Total: " + std::to_string(total) + "\n";
		return out;
	}
}
