// Tests for include/Vitals/Rules.h, the parts of Vitals that need no game. Standard C++20 only:
//   g++ -std=c++20 -I include tests/cpp/rules_test.cpp -o rules_test && ./rules_test tests/loader_cases.txt
// CI runs it (build.yml, analyzer job) and tests/test_tools.py runs it when a compiler is installed.
#include "Vitals/Formats.h"
#include "Vitals/Rules.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

using namespace Vitals::Rules;
namespace F = Vitals::Formats;

static int g_fail = 0;

#define CHECK(cond)                                                                 \
	do {                                                                            \
		if (!(cond)) {                                                              \
			std::cerr << __FILE__ << ":" << __LINE__ << ": FAILED " #cond "\n";     \
			++g_fail;                                                               \
		}                                                                           \
	} while (0)

static std::uint32_t Ver(const std::string& a_text)
{
	const auto f = Split(a_text, '.');
	auto n = [&](std::size_t i) { return i < f.size() ? static_cast<std::uint32_t>(std::stoul(f[i])) : 0u; };
	return (n(0) & 0xFF) << 24 | (n(1) & 0xFF) << 16 | (n(2) & 0xFFF) << 4;
}

static int RunLoaderCases(const char* a_path)
{
	std::ifstream in(a_path);
	if (!in) {
		std::cerr << "cannot open " << a_path << "\n";
		return 1;
	}
	std::string line;
	int         n = 0, bad = 0;
	while (std::getline(in, line)) {
		line = Trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		const auto f = Split(line, '|');
		if (f.size() != 14) {
			std::cerr << "bad case line: " << line << "\n";
			++bad;
			continue;
		}
		Loader::Input in2;
		in2.era = f[0] == "OG" ? Loader::Era::OG : f[0] == "NG" ? Loader::Era::NG : Loader::Era::AE;
		in2.runtime = Ver(f[1]);
		in2.f4se = f[2] == "-" ? 0 : Ver(f[2]);
		const auto ex = Split(f[3], ',');
		auto       has = [&](const char* a_e) { return std::ranges::find(ex, a_e) != ex.end(); };
		in2.hasAnyExport = f[3] != "none";
		in2.hasQuery = has("query");
		if (has("version")) {
			Loader::VersionData v;
			v.dataVersion = static_cast<std::uint32_t>(std::stoul(f[4]));
			v.hasName = f[5] == "y";
			v.addr = static_cast<std::uint32_t>(std::stoul(f[6]));
			v.structs = static_cast<std::uint32_t>(std::stoul(f[7]));
			if (f[8] != "-")
				for (const auto& c : Split(f[8], ','))
					v.compat.push_back(Ver(c));
			v.f4seRequired = f[9] == "-" ? 0 : Ver(f[9]);
			v.reservedBreaking = static_cast<std::uint32_t>(std::stoul(f[10]));
			in2.vd = v;
		}
		in2.addressLibraryPresent = f[11] == "y";
		const auto r = Loader::Check(in2);
		++n;
		if (Loader::Name(r.status) != f[12] || Loader::Name(r.why) != f[13]) {
			std::cerr << "loader case FAILED: " << line << "\n   got " << Loader::Name(r.status) << "|" << Loader::Name(r.why) << "\n";
			++bad;
		}
	}
	std::cout << n << " loader cases, " << bad << " failed\n";
	return bad;
}

static void TextTests()
{
	CHECK(Trim("  a b \r\n") == "a b");
	CHECK(Split("a | b|c", '|') == (std::vector<std::string>{ "a", "b", "c" }));
	CHECK(IsValidUtf8("plain.esp"));
	CHECK(IsValidUtf8("Caf\xC3\xA9.esp"));
	CHECK(!IsValidUtf8("Caf\xE9.esp"));
	CHECK(!IsValidUtf8("\xC3"));
	CHECK(PluginNameToUtf8("Caf\xE9.esp") == "Caf\xC3\xA9.esp");         // Windows-1252 e acute
	CHECK(PluginNameToUtf8("Caf\xC3\xA9.esp") == "Caf\xC3\xA9.esp");     // already UTF-8
	CHECK(PluginNameToUtf8("\x80uro.esp") == "\xE2\x82\xAC" "uro.esp");  // 0x80 is the euro sign in 1252
	CHECK(IniInt("1") == 1);
	CHECK(IniInt(" 1 ; set by BethINI") == 1);
	CHECK(IniInt("0") == 0);
	CHECK(!IniInt("true"));
	CHECK(!IniInt(""));
	CHECK(IniString("") == "");
	CHECK(IniString("  ; empty on purpose") == "");
	CHECK(IniString("Strings ; x") == "Strings");
}

