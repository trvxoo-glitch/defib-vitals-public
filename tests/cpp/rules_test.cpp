// Tests for include/Vitals/Rules.h, the parts of Vitals that need no game. Standard C++20 only:
//   g++ -std=c++20 -I include tests/cpp/rules_test.cpp -o rules_test && ./rules_test tests/loader_cases.txt
// CI runs it (build.yml, analyzer job) and tests/test_tools.py runs it when a compiler is installed.
#include "Vitals/Formats.h"
#include "Vitals/Launch.h"
#include "Vitals/ModManagers.h"
#include "Vitals/Rules.h"
#include "Vitals/Scrub.h"
#include "Vitals/Share.h"
#include "Vitals/Zip.h"

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
	CHECK(!pe.reproducible);
	CHECK(F::FileVersion(good).empty());
	{
		auto d = good;
		const std::size_t at = 0x1380;  // inside the section, after the export names
		Put<std::uint32_t>(d, at, 0xFEEF04BD);
		Put<std::uint32_t>(d, at + 4, 0x00010000);
		Put<std::uint32_t>(d, at + 8, (5u << 16) | 0);   // 5.0
		Put<std::uint32_t>(d, at + 12, (2u << 16) | 7);  // .2.7
		CHECK(F::FileVersion(d) == "5.0.2.7");
		Put<std::uint32_t>(d, at + 4, 0x00020000);  // not a VS_FIXEDFILEINFO
		CHECK(F::FileVersion(d).empty());
		std::vector<std::uint8_t> cut(d.begin(), d.begin() + at + 10);  // signature at the very end: no read past it
		CHECK(F::FileVersion(cut).empty());
	}
	{
		// a /Brepro build: debug directory (data directory 6) with an entry of type 16; its timestamp is a hash, not a date
		auto d = good;
		Put<std::uint32_t>(d, 0x98 + 160, 0x13A0);  // debug directory RVA
		Put<std::uint32_t>(d, 0x98 + 164, 28);      // size: one entry
		Put<std::uint32_t>(d, 0x400 + (0x13A0 - 0x1000) + 12, 16);
		CHECK(F::ParsePe(d).reproducible);
		Put<std::uint32_t>(d, 0x400 + (0x13A0 - 0x1000) + 12, 2);  // CodeView: a normal build
		CHECK(!F::ParsePe(d).reproducible);
		Put<std::uint32_t>(d, 0x98 + 164, 0xFFFFFFFF);  // absurd size: ignored, no crash
		CHECK(!F::ParsePe(d).reproducible);
	}
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

// V-003: the image search never touches memory itself; it asks a reader that can fail (a page freed or re-protected mid-scan)
static void ImageSearchTests()
{
	std::string img(3 * 4096 + 100, 'x');
	auto        put = [&](std::size_t at, std::string_view s) { img.replace(at, s.size(), s); };
	std::set<std::size_t> bad;  // page numbers whose read fails
	std::size_t           reads = 0;
	auto read = [&](std::size_t a_off, std::uint8_t* a_out, std::size_t a_len) {
		++reads;
		for (std::size_t p = a_off / 4096; p <= (a_off + a_len - 1) / 4096; ++p)
			if (bad.count(p))
				return false;
		std::memcpy(a_out, img.data() + a_off, a_len);
		return true;
	};
	CHECK(!SearchPaged(img.size(), "f4rd-runtime", read));
	put(5000, "F4RD-Runtime");  // any case, inside page 1
	CHECK(SearchPaged(img.size(), "f4rd-runtime", read));
	img.assign(img.size(), 'x');
	put(4096 - 5, "f4rd-runtime");  // across the page 0 / page 1 boundary
	CHECK(SearchPaged(img.size(), "f4rd-runtime", read));
	// a page that cannot be read is skipped; the others are still searched
	img.assign(img.size(), 'x');
	put(2 * 4096 + 10, "needle");
	bad = { 1 };
	CHECK(SearchPaged(img.size(), "needle", read));
	// a needle only in an unreadable page is not found, and nothing is read past what the reader allowed
	bad = { 2 };
	CHECK(!SearchPaged(img.size(), "needle", read));
	// the boundary case with the next page unreadable: the first page is read alone and its own bytes still match
	img.assign(img.size(), 'x');
	put(100, "needle");
	bad = { 1 };
	CHECK(SearchPaged(img.size(), "needle", read));
	// every read fails: not found, no crash
	bad = { 0, 1, 2, 3 };
	CHECK(!SearchPaged(img.size(), "needle", read));
	bad.clear();
	// degenerate input
	CHECK(!SearchPaged(0, "needle", read));
	CHECK(!SearchPaged(3, "needle", read));
	CHECK(!SearchPaged(img.size(), "", read));
	reads = 0;
	CHECK(!SearchPaged(img.size(), "needle", read, 0) && reads == 0);
	// the tail page is shorter than a page: no read beyond the image
	img.assign(100, 'x');
	put(94, "needle");
	std::size_t maxEnd = 0;
	auto        tail = [&](std::size_t a_off, std::uint8_t* a_out, std::size_t a_len) {
        maxEnd = std::max(maxEnd, a_off + a_len);
        std::memcpy(a_out, img.data() + a_off, a_len);
        return true;
	};
	CHECK(SearchPaged(img.size(), "needle", tail) && maxEnd <= img.size());
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

// tests/scrub_cases.txt: the share-pack scrubber
static int RunScrubCases(const std::string& a_path)
{
	std::ifstream in(a_path, std::ios::binary);
	if (!in) {
		std::cerr << "cannot open " << a_path << "\n";
		return 1;
	}
	namespace S = Vitals::Scrub;
	S::Context  ctx;
	std::string line, input;
	bool        crash = false;
	int         n = 0, bad = 0;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.starts_with("ctx ")) {
			const auto kv = line.substr(4);
			if (kv == "reset")
				ctx = {};
			else if (kv.starts_with("user="))
				ctx.user = kv.substr(5);
			else if (kv.starts_with("pc="))
				ctx.pc = kv.substr(3);
			else if (kv.starts_with("domain="))
				ctx.domain = kv.substr(7);
			else if (kv.starts_with("root=")) {
				const auto bar = kv.find('|');
				ctx.roots.emplace_back(kv.substr(5, bar - 5), kv.substr(bar + 1));
			}
		} else if (line.starts_with("text: ") || line.starts_with("crash: ")) {
			crash = line.starts_with("crash: ");
			input = line.substr(crash ? 7 : 6);
		} else if (line.starts_with("want: ")) {
			S::Counts  counts;
			const auto got = crash ? S::CrashLog(input, ctx, counts) : S::Text(input, ctx, counts);
			++n;
			if (got != line.substr(6)) {
				std::cerr << "scrub case FAILED\n  in:   " << input << "\n  want: " << line.substr(6) << "\n  got:  " << got << "\n";
				++bad;
			}
		}
	}
	std::cout << "scrub cases: " << n << " run, " << bad << " failed\n";
	return (bad || n < 40) ? 1 : 0;
}

