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
}