static void StubTests()
{
	const std::uint8_t ret[] = { 0xC3, 0x90, 0x48, 0x89 };
	const std::uint8_t xorret[] = { 0x33, 0xC0, 0xC3, 0xCC };
	const std::uint8_t movret[] = { 0xB0, 0x01, 0xC3 };
	const std::uint8_t prologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x08 };
	const std::uint8_t jmp[] = { 0xE9, 0, 0, 0, 0 };
	CHECK(ReturnStub(ret, sizeof(ret)) == "ret");
	CHECK(ReturnStub(xorret, sizeof(xorret)) == "xor eax,eax; ret");
	CHECK(ReturnStub(movret, sizeof(movret)) == "mov al,imm8; ret");
	CHECK(ReturnStub(prologue, sizeof(prologue)).empty());
	CHECK(ReturnStub(jmp, sizeof(jmp)).empty());
	CHECK(ReturnStub(xorret, 2).empty());  // too short to see the ret
}

static void PopupTests()
{
	using S = std::set<std::string>;
	const std::vector<std::string> crash{ "load_order:a.esp" }, dlls{ "plugin_dll:x.dll", "plugin_dll:y.dll" };
	std::vector<std::string>       none;
	// the box opens on every launch while something is listed: nothing is remembered between launches
	auto s = DecidePopup(1, crash, dlls, {}, false);
	CHECK(s.open && s.show.size() == 3);
	s = DecidePopup(1, crash, dlls, {}, false);
	CHECK(s.open && s.show.size() == 3);
	// nothing to list: no box
	CHECK(!DecidePopup(1, none, none, {}, false).open);
	// "Don't show" mutes not-running plugins only: a crash risk still opens the box
	S muted{ "plugin_dll:x.dll", "plugin_dll:y.dll" };
	s = DecidePopup(1, none, dlls, muted, false);
	CHECK(!s.open && s.show.empty());
	s = DecidePopup(1, crash, dlls, muted, false);
	CHECK(s.open && s.show.size() == 1);
	// a plugin that is not muted still shows alongside muted ones
	s = DecidePopup(1, none, { "plugin_dll:x.dll", "plugin_dll:new.dll" }, muted, false);
	CHECK(s.open && s.show.size() == 1 && s.show[0] == "plugin_dll:new.dll");
	// a 0.3.0 popup_disabled.txt (no keys) mutes every not-running plugin
	s = DecidePopup(1, none, { "plugin_dll:new.dll" }, {}, true);
	CHECK(!s.open);
	// mode 0 never; modes 1 and 2 behave the same
	CHECK(!DecidePopup(0, crash, dlls, {}, false).open);
	CHECK(DecidePopup(2, crash, dlls, {}, false).open);
	CHECK(!DecidePopup(2, none, none, {}, false).open);
}

// ---------------------------------------------------------------- file readers (Formats.h)
template <class T>
static void Put(std::vector<std::uint8_t>& a_d, std::size_t a_off, T a_v)
{
	if (a_d.size() < a_off + sizeof(T))
		a_d.resize(a_off + sizeof(T));
	std::memcpy(a_d.data() + a_off, &a_v, sizeof(T));
}