static void ScrubTests()
{
	namespace S = Vitals::Scrub;
	S::Counts n;
	const S::Context ctx{ "Robin Hale", "DESKTOP-4F7K2Q", "", { { "D:\\Games\\Fallout 4", "<game>" } } };
	// idempotent: a second pass (the server's) changes nothing
	const std::string once = S::Text("C:\\Users\\Robin Hale\\x\\y.txt robin@example.com 192.168.0.4 D:\\Games\\Fallout 4\\Data", ctx, n);
	S::Counts         n2;
	CHECK(S::Text(once, ctx, n2) == once);
	CHECK(S::Text(once, {}, n2) == once);
	CHECK(n2.empty());
	CHECK(n["email"] == 1 && n["ip"] == 1 && n["folder"] == 1);
	const auto report = S::Report(n);
	CHECK(report.find("1  email addresses") != std::string::npos);
	CHECK(report.find("robin") == std::string::npos);
	// a common word as user name is not replaced everywhere
	S::Counts n3;
	CHECK(S::Text("Data\\F4SE", S::Context{ "Data", "", "", {} }, n3) == "Data\\F4SE");
	CHECK(S::LooksRandom("Zm9vYmFyQmF6UXV4MTIzNDU2Nzg5MEFCQ0RFRkdISUpLTE1O"));
	CHECK(!S::LooksRandom("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));
	// fuzz: random bytes never crash and never grow without bound
	std::mt19937 rng(7);
	for (int k = 0; k < 2000; ++k) {
		std::string s(static_cast<std::size_t>(rng() % 200), '\0');
		for (auto& c : s)
			c = "\\/:.-@\"' aZ09fF<>=_\n\t"[rng() % 22];
		S::Counts f;
		const auto out = S::CrashLog(s, ctx, f);
		CHECK(out.size() <= s.size() * 8 + 16);
	}
}

static void ZipTests()
{
	namespace Z = Vitals::Zip;
	CHECK(Z::Crc32("") == 0);
	CHECK(Z::Crc32("123456789") == 0xCBF43926u);
	Z::Writer w(Z::DosTime::From(2026, 11, 15, 10, 42, 12));
	CHECK(w.Add("manifest.json", "{}"));
	CHECK(w.Add("crash/crash-1.log", "hello\n"));
	const auto zip = w.Finish();
	auto u32 = [&](std::size_t at) { return static_cast<std::uint32_t>(static_cast<unsigned char>(zip[at]) | static_cast<unsigned char>(zip[at + 1]) << 8 |
		                                                               static_cast<unsigned char>(zip[at + 2]) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(zip[at + 3])) << 24); };
	CHECK(u32(0) == 0x04034b50u);
	const auto eocd = zip.size() - 22;
	CHECK(u32(eocd) == 0x06054b50u);
	CHECK((static_cast<unsigned char>(zip[eocd + 10]) | static_cast<unsigned char>(zip[eocd + 11]) << 8) == 2);
	const auto dir = u32(eocd + 16);
	CHECK(u32(dir) == 0x02014b50u);
	CHECK(zip.find("crash/crash-1.log") != std::string::npos);
	CHECK(zip.find("hello\n") != std::string::npos);
}

static void ShareTests()
{
	namespace H = Vitals::Share;
	CHECK(H::KeepTail("abc", 10) == "abc");
	// a file read from byte 100: the count is of the file, plus the part line cut off
	CHECK(H::FileTail("rest of a line\nnext\n", 100) == "[Vitals: 115 older bytes left out; the newest part is kept]\nnext\n");
	CHECK(H::FileTail("whole\n", 0) == "whole\n");
	const auto tail = H::KeepTail("line1\nline2\nline3\n", 8);
	CHECK(tail.find("line3") != std::string::npos && tail.find("line1") == std::string::npos);
	const std::string log =
		"[11/15/2026 - 10:42:13AM] Papyrus log opened (PC-64)\n"
		"[11/15/2026 - 10:42:14AM] error: Unbound native function \"GetSUPF4SEVersion\" in object \"SUP_F4SE\" -- function will not be bound.\n"
		"[11/15/2026 - 10:42:14AM] error: Unbound native function \"GetSUPF4SEVersion\" in object \"SUP_F4SE\" -- function will not be bound.\n"
		"[11/15/2026 - 10:42:15AM] Cannot open store for class \"MissingScript\", missing file?\n"
		"[11/15/2026 - 10:42:16AM] error: Cannot call GetValue() on a None object, aborting function call\n"
		"stack:\n"
		"\t[Actor < (0001A4D7)>].WorkshopNPCScript.OnLoad() - \"WorkshopNPCScript.psc\" Line 12\n"
		"[11/15/2026 - 10:42:17AM] error: Cannot call GetValue() on a None object, aborting function call\n"
		"stack:\n"
		"\t[Actor < (0002B5E8)>].WorkshopNPCScript.OnLoad() - \"WorkshopNPCScript.psc\" Line 12\n"
		"[11/15/2026 - 10:42:18AM] warning: Property Name on script X attached to Nora Hale (00000014) cannot be bound\n";
	const auto sum = H::PapyrusSummary(log);
	CHECK(sum.find("2x  Unbound native function \"GetSUPF4SEVersion\" in object \"SUP_F4SE\"") != std::string::npos);
	CHECK(sum.find("2x  Cannot call GetValue() on a None object, aborting function call  @ WorkshopNPCScript.OnLoad()") != std::string::npos);
	CHECK(sum.find("MissingScript") != std::string::npos);
	CHECK(sum.find("0001A4D7") == std::string::npos);
	CHECK(sum.find("Nora") == std::string::npos);
	CHECK(sum.find("attached to <player> (#) cannot be bound") != std::string::npos);
	// the player's name in any sentence shape
	CHECK(H::detail::HidePlayerName("[Actor <Nora Hale (00000014)>].X.Y()") == "[Actor <<player> (00000014)>].X.Y()");
	CHECK(H::detail::HidePlayerName("Nate (00000014) is not in a workshop") == "<player> (00000014) is not in a workshop");
	CHECK(H::detail::HidePlayerName("Cannot call X() on xXslayerXx (00000014)") == "Cannot call X() on <player> (00000014)");
	CHECK(H::detail::HidePlayerName("Unable to bind Ann Marie Lee (00000014), then Bob (00000014)") ==
		"Unable to bind <player> (00000014), then <player> (00000014)");
	CHECK(H::detail::HidePlayerName("[Actor < (00000014)>]") == "[Actor < (00000014)>]");
	CHECK(H::detail::HidePlayerName("no player here (0001A4D7)") == "no player here (0001A4D7)");
	const std::string vortex = R"({"instance":"x","version":1,"stagingPath":"E:\\Vortex\\fallout4\\mods","files":[
		{"relPath":"F4SE\\Plugins\\XDI.dll","source":"XDI-123-1-0","target":"","time":1},
		{"relPath":"F4SE\\Plugins\\Sub\\nested.dll","source":"Other","time":2},
		{"relPath":"textures\\a.dds","source":"Tex","time":3},
		{"relPath":"F4SE/Plugins/Addictol.dll","source":"Addictol-84214-1-2","time":4}]})";
	const auto v = H::ParseVortexManifest(vortex);
	// a brace inside a string does not end the entry
	const auto braces = H::ParseVortexManifest(R"({"stagingPath":"S","files":[{"relPath":"F4SE\\Plugins\\A.dll","note":"a } b","source":"Mod {A}"},{"relPath":"F4SE\\Plugins\\B.dll","source":"B"}]})");
	CHECK(braces.dlls.size() == 2 && braces.dlls[0].second == "Mod {A}" && braces.dlls[1].second == "B");
	CHECK(v.staging == "E:\\Vortex\\fallout4\\mods");
	CHECK(v.dlls.size() == 2);
	CHECK(v.dlls.size() == 2 && v.dlls[0].first == "Addictol.dll" && v.dlls[1].second == "XDI-123-1-0");
	CHECK(H::DllSourcesText(v).find("XDI.dll | XDI-123-1-0") != std::string::npos);
	// breadcrumbs: header + 2 entries of a 4-slot ring
	std::string bin(64 + 4 * 128, '\0');
	auto put32 = [&](std::size_t at, std::uint32_t x) { std::memcpy(bin.data() + at, &x, 4); };
	auto put64 = [&](std::size_t at, std::uint64_t x) { std::memcpy(bin.data() + at, &x, 8); };
	std::memcpy(bin.data(), "VTLB", 4);
	put32(4, 1);
	put32(8, 4);
	put32(12, 128);
	put64(64 + 128 * 1, 7);
	std::memcpy(bin.data() + 64 + 128 + 24, "Vitals", 6);
	std::memcpy(bin.data() + 64 + 128 + 64, "doctor", 6);
	put64(64 + 128 * 3, 5);
	std::memcpy(bin.data() + 64 + 384 + 24, "Port", 4);
	std::memcpy(bin.data() + 64 + 384 + 64, "hook:A", 6);
	const auto crumbs = H::BreadcrumbsText(bin);
	CHECK(crumbs.find("5 | Port | hook:A\n7 | Vitals | doctor\n") != std::string::npos);
	CHECK(H::BreadcrumbsText("short").find("not a Vitals") != std::string::npos);
}

