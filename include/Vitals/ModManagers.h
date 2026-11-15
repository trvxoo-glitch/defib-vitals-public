#pragma once
// Reading what the player's mod manager already knows, so Vitals can explain a DLL without our own copy of it or a
// Nexus page: which installed mods carry a copy of the same DLL (Vortex staging folder, MO2 mods folder), and what
// MO2's meta.ini says about a mod (its version, and the newest version MO2 saw on Nexus). Plain C++20; tested in
// tests/cpp/rules_test.cpp. src/Doctor.cpp does the file access.

#include <algorithm>
#include <map>
#include <string>
#include <string_view>

namespace Vitals::ModManagers
{
	inline std::string Lower(std::string s)
	{
		std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
		return s;
	}

	inline std::string Trim(std::string_view s)
	{
		while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n' || s.front() == '"'))
			s.remove_prefix(1);
		while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n' || s.back() == '"'))
			s.remove_suffix(1);
		return std::string(s);
	}

	// key=value lines (Qt and Windows INI style): "section/key" keys lower-cased; the first value wins.
	inline std::map<std::string, std::string> SimpleIni(std::string_view a_text)
	{
		std::map<std::string, std::string> out;
		std::string                        section;
		while (!a_text.empty()) {
			const auto nl = a_text.find('\n');
			auto       line = Trim(a_text.substr(0, nl));
			a_text = nl == std::string_view::npos ? std::string_view{} : a_text.substr(nl + 1);
			if (line.empty() || line[0] == ';' || line[0] == '#')
				continue;
			if (line.front() == '[' && line.back() == ']') {
				section = Lower(line.substr(1, line.size() - 2));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			out.try_emplace(section + "/" + Lower(Trim(line.substr(0, eq))), Trim(line.substr(eq + 1)));
		}
		return out;
	}

	// An MO2 path setting: @ByteArray(...) wrapper, %BASE_DIR% placeholder, forward slashes. Empty in, empty out.
	inline std::string Mo2Path(std::string a_value, std::string_view a_baseDir)
	{
		if (a_value.starts_with("@ByteArray(") && a_value.ends_with(")"))
			a_value = a_value.substr(11, a_value.size() - 12);
		if (const auto at = Lower(a_value).find("%base_dir%"); at != std::string::npos)
			a_value.replace(at, 10, a_baseDir);
		std::ranges::replace(a_value, '/', '\\');
		return a_value;
	}

	// The mods folder of an MO2 instance from its ModOrganizer.ini text and the folder that ini is in.
	inline std::string Mo2ModsDir(std::string_view a_iniText, std::string_view a_instanceDir)
	{
		const auto ini = SimpleIni(a_iniText);
		std::string base = a_instanceDir.empty() ? std::string{} : std::string(a_instanceDir);
		if (const auto it = ini.find("settings/base_directory"); it != ini.end() && !it->second.empty())
			base = Mo2Path(it->second, a_instanceDir);
		if (const auto it = ini.find("settings/mod_directory"); it != ini.end() && !it->second.empty())
			return Mo2Path(it->second, base);
		return base.empty() ? std::string{} : base + "\\mods";
	}

	struct MetaInfo
	{
		std::string version, newest, modId;
	};

	// MO2's meta.ini for one mod: the installed version and the newest version MO2's own update check saw.
	inline MetaInfo Mo2Meta(std::string_view a_text)
	{
		const auto ini = SimpleIni(a_text);
		auto       get = [&](const char* k) {
            const auto it = ini.find(std::string("general/") + k);
            return it == ini.end() ? std::string{} : it->second;
		};
		return { get("version"), get("newestversion"), get("modid") };
	}

	// "1.2.3" vs "1.10" by numbers; true when a_newer is a higher version than a_have. Text that is not numbers
	// (a "d" prefix, dates) compares field by field as text.
	inline bool NewerVersion(std::string_view a_newer, std::string_view a_have)
	{
		auto next = [](std::string_view& s) {
			while (!s.empty() && !(s.front() >= '0' && s.front() <= '9') && s.front() != '.')
				s.remove_prefix(1);
			std::string_view f = s.substr(0, s.find('.'));
			s = f.size() < s.size() ? s.substr(f.size() + 1) : std::string_view{};
			return f;
		};
		std::string_view a = a_newer, b = a_have;
		if (a.empty() || b.empty())
			return false;
		for (int i = 0; i < 8 && (!a.empty() || !b.empty()); ++i) {
			auto x = next(a), y = next(b);
			if (x.empty())
				x = "0";  // 5.0 == 5.0.0
			if (y.empty())
				y = "0";
			const bool xn = !x.empty() && std::ranges::all_of(x, [](char c) { return c >= '0' && c <= '9'; });
			const bool yn = !y.empty() && std::ranges::all_of(y, [](char c) { return c >= '0' && c <= '9'; });
			if (xn && yn) {
				const auto xs = x.substr(std::min(x.find_first_not_of('0'), x.size() - 1));
				const auto ys = y.substr(std::min(y.find_first_not_of('0'), y.size() - 1));
				if (xs.size() != ys.size())
					return xs.size() > ys.size();
				if (xs != ys)
					return xs > ys;
			} else if (x != y) {
				return x > y;
			}
		}
		return false;
	}

	// ---------------- Vortex's own update check ----------------
	// Vortex keeps its whole state in a LevelDB database, and also writes full JSON copies of it to
	// %APPDATA%\Vortex\temp\state_backups_full\*.json (at start-up, then hourly and daily). In those,
	// persistent.mods.<game>.<id> is one installed mod: "installationPath" is its folder in the staging folder (the
	// same name vortex.deployment.json gives as "source"), and "attributes" holds "version" and, once Vortex's update
	// check has run, "newestVersion". This reads only that, with a small JSON walker (no allocation per value we skip).
	namespace detail
	{
		struct Json
		{
			std::string_view s;
			std::size_t      i{ 0 };
			int              depth{ 0 };

			void Ws()
			{
				while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
					++i;
			}
			bool Eat(char c)
			{
				Ws();
				if (i < s.size() && s[i] == c) {
					++i;
					return true;
				}
				return false;
			}
			bool Str(std::string* a_out)
			{
				Ws();
				if (i >= s.size() || s[i] != '"')
					return false;
				for (++i; i < s.size(); ++i) {
					const char c = s[i];
					if (c == '"') {
						++i;
						return true;
					}
					if (c == '\\') {
						if (++i >= s.size())
							return false;
						const char e = s[i];
						if (a_out)
							*a_out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'u' ? '?' : e;
						if (e == 'u')
							i += 4;
						continue;
					}
					if (a_out)
						*a_out += c;
				}
				return false;
			}
			// Skips any value; for a string or number, returns its text in a_out when given.
			bool Skip(std::string* a_out = nullptr)
			{
				Ws();
				if (i >= s.size() || ++depth > 64)
					return false;
				bool ok = true;
				if (s[i] == '"')
					ok = Str(a_out);
				else if (s[i] == '{' || s[i] == '[') {
					const char close = s[i] == '{' ? '}' : ']';
					const bool obj = s[i] == '{';
					++i;
					if (!Eat(close)) {
						do {
							if (obj && (!Str(nullptr) || !Eat(':'))) {
								ok = false;
								break;
							}
							if (!Skip()) {
								ok = false;
								break;
							}
						} while (Eat(','));
						ok = ok && Eat(close);
					}
				} else {
					const auto start = i;
					while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r')
						++i;
					if (a_out)
						*a_out = std::string(s.substr(start, i - start));
					ok = i > start;
				}
				--depth;
				return ok;
			}
			// Calls a_fn(key) for each member of the object at i; a_fn must consume the value (return false to stop).
			template <class F>
			bool Members(F&& a_fn)
			{
				if (!Eat('{'))
					return false;
				if (Eat('}'))
					return true;
				do {
					std::string key;
					if (!Str(&key) || !Eat(':') || !a_fn(key))
						return false;
				} while (Eat(','));
				return Eat('}');
			}
		};
	}

	struct VortexModInfo
	{
		std::string version, newest;
	};

	// installationPath (lower-cased) -> versions, for one game ("fallout4") from a Vortex full state backup.
	inline std::map<std::string, VortexModInfo> VortexStateMods(std::string_view a_json, std::string_view a_game = "fallout4")
	{
		std::map<std::string, VortexModInfo> out;
		detail::Json                         j{ a_json };
		auto mod = [&](const std::string&) {
			std::string   path;
			VortexModInfo info;
			const bool ok = j.Members([&](const std::string& k) {
				if (k == "installationPath")
					return j.Skip(&path);
				if (k == "attributes")
					return j.Members([&](const std::string& a) {
						if (a == "version")
							return j.Skip(&info.version);
						if (a == "newestVersion")
							return j.Skip(&info.newest);
						return j.Skip();
					});
				return j.Skip();
			});
			if (ok && !path.empty())
				out[Lower(path)] = info;
			return ok;
		};
		j.Members([&](const std::string& top) {
			if (top != "persistent")
				return j.Skip();
			return j.Members([&](const std::string& p) {
				if (p != "mods")
					return j.Skip();
				return j.Members([&](const std::string& g) { return g == a_game ? j.Members(mod) : j.Skip(); });
			});
		});
		return out;
	}
}