// A minimal 64-bit DLL: one section at RVA 0x1000 (file 0x400) holding an export directory with the given names.
static std::vector<std::uint8_t> MakePe(const std::vector<std::string>& a_exports)
{
	std::vector<std::uint8_t> d(0x400, 0);
	d[0] = 'M';
	d[1] = 'Z';
	Put<std::uint32_t>(d, 0x3C, 0x80);
	Put<std::uint32_t>(d, 0x80, 0x00004550);
	Put<std::uint16_t>(d, 0x86, 1);      // sections
	Put<std::uint32_t>(d, 0x88, 0x66000000);
	Put<std::uint16_t>(d, 0x94, 0xF0);   // optional header size
	const std::size_t opt = 0x98;
	Put<std::uint16_t>(d, opt, 0x20B);
	Put<std::uint32_t>(d, opt + 112, 0x1000);  // export directory RVA
	const std::size_t sec = opt + 0xF0;
	Put<std::uint32_t>(d, sec + 8, 0x1000);    // vsize
	Put<std::uint32_t>(d, sec + 12, 0x1000);   // va
	Put<std::uint32_t>(d, sec + 16, 0x1000);   // raw size
	Put<std::uint32_t>(d, sec + 20, 0x400);    // raw offset
	d.resize(0x1400, 0);
	const std::uint32_t n = static_cast<std::uint32_t>(a_exports.size());
	const std::uint32_t funcs = 0x1100, names = 0x1200, ords = 0x1300, strs = 0x1340;
	Put<std::uint32_t>(d, 0x400 + 20, n);
	Put<std::uint32_t>(d, 0x400 + 24, n);
	Put<std::uint32_t>(d, 0x400 + 28, funcs);
	Put<std::uint32_t>(d, 0x400 + 32, names);
	Put<std::uint32_t>(d, 0x400 + 36, ords);
	std::uint32_t str = strs;
	for (std::uint32_t i = 0; i < n; ++i) {
		Put<std::uint32_t>(d, 0x400 + (funcs - 0x1000) + 4 * i, 0x1800 + i);
		Put<std::uint32_t>(d, 0x400 + (names - 0x1000) + 4 * i, str);
		Put<std::uint16_t>(d, 0x400 + (ords - 0x1000) + 2 * i, static_cast<std::uint16_t>(i));
		std::memcpy(d.data() + 0x400 + (str - 0x1000), a_exports[i].c_str(), a_exports[i].size() + 1);
		str += static_cast<std::uint32_t>(a_exports[i].size() + 1);
	}
	return d;
}

static void PeTests()
{
	const auto good = MakePe({ "F4SEPlugin_Load", "F4SEPlugin_Query" });
	auto       pe = F::ParsePe(good);
	CHECK(pe.ok && pe.exports.size() == 2 && pe.exports.contains("F4SEPlugin_Query") && pe.timestamp == 0x66000000);
	// e_lfanew near 4 GB: 32-bit "peOff + 24" wrapped to a small number in build 40
	for (const std::uint32_t bad : { 0xFFFFFFF0u, 0xFFFFFFFFu, 0xFFFFFFE8u, 0x7FFFFFFFu, 0x13FCu, 0x13FDu }) {
		auto d = good;
		Put<std::uint32_t>(d, 0x3C, bad);
		CHECK(!F::ParsePe(d).ok);
	}
	// section and export fields at the extremes: nothing read out of bounds, no crash (run under -fsanitize to see it)
	{
		auto d = good;
		Put<std::uint32_t>(d, 0x98 + 0xF0 + 8, 0xFFFFFFFFu);   // vsize: va + vsize wrapped in 32 bits
		Put<std::uint32_t>(d, 0x98 + 0xF0 + 16, 0xFFFFFFFFu);  // raw size
		Put<std::uint32_t>(d, 0x98 + 0xF0 + 20, 0xFFFFFF00u);  // raw offset
		F::ParsePe(d);
		auto e = good;
		Put<std::uint32_t>(e, 0x400 + 24, 0xFFFFFFFFu);  // name count
		Put<std::uint32_t>(e, 0x400 + 32, 0xFFFFFFF0u);  // names RVA
		F::ParsePe(e);
	}
	// every truncation of a good file, and random bytes over it
	for (std::size_t n = 0; n < good.size(); n += 7)
		F::ParsePe(std::span(good.data(), n));
	std::mt19937 rng(40);
	for (int i = 0; i < 4000; ++i) {
		auto d = good;
		for (int k = 0; k < 8; ++k)
			d[rng() % d.size()] = static_cast<std::uint8_t>(rng());
		Put<std::uint32_t>(d, 0x80 + (rng() % 0x380), static_cast<std::uint32_t>(rng()));
		F::ParsePe(d);
	}
	CHECK(F::ParsePe(std::vector<std::uint8_t>{}).error == "not a Windows DLL");
	auto x86 = good;
	Put<std::uint16_t>(x86, 0x98, 0x10B);
	CHECK(!F::ParsePe(x86).ok && F::ParsePe(x86).error.find("32-bit") != std::string::npos);
}