static void RuntimeTagTests()
{
	std::string kind;
	CHECK(RuleRuntimes("oldfile", "AE", kind) && kind == "oldfile");
	CHECK(RuleRuntimes("oldfile@NG,AE", "AE", kind) && kind == "oldfile");
	CHECK(RuleRuntimes("oldfile@ng,ae", "NG", kind));
	CHECK(!RuleRuntimes("oldfile@NG,AE", "OG", kind));
	CHECK(!RuleRuntimes("dll@OG", "AE", kind) && kind == "dll");
	CHECK(!RuleRuntimes("dll@", "AE", kind));
}

static void ModManagerTests()
{
	namespace M = Vitals::ModManagers;
	namespace F = Vitals::Formats;
	// linker floor: 14.0 (lld-link) and other majors say nothing; 14.44 is VS 2022 17.14 (May 2025)
	CHECK(F::LinkerFloor(14, 0).y == 0);
	CHECK(F::LinkerFloor(9, 0).y == 0);
	CHECK(F::LinkerFloor(14, 44).y == 2025 && F::LinkerFloor(14, 44).m == 5);
	CHECK(F::LinkerFloor(14, 40).y == 2024 && F::LinkerFloor(14, 40).m == 5);  // after the April 2024 Next-Gen update
	CHECK(F::LinkerFloor(14, 17).y == 2018);                                   // between table rows: the row below
	CHECK(F::LinkerFloor(14, 99).y == 2025);                                   // newer than the table: its last row
	// MO2 ini paths
	CHECK(M::Mo2ModsDir("[Settings]\nbase_directory=@ByteArray(D:/MO2/FO4)\n", "C:\\inst") == "D:\\MO2\\FO4\\mods");
	CHECK(M::Mo2ModsDir("[General]\ngameName=Fallout 4\n", "C:\\MO2") == "C:\\MO2\\mods");
	CHECK(M::Mo2ModsDir("[Settings]\nmod_directory=%BASE_DIR%/stuff\n", "C:\\MO2") == "C:\\MO2\\stuff");
	CHECK(M::Mo2ModsDir("", "").empty());
	const auto meta = M::Mo2Meta("[General]\r\nmodid=43627\r\nversion=4.9.0\r\nnewestVersion=5.0.0\r\n[installedFiles]\r\n1\\modid=1\r\n");
	CHECK(meta.version == "4.9.0" && meta.newest == "5.0.0" && meta.modId == "43627");
	CHECK(M::NewerVersion("5.0.0", "4.9.0"));
	CHECK(M::NewerVersion("1.10", "1.9"));
	CHECK(!M::NewerVersion("1.9", "1.10"));
	CHECK(!M::NewerVersion("5.0", "5.0.0"));
	CHECK(!M::NewerVersion("5.0.0", "5.0"));
	CHECK(M::NewerVersion("v2.1", "2.0"));
	CHECK(!M::NewerVersion("", "1.0"));
	CHECK(!M::NewerVersion("1.0", ""));
	CHECK(!M::NewerVersion("2.0-beta", "2.0"));   // a pre-release is not newer than its release
	CHECK(M::NewerVersion("2.0", "2.0-beta"));
	CHECK(!M::NewerVersion("1.0.0-beta", "1.0.0"));
	CHECK(!M::NewerVersion("1.0.0 RC2", "1.0"));
	CHECK(M::NewerVersion("1.2b", "1.2a"));
	CHECK(M::NewerVersion("1.2a", "1.2"));
	CHECK(!M::NewerVersion("1.2", "1.2a"));
	CHECK(!M::NewerVersion("1.0 fixed", "1.0"));      // text the numbers do not explain: not "newer"
	CHECK(M::NewerVersion("2.1-beta", "2.0"));        // higher numbers win whatever the text
	CHECK(!M::NewerVersion("Final", "1.0"));
	CHECK(!M::NewerVersion("010", "10"));
	// Qt's doubled backslashes, gamePath, the selected profile and its mod order
	CHECK(M::Mo2Path("@ByteArray(D:\\\\Games\\\\MO2\\\\)", {}) == "D:\\Games\\MO2");
	CHECK(M::Mo2GamePath("[General]\ngamePath=@ByteArray(D:\\\\Steam\\\\steamapps\\\\common\\\\Fallout 4)\n") == "D:\\Steam\\steamapps\\common\\Fallout 4");
	CHECK(M::SameDir("D:\\Steam\\steamapps\\common\\Fallout 4\\", "d:/steam/steamapps/common/fallout 4"));
	CHECK(!M::SameDir("D:\\Games\\Skyrim", "D:\\Games\\Fallout 4"));
	CHECK(!M::SameDir("", ""));
	CHECK(M::Mo2ProfileDir("[General]\nselected_profile=@ByteArray(Player)\n", "C:\\MO2") == "C:\\MO2\\profiles\\Player");
	CHECK(M::Mo2ProfileDir("", "C:\\MO2") == "C:\\MO2\\profiles\\Default");
	CHECK(M::Mo2ProfileDir("[Settings]\nprofiles_directory=%BASE_DIR%/p\n[General]\nselected_profile=X\n", "C:\\MO2") == "C:\\MO2\\p\\X");
	const auto ml = M::Mo2ModList("# This file was automatically generated by Mod Organizer.\r\n+Winner\r\n-Off Mod\r\n*DLC: Far Harbor\r\n+Loser\r\n");
	CHECK(ml.size() == 3 && ml[0].name == "Winner" && ml[0].enabled && !ml[1].enabled && ml[2].name == "Loser");
	// Vortex full state backup: persistent.mods.<game>.<id>; other games, other keys and odd values are skipped
	const auto vm = M::VortexStateMods(R"({"app":{"x":[1,2,{"a":null}]},"persistent":{"profiles":{},"mods":{
		"skyrimse":{"1":{"installationPath":"Other","attributes":{"version":"1","newestVersion":"2"}}},
		"fallout4":{
		  "BakaFramework-43627-5-0-0":{"id":"x","state":"installed","installationPath":"BakaFramework 43627 5.0.0 2026-09-22T22-55Z arQOmDNId",
		     "attributes":{"modId":43627,"version":"4.9.0","newestVersion":"5.0.0","name":"Baka \"F\" \u00e9","enabled":true,"x":[1.5e3,-2]}},
		  "Plain":{"installationPath":"Plain","attributes":{"version":"1.0"}}}}}})");
	CHECK(vm.size() == 2);
	CHECK(vm.contains("bakaframework 43627 5.0.0 2026-09-22t22-55z arqomdnid"));
	CHECK(vm.at("bakaframework 43627 5.0.0 2026-09-22t22-55z arqomdnid").newest == "5.0.0");
	CHECK(vm.at("plain").version == "1.0" && vm.at("plain").newest.empty());
	CHECK(M::VortexStateMods("{\"persistent\":{\"mods\":{\"fallout4\":{\"a\":{").empty());  // cut short: nothing, no crash
	CHECK(M::VortexStateMods("").empty());
}

