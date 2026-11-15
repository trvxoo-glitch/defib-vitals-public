#pragma once
// Reading what the player's mod manager already knows, so Vitals can explain a DLL without our own copy of it or a
// Nexus page: which installed mods carry a copy of the same DLL (Vortex staging folder, MO2 mods folder), and what
// MO2's meta.ini says about a mod (its version, and the newest version MO2 saw on Nexus). Plain C++20; tested in
// tests/cpp/rules_test.cpp. src/Doctor.cpp does the file access.

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <vector>

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

	// An MO2 path setting: @ByteArray(...) wrapper, Qt's doubled backslashes, %BASE_DIR% placeholder, forward
	// slashes. Empty in, empty out.
	inline std::string Mo2Path(std::string a_value, std::string_view a_baseDir)
	{
		if (a_value.starts_with("@ByteArray(") && a_value.ends_with(")"))
			a_value = a_value.substr(11, a_value.size() - 12);
		std::string unescaped;  // Qt writes a backslash in an INI value as two
		for (std::size_t i = 0; i < a_value.size(); ++i) {
			unescaped += a_value[i];
			if (a_value[i] == '\\' && i + 1 < a_value.size() && a_value[i + 1] == '\\')
				++i;
		}
		a_value = std::move(unescaped);
		if (const auto at = Lower(a_value).find("%base_dir%"); at != std::string::npos)
			a_value.replace(at, 10, a_baseDir);
		std::ranges::replace(a_value, '/', '\\');
		while (a_value.size() > 3 && a_value.back() == '\\')
			a_value.pop_back();
		return a_value;
	}

	// The same folder, written two ways ("D:\\Games\\Fallout 4\\" and "d:/games/fallout 4")?
	inline bool SameDir(std::string a, std::string b)
	{
		for (auto* s : { &a, &b }) {
			std::ranges::replace(*s, '/', '\\');
			while (s->size() > 3 && s->back() == '\\')
				s->pop_back();
			*s = Lower(*s);
		}
		return !a.empty() && a == b;
	}

	// The game folder an MO2 instance manages ([General] gamePath), so only a Fallout 4 instance is used: the same
	// %LOCALAPPDATA%\ModOrganizer folder holds the instances of every game.
	inline std::string Mo2GamePath(std::string_view a_iniText)
	{
		const auto ini = SimpleIni(a_iniText);
		const auto it = ini.find("general/gamepath");
		return it == ini.end() ? std::string{} : Mo2Path(it->second, {});
	}

	// The folder of the profile an MO2 instance has selected (its modlist.txt says which mods are on, and in what order).
	inline std::string Mo2ProfileDir(std::string_view a_iniText, std::string_view a_instanceDir)
	{
		const auto ini = SimpleIni(a_iniText);
		std::string base(a_instanceDir);
		if (const auto it = ini.find("settings/base_directory"); it != ini.end() && !it->second.empty())
			base = Mo2Path(it->second, a_instanceDir);
		std::string profile = "Default";
		if (const auto it = ini.find("general/selected_profile"); it != ini.end() && !it->second.empty())
			profile = Mo2Path(it->second, {});
		std::string dir = base.empty() ? std::string{} : base + "\\profiles";
		if (const auto it = ini.find("settings/profiles_directory"); it != ini.end() && !it->second.empty())
			dir = Mo2Path(it->second, base);
		return dir.empty() ? std::string{} : dir + "\\" + profile;
	}

	// A profile's modlist.txt, highest priority first (MO2 writes the mod that wins file conflicts, the bottom of
	// its left pane, at the top of the file). "+Name" is on, "-Name" is off; "*Name" (DLC, unmanaged) is skipped.
	struct ModListEntry
	{
		std::string name;
		bool        enabled{ false };
	};
	inline std::vector<ModListEntry> Mo2ModList(std::string_view a_text)
	{
		std::vector<ModListEntry> out;
		while (!a_text.empty()) {
			const auto nl = a_text.find('\n');
			auto       line = a_text.substr(0, nl);
			a_text = nl == std::string_view::npos ? std::string_view{} : a_text.substr(nl + 1);
			while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.remove_suffix(1);
			if (line.size() < 2 || (line[0] != '+' && line[0] != '-'))
				continue;
			out.push_back({ std::string(line.substr(1)), line[0] == '+' });
		}
		return out;
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

	// "1.2.3" vs "1.10" by numbers; true only when a_newer is clearly a higher version than a_have. A leading "v" is
	// ignored and missing fields count as 0 (5.0 == 5.0.0). After equal numbers, a release beats its own pre-release
	// ("2.0" is newer than "2.0-beta", never the reverse) and "1.2b" beats "1.2a" or "1.2". Anything else that is not
	// clearly newer (text the numbers do not explain, a date, an empty side) is false: a wrong "update available" sends
	// the player to download what they already have.
	inline bool NewerVersion(std::string_view a_newer, std::string_view a_have)
	{
		struct V
		{
			std::vector<std::string> nums;
			std::string              suffix;
		};
		auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
		auto parse = [&](std::string_view s) {
			V v;
			while (!s.empty() && (s.front() == ' ' || s.front() == 'v' || s.front() == 'V'))
				s.remove_prefix(1);
			while (!s.empty() && isDigit(s.front())) {
				std::size_t n = 0;
				while (n < s.size() && isDigit(s[n]))
					++n;
				auto num = s.substr(0, n);
				while (num.size() > 1 && num.front() == '0')
					num.remove_prefix(1);
				v.nums.emplace_back(num);
				s.remove_prefix(n);
				if (s.size() >= 2 && s[0] == '.' && isDigit(s[1]))
					s.remove_prefix(1);
				else
					break;
			}
			while (!s.empty() && (s.front() == '-' || s.front() == '_' || s.front() == '.' || s.front() == ' '))
				s.remove_prefix(1);
			while (!s.empty() && s.back() == ' ')
				s.remove_suffix(1);
			v.suffix = Lower(std::string(s));
			return v;
		};
		const auto a = parse(a_newer), b = parse(a_have);
		if (a.nums.empty() || b.nums.empty())
			return false;
		for (std::size_t i = 0; i < std::max(a.nums.size(), b.nums.size()); ++i) {
			const std::string x = i < a.nums.size() ? a.nums[i] : "0", y = i < b.nums.size() ? b.nums[i] : "0";
			if (x.size() != y.size())
				return x.size() > y.size();
			if (x != y)
				return x > y;
		}
		auto letter = [](const std::string& s) { return s.size() == 1 && s[0] >= 'a' && s[0] <= 'z'; };
		auto pre = [](const std::string& s) {
			for (const char* w : { "alpha", "beta", "rc", "pre", "dev", "test", "wip", "preview" })
				if (s.starts_with(w))
					return true;
			return false;
		};
		if (a.suffix == b.suffix)
			return false;
		if (a.suffix.empty())
			return pre(b.suffix);                   // 2.0 vs 2.0-beta
		if (letter(a.suffix))
			return b.suffix.empty() ? true : letter(b.suffix) && a.suffix > b.suffix;  // 1.2b vs 1.2 or 1.2a
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