static std::vector<std::uint8_t> MakeAl(const std::vector<std::pair<std::uint64_t, std::uint64_t>>& a_rows, std::uint64_t a_count)
{
	std::vector<std::uint8_t> d;
	Put<std::uint64_t>(d, 0, a_count);
	for (std::size_t i = 0; i < a_rows.size(); ++i) {
		Put<std::uint64_t>(d, 8 + i * 16, a_rows[i].first);
		Put<std::uint64_t>(d, 16 + i * 16, a_rows[i].second);
	}
	return d;
}

static void AddressLibraryTests()
{
	using S = F::AddressLib::State;
	const std::vector<std::pair<std::uint64_t, std::uint64_t>> rows{ { 1, 0x10 }, { 5, 0x50 }, { 9, 0x90 }, { 4000000, 0x1234 } };
	auto ok = MakeAl(rows, rows.size());
	auto t = F::AddressLib::Check(ok);
	CHECK(t.state == S::ok && t.count == 4);
	CHECK(F::AddressLib::Find(ok, t.count, 5) == 0x50u);
	CHECK(F::AddressLib::Find(ok, t.count, 4000000) == 0x1234u);
	CHECK(!F::AddressLib::Find(ok, t.count, 6));
	CHECK(!F::AddressLib::Find(ok, t.count, 0));
	CHECK(F::AddressLib::Check(MakeAl(rows, 5)).state == S::truncated);                    // count says more than the file holds
	CHECK(F::AddressLib::Check(MakeAl(rows, 0xFFFFFFFFFFFFFFFFull)).state == S::truncated);  // count * 16 would wrap
	CHECK(F::AddressLib::Check(MakeAl({}, 0)).state == S::empty);
	CHECK(F::AddressLib::Check(std::vector<std::uint8_t>{ 1, 2, 3 }).state == S::too_small);
	CHECK(F::AddressLib::Check(MakeAl({ { 5, 1 }, { 1, 2 } }, 2)).state == S::unsorted);
	CHECK(F::AddressLib::Check(MakeAl({ { 5, 1 }, { 5, 2 } }, 2)).state == S::unsorted);
	auto cut = ok;
	cut.resize(cut.size() - 1);
	CHECK(F::AddressLib::Check(cut).state == S::truncated);
	CHECK(std::string(F::AddressLib::Name(S::truncated)) == "truncated");
}