// ---- Launch.h: last-launch recap, changes since the last launch, rule-file order, owners, DLL builds
static std::vector<std::string> Lines(std::initializer_list<const char*> a_list)
{
	return { a_list.begin(), a_list.end() };
}

static Launch::Snapshot Snap(std::initializer_list<const char*> a_lines)
{
	return Launch::Parse(Lines(a_lines), [] { return false; });
}

static void ReportBreakTests()
{
	// the break goes before the first detail sentence, whichever the DLL has
	CHECK(BreakBeforeDetails("X.dll has no version data. Built (date inside the DLL) 2019-01-02. File version 1.0.") == "X.dll has no version data.\n    Built (date inside the DLL) 2019-01-02. File version 1.0.");
	CHECK(BreakBeforeDetails("X.dll is not a plugin. File version 1.0.") == "X.dll is not a plugin.\n    File version 1.0.");
	// no build sentence and no version resource: the manager's sentences still break
	CHECK(BreakBeforeDetails("X.dll is not a plugin. Installed by the Vortex mod \"M\".") == "X.dll is not a plugin.\n    Installed by the Vortex mod \"M\".");
	CHECK(BreakBeforeDetails("X.dll is not a plugin. Vortex says a newer version of this mod is on Nexus: 2 (you have 1).") == "X.dll is not a plugin.\n    Vortex says a newer version of this mod is on Nexus: 2 (you have 1).");
	CHECK(BreakBeforeDetails("X.dll is not a plugin. 2 other installed mods have a working copy: A, B.") == "X.dll is not a plugin.\n    2 other installed mods have a working copy: A, B.");
	CHECK(BreakBeforeDetails("X.dll is not a plugin. The game is not loading the copy from \"M\", your highest-priority MO2 mod with this DLL: x.") == "X.dll is not a plugin.\n    The game is not loading the copy from \"M\", your highest-priority MO2 mod with this DLL: x.");
	// only one break, and none when there are no details
	CHECK(BreakBeforeDetails("X.dll is not a plugin. File version 1.0. Installed by the MO2 mod \"M\".") == "X.dll is not a plugin.\n    File version 1.0. Installed by the MO2 mod \"M\".");
	CHECK(BreakBeforeDetails("X.dll is not a plugin.") == "X.dll is not a plugin.");
	CHECK(BreakBeforeDetails("") == "");
}