static std::vector<std::uint8_t> MakeTes4(std::uint32_t a_flags, const std::vector<std::string>& a_masters)
{
	std::vector<std::uint8_t> d{ 'T', 'E', 'S', '4' };
	std::vector<std::uint8_t> body;
	for (const auto& m : a_masters) {
		body.insert(body.end(), { 'M', 'A', 'S', 'T' });
		Put<std::uint16_t>(body, body.size(), static_cast<std::uint16_t>(m.size() + 1));
		body.insert(body.end(), m.begin(), m.end());
		body.push_back(0);
		body.insert(body.end(), { 'D', 'A', 'T', 'A', 8, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
	}
	Put<std::uint32_t>(d, 4, static_cast<std::uint32_t>(body.size()));
	Put<std::uint32_t>(d, 8, a_flags);
	d.resize(24, 0);
	d.insert(d.end(), body.begin(), body.end());
	return d;
}

static void Tes4Tests()
{
	auto h = F::ParseTes4(MakeTes4(0x1, { "Fallout4.esm", "Caf\xE9.esp" }), ".esp");
	CHECK(h.ok && h.master && !h.light && h.masters.size() == 2);
	CHECK(h.masters.size() == 2 && h.masters[1] == "Caf\xC3\xA9.esp");  // Windows-1252 in the file
	h = F::ParseTes4(MakeTes4(0, { "Caf\xC3\xA9.esm" }), ".esp");
	CHECK(h.ok && !h.master && h.masters.size() == 1 && h.masters[0] == "Caf\xC3\xA9.esm");  // UTF-8 kept
	CHECK(F::ParseTes4(MakeTes4(0x200, {}), ".esp").light);
	h = F::ParseTes4(MakeTes4(0, {}), ".esl");
	CHECK(h.light && h.master);
	CHECK(!F::ParseTes4(std::vector<std::uint8_t>{ 'T', 'E', 'S' }, ".esp").ok);
	auto big = MakeTes4(0, { "A.esm" });
	Put<std::uint32_t>(big, 4, 0xFFFFFFFFu);  // record size larger than the file
	CHECK(F::ParseTes4(big, ".esp").ok);
	for (std::size_t n = 0; n < big.size(); ++n)
		F::ParseTes4(std::span(big.data(), n), ".esp");
}

static void KeyFileTests()
{
	// spaces and ';' in DLL names survive a write and a read (build 40 dropped every key with a space)
	const std::set<std::string> keys{ "plugin_dll:example framework.dll", "plugin_dll:a;b.dll", "load_order:x.esp" };
	auto f = ParseKeyFile(FormatKeyFile(keys, "# comment\n"));
	CHECK(f.versioned && f.keys == keys && !f.legacyNoKeys);
	f = ParseKeyFile(FormatKeyFile({}, "# Delete this file to show them again.\n"));
	CHECK(f.versioned && f.keys.empty() && !f.legacyNoKeys);  // a new file with no keys does NOT mute everything
	f = ParseKeyFile(std::string(kKeyFileMagic) + "\r\nplugin_dll:x y.dll\r\n");
	CHECK(f.keys.size() == 1 && f.keys.contains("plugin_dll:x y.dll"));  // CRLF
	// 0.3.0 popup_seen.txt: one line joined with ';'
	f = ParseKeyFile("plugin_dll:a.dll;load_order:b.esp\n");
	CHECK(!f.versioned && f.keys.size() == 2);
	// 0.3.0 popup_disabled.txt: a sentence, no keys -> mutes every plugin
	f = ParseKeyFile("Vitals will not show this box again. Delete this file to see it again.\n");
	CHECK(!f.versioned && f.keys.empty() && f.legacyNoKeys);
}

static void PlayerTextTests()
{
	CHECK(Utf8Prefix("abc", 10) == "abc");
	CHECK(Utf8Prefix("abcdef", 3) == "abc");
	CHECK(Utf8Prefix("ab\xC3\xA9z", 3) == "ab");             // never half of e acute
	CHECK(Utf8Prefix("a\xE2\x82\xAC", 3) == "a");            // nor of a 3-byte euro sign
	CHECK(Utf8Prefix("a\xE2\x82\xAC", 4) == "a\xE2\x82\xAC");
	CHECK(IsValidUtf8(Utf8Prefix("\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 6)));
	// the user name is replaced only as a whole folder name
	CHECK(RedactUserFolder("2 problem(s), PROBLEMS, Rob", "Rob") == "2 problem(s), PROBLEMS, Rob");
	CHECK(RedactUserFolder("Anniversary Patch", "Ann") == "Anniversary Patch");
	CHECK(RedactUserFolder("D:\\Users\\rob\\Games\\Fallout 4", "Rob") == "D:\\Users\\<user>\\Games\\Fallout 4");
	CHECK(RedactUserFolder("C:/Users/Rob", "Rob") == "C:/Users/<user>");
	CHECK(RedactUserFolder("E:\\Robert\\x", "Rob") == "E:\\Robert\\x");
	CHECK(RedactUserFolder("\"C:\\Rob\" and C:\\Rob;", "Rob") == "\"C:\\<user>\" and C:\\<user>;");
}

static void LoadOrderTests()
{
	// implicit: Fallout4.esm; then plugins.txt: a.esp, flagged.esp (ESM flag), b.esm, c.esp
	const std::vector<bool> master{ true, false, true, true, false };
	CHECK(ActualLoadOrder(1, master) == (std::vector<std::size_t>{ 0, 2, 3, 1, 4 }));
	CHECK(ActualLoadOrder(0, { false, true }) == (std::vector<std::size_t>{ 1, 0 }));
	CHECK(ActualLoadOrder(3, { true, true }) == (std::vector<std::size_t>{ 0, 1 }));
	CHECK(ActualLoadOrder(0, {}).empty());
}

int main(int argc, char** argv)
{
	TextTests();
	StubTests();
	PopupTests();
	PeTests();
	AddressLibraryTests();
	Tes4Tests();
	KeyFileTests();
	PlayerTextTests();
	LoadOrderTests();
	const int loader = argc > 1 ? RunLoaderCases(argv[1]) : 0;
	std::cout << (g_fail ? "unit checks FAILED: " : "unit checks ok") << (g_fail ? std::to_string(g_fail) : "") << "\n";
	return (g_fail || loader) ? 1 : 0;
}