static void LaunchTests()
{
	// Field reads strings (with escapes), numbers and booleans; a key inside a string value is not a field.
	CHECK(Launch::Field(R"({"a":"x\"y","b":12,"c":true})", "a") == "x\"y");
	CHECK(Launch::Field(R"({"a":"x\"y","b":12,"c":true})", "b") == "12");
	CHECK(Launch::Field(R"({"a":"x\"y","b":12,"c":true})", "c") == "true");
	CHECK(!Launch::Field(R"({"a":"x","b":1})", "z"));
	CHECK(!Launch::Field(R"({"a":"cut off)", "a"));
	CHECK(!Launch::Field(R"({"m":"has \"file\":\"q\" inside"})", "file"));

	CHECK(Launch::ParseIsoUtc("2026-11-16T14:27:03.123Z") == 1794839223);
	CHECK(Launch::ParseIsoUtc("1970-01-01T00:00:00Z") == 0);
	CHECK(!Launch::ParseIsoUtc("2026-13-01T00:00:00Z") && !Launch::ParseIsoUtc("junk") && !Launch::ParseIsoUtc(""));

	const auto prev = Snap({
		R"({"t":"2026-11-16T14:27:03.100Z","kind":"start","vitals_version":"0.4.1"})",
		R"({"t":"2026-11-16T14:27:04.000Z","kind":"environment","game_version":"1.11.240.0","f4se_version":"0.7.9.0"})",
		R"({"t":"x","kind":"plugin","file":"A.dll","size":1,"sha256":"aa","file_version":"1.0.0.0","in_process":true,"plugin_version":"1.0.0.0"})",
		R"({"t":"x","kind":"plugin","file":"B.dll","size":1,"sha256":"bb","file_version":"2.0.0.0","in_process":false})",
		R"({"t":"x","kind":"plugin","file":"Gone.dll","size":1,"sha256":"cc","file_version":"","in_process":false})",
		R"({"t":"x","kind":"doctor","rules":5,"problems":0,"warnings":1,"issues":1,"plugins_active":700,"ms":900})",
		R"({"t":"x","kind":"phase","event":"PostPostLoad","ms_since_vitals_load":"1000"})",
		R"({"t":"x","kind":"phase","event":"GameDataReady","ms_since_vitals_load":"97400"})",
		R"({"t":"x","kind":"phase","event":"PostLoadGame","ms_since_vitals_load":"158000"})",
		R"({"t":"x","kind":"plugin","file":"Cut.dll","sha25)",  // the game ended while this line was written
		"not json at all",
	});
	CHECK(prev.valid && prev.hasEnvironment && prev.dlls.size() == 3 && prev.activePlugins == 700 && prev.phases.size() == 3);
	CHECK(prev.dlls[0].version == "1.0.0.0" && prev.game == "1.11.240.0");

	// Recap: only what the records show.
	auto recap = Launch::Recap(prev, std::nullopt);
	CHECK(recap == "Last launch (2026-11-16 14:27 UTC): game data was ready (the main menu) after 97 s, a save loaded after 158 s. Vitals saw no later event.");
	CHECK(recap.find("crash") == std::string::npos);
	const auto crash = Launch::CrashLogAfter(*Launch::ParseIsoUtc(prev.startTime), { { "crash-2026-11-10.log", 100 }, { "Crash-new.LOG", *Launch::ParseIsoUtc(prev.startTime) + 46 * 60 + 5 },
		{ "f4se.log", *Launch::ParseIsoUtc(prev.startTime) + 60 }, { "crash-older.log", 5 } });
	CHECK(crash && crash->name == "Crash-new.LOG" && crash->minutesAfterStart == 46);
	CHECK(Launch::Recap(prev, crash).ends_with(" A crash log was written 46 minutes after that launch started: Crash-new.LOG."));
	CHECK(!Launch::CrashLogAfter(1000, { { "crash-a.log", 999 }, { "crashes.txt", 5000 } }));
	CHECK(Launch::CrashLogAfter(1000, { { "crash-a.log", 1030 } })->minutesAfterStart == 0);
	CHECK(Launch::Recap(prev, Launch::CrashLogAfter(1000, { { "crash-a.log", 1030 } })).find("less than a minute") != std::string::npos);
	const auto clean = Snap({ R"({"t":"2026-11-17T01:02:03Z","kind":"start"})", R"({"kind":"phase","event":"GameDataReady","ms_since_vitals_load":"80000"})",
		R"({"kind":"phase","event":"DeleteGame","ms_since_vitals_load":"90000"})" });
	CHECK(Launch::Recap(clean, std::nullopt) == "Last launch (2026-11-17 01:02 UTC): game data was ready (the main menu) after 80 s. The last thing Vitals saw was DeleteGame at 90 s.");
	CHECK(Launch::Recap(Snap({ R"({"t":"2026-11-17T01:02:03Z","kind":"start"})" }), std::nullopt) == "Last launch (2026-11-17 01:02 UTC): Vitals saw no game event.");
	CHECK(Launch::Recap(Snap({ "nothing" }), std::nullopt).empty());

	// Changes: compared by hash, never by date; skipped when the earlier launch has no DLL records.
	const auto now = Snap({
		R"({"t":"2026-11-17T01:00:00Z","kind":"start"})",
		R"({"kind":"environment","game_version":"1.11.240.0","f4se_version":"0.7.9.0"})",
		R"({"kind":"plugin","file":"a.dll","sha256":"AA2","file_version":"1.1.0.0","plugin_version":"1.1.0.0"})",
		R"({"kind":"plugin","file":"B.dll","sha256":"bb","file_version":"2.0.0.0"})",
		R"({"kind":"plugin","file":"New.dll","sha256":"dd","file_version":"3.0.0.0"})",
		R"({"kind":"doctor","plugins_active":703})",
	});
	const auto diff = Launch::Changes(prev, now);
	CHECK(diff.size() == 5);
	CHECK(diff[0] == "Replaced: a.dll (1.0.0.0 -> 1.1.0.0)");
	CHECK(diff[1] == "New: New.dll 3.0.0.0");
	CHECK(diff[2] == "Removed: Gone.dll");
	CHECK(diff[3] == "Active plugins: 700 -> 703");
	CHECK(diff[4] == "The game and F4SE are the same as last launch.");
	CHECK(Launch::Changes(now, now) == Lines({ "Nothing changed since the last launch." }));
	CHECK(Launch::Changes(Snap({ R"({"kind":"start"})" }), now).empty());  // the earlier launch recorded no DLLs
	auto noCount = now;
	noCount.activePlugins.reset();
	CHECK(Launch::Changes(prev, noCount).size() == 4);  // the count line shows only when both launches have it
	auto sameHashLaterDate = Snap({ R"({"kind":"start"})", R"({"kind":"environment","game_version":"1.11.240.0","f4se_version":"0.7.9.0"})",
		R"({"kind":"plugin","file":"A.dll","sha256":"aa","file_version":"1.0.0.0"})", R"({"kind":"plugin","file":"B.dll","sha256":"","file_version":"2.0.0.0"})" });
	CHECK(Launch::Changes(sameHashLaterDate, sameHashLaterDate) == Lines({ "Nothing changed since the last launch." }));
	auto newGame = now;
	newGame.game = "1.10.163.0";
	CHECK(Launch::Changes(now, newGame).size() == 1 && Launch::Changes(now, newGame)[0] == "The game changed: 1.11.240.0 -> 1.10.163.0");
	CHECK(Launch::Changes(Snap({ R"({"kind":"start"})", R"({"kind":"environment"})", R"({"kind":"plugin","file":"X.dll","sha256":"1","file_version":"1"})" }),
			  Snap({ R"({"kind":"start"})", R"({"kind":"environment"})", R"({"kind":"plugin","file":"X.dll","sha256":"2","file_version":"1"})" }))[0] ==
		  "Replaced: X.dll (version 1, different file)");
	// more than 10 entries: the rest is counted
	std::vector<std::string> many{ R"({"kind":"start"})", R"({"kind":"environment"})" };
	for (int i = 0; i < 13; ++i)
		many.push_back(R"({"kind":"plugin","file":"N)" + std::to_string(i) + R"(.dll","sha256":"x","file_version":"1"})");
	const auto big = Launch::Changes(Snap({ R"({"kind":"start"})", R"({"kind":"environment"})" }), Launch::Parse(many, [] { return false; }));
	CHECK(big.size() == 11 && big[10] == "and 3 more");
	// out of time: parsing stops, and what was read so far is used
	int calls = 0;
	std::vector<std::string> longFile(200, R"({"kind":"plugin","file":"Z.dll","sha256":"x"})");
	CHECK(Launch::Parse(longFile, [&] { return ++calls >= 2; }).dlls.size() == 127);

	// Rule files: undated first (name order), then dated, newest first.
	CHECK(Launch::GeneratedDate(Lines({ "# Vitals knowledge rules", "# Generated: 2026-11-17", "dll | a | - | info | m" })) == "2026-11-17");
	CHECK(Launch::GeneratedDate(Lines({ "; generated on 2025-01-02 by a tool" })) == "2025-01-02");
	CHECK(!Launch::GeneratedDate(Lines({ "# Generated by defib-census", "# on 2026-11-14", "# Generated: 2026-13-40" })));
	CHECK(!Launch::GeneratedDate(Lines({ "dll | Generated: 2026-11-17 | - | info | m" })));  // a rule line is not a header
	std::vector<Launch::RuleFile> files{ { "defib-census.txt", "2026-11-17", 16 }, { "defib-known.txt", std::nullopt, 6 }, { "nexus-rules.txt", "2026-12-01", 3 }, { "a-mine.txt", std::nullopt, 0 }, { "old.txt", "2026-11-17", 1 } };
	const auto order = Launch::RuleFileOrder(files);
	CHECK((order == std::vector<std::size_t>{ 3, 1, 2, 0, 4 }));
	CHECK(Launch::RulesStamp(files, order) == "Knowledge rules: 6 from defib-known.txt, 3 from nexus-rules.txt (generated 2026-12-01), 16 from defib-census.txt (generated 2026-11-17), 1 from old.txt (generated 2026-11-17) (the first file listed wins when two files have the same rule)");
	CHECK(Launch::RulesStamp({}, {}) == "Knowledge rules: none loaded");
	CHECK(Launch::RulesStamp({ { "x.txt", std::nullopt, 2 } }, { 0 }) == "Knowledge rules: 2 from x.txt");

	// Owners: named only when that DLL is running.
	const std::vector<Launch::Owner> owners{ { "MemoryManager::", "Addictol.dll" }, { "CheckModsLoaded", "AchievementsEnablerAE.dll" }, { "GetEncounterZone", "encounter_zone_recalculation.dll" } };
	auto running = [](const std::string& d) { return d == "addictol.dll" || d == "Addictol.dll"; };
	CHECK(Launch::PatchOwner(owners, "MemoryManager::AutoScrapBuffer::ctor", running) == "Addictol.dll");
	CHECK(Launch::PatchOwner(owners, "memorymanager::scrapheap::x", running) == "Addictol.dll");
	CHECK(!Launch::PatchOwner(owners, "TESObjectREFR::GetEncounterZone", running));  // its DLL is not running: no name is given
	CHECK(!Launch::PatchOwner(owners, "Papyrus::Something", running));

	// One build of a DLL (rule kind dllbuild).
	CHECK(Launch::PackedVersionText((0u << 24) | (0u << 16) | (4u << 4) | 13u) == "0.0.4.13");
	CHECK(Launch::DllBuildMatches("0.0.4.13", "0.0.4.13") && !Launch::DllBuildMatches("0.0.4.13", "0.0.4.12"));
	CHECK(Launch::DllBuildMatches("0.0.4", "0.0.4.13") && !Launch::DllBuildMatches("0.0.4", "0.0.5.0"));
	CHECK(!Launch::DllBuildMatches("", "1.0.0.0") && !Launch::DllBuildMatches("1.0.0.0.1", "1.0.0.0") && !Launch::DllBuildMatches("1..", "1.0.0.0"));
	CHECK(!Launch::DllBuildMatches("0.0.4.13", ""));
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
	ImageSearchTests();
	ScrubTests();
	ZipTests();
	ShareTests();
	RuntimeTagTests();
	ModManagerTests();
	ReportBreakTests();
	LaunchTests();
	const int loader = argc > 1 ? RunLoaderCases(argv[1]) : 0;
	// tests/scrub_cases.txt sits next to tests/loader_cases.txt
	int scrub = 0;
	if (argc > 1) {
		std::string dir = argv[1];
		const auto  slash = dir.find_last_of("/\\");
		dir = slash == std::string::npos ? std::string{} : dir.substr(0, slash + 1);
		scrub = RunScrubCases(dir + "scrub_cases.txt");
	}
	std::cout << (g_fail ? "unit checks FAILED: " : "unit checks ok") << (g_fail ? std::to_string(g_fail) : "") << "\n";
	return (g_fail || loader || scrub) ? 1 : 0;
}
