#pragma once
// Readers for the files Vitals inspects on disk: DLLs (PE), the Address Library table, and plugin (.esm/.esp/.esl)
// headers. Plain C++20 and the standard library only, like Rules.h, so the same code that ships is fuzzed and tested
// by tests/cpp/rules_test.cpp on any machine.
//
// Every reader takes the file's bytes and never trusts a size, count or offset read from them: all bounds checks
// subtract from the buffer size instead of adding to an offset, so a crafted value cannot wrap around.

#include "Vitals/Rules.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Vitals::Formats
{
	using Bytes = std::span<const std::uint8_t>;

	// Reads a T at a_off if all of it is inside a_d. Overflow-safe for any a_off.
	template <class T>
	bool Get(Bytes a_d, std::uint64_t a_off, T& a_out)
	{
		if (a_off > a_d.size() || sizeof(T) > a_d.size() - a_off)
			return false;
		std::memcpy(&a_out, a_d.data() + a_off, sizeof(T));
		return true;
	}

	// ---------------------------------------------------------------- PE (a DLL on disk, never loaded)
	struct PeSection
	{
		std::uint32_t va{ 0 }, vsize{ 0 }, raw{ 0 }, rsize{ 0 };
	};

	struct PeInfo
	{
		bool                                 ok{ false };
		std::uint32_t                        timestamp{ 0 };  // COFF TimeDateStamp: when the DLL was built (0/garbage in reproducible builds)
		bool                                 reproducible{ false };  // a reproducible build (/Brepro): the timestamp is a hash, not a date
		std::string                          error;
		std::map<std::string, std::uint32_t> exports;  // name -> RVA
		std::vector<PeSection>               sections;
	};

	inline std::optional<std::uint64_t> RvaToOff(const PeInfo& a_pe, std::uint32_t a_rva)
	{
		for (const auto& s : a_pe.sections) {
			const std::uint64_t end = std::uint64_t{ s.va } + std::max(s.vsize, s.rsize);  // 64-bit: cannot wrap
			if (a_rva >= s.va && a_rva < end) {
				const std::uint64_t delta = a_rva - s.va;
				if (delta >= s.rsize)
					return std::nullopt;  // in the section's memory, but not in the file
				return std::uint64_t{ s.raw } + delta;
			}
		}
		return std::nullopt;
	}

	inline PeInfo ParsePe(Bytes d)
	{
		PeInfo        pe;
		std::uint32_t peOff = 0;
		std::uint32_t sig = 0;
		if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z' || !Get(d, 0x3C, peOff) || !Get(d, peOff, sig) || sig != 0x00004550 /* "PE\0\0" */) {
			pe.error = "not a Windows DLL";
			return pe;
		}
		std::uint16_t nsec = 0, optSize = 0, magic = 0;
		Get(d, std::uint64_t{ peOff } + 8, pe.timestamp);
		Get(d, std::uint64_t{ peOff } + 6, nsec);
		Get(d, std::uint64_t{ peOff } + 20, optSize);
		const std::uint64_t opt = std::uint64_t{ peOff } + 24;
		if (!Get(d, opt, magic)) {
			pe.error = "not a Windows DLL";
			return pe;
		}
		if (magic != 0x20B) {
			pe.error = "a 32-bit DLL (Fallout 4 is 64-bit, so it can never load)";
			return pe;
		}
		std::uint32_t expRva = 0;
		Get(d, opt + 112, expRva);
		for (std::uint16_t i = 0; i < nsec; ++i) {
			const std::uint64_t o = opt + optSize + i * 40ull;
			PeSection           s;
			if (!Get(d, o + 8, s.vsize) || !Get(d, o + 12, s.va) || !Get(d, o + 16, s.rsize) || !Get(d, o + 20, s.raw))
				break;
			pe.sections.push_back(s);
		}
		pe.ok = true;
		{
			// Reproducible builds (/Brepro) fill the timestamp with a hash that can look like any date. They say so with a
			// debug directory entry of type 16 (IMAGE_DEBUG_TYPE_REPRO).
			std::uint32_t dbgRva = 0, dbgSize = 0;
			Get(d, opt + 160, dbgRva);
			Get(d, opt + 164, dbgSize);
			if (const auto dbg = dbgRva ? RvaToOff(pe, dbgRva) : std::nullopt; dbg && dbgSize <= 4096)
				for (std::uint32_t k = 0; k + 28 <= dbgSize; k += 28) {
					std::uint32_t type = 0;
					if (Get(d, *dbg + k + 12, type) && type == 16)
						pe.reproducible = true;
				}
		}
		const auto eo = expRva ? RvaToOff(pe, expRva) : std::nullopt;
		if (!eo)
			return pe;
		std::uint32_t nFuncs = 0, nNames = 0, funcsRva = 0, namesRva = 0, ordsRva = 0;
		Get(d, *eo + 20, nFuncs);
		Get(d, *eo + 24, nNames);
		Get(d, *eo + 28, funcsRva);
		Get(d, *eo + 32, namesRva);
		Get(d, *eo + 36, ordsRva);
		const auto fo = RvaToOff(pe, funcsRva), no = RvaToOff(pe, namesRva), oo = RvaToOff(pe, ordsRva);
		if (!fo || !no || !oo)
			return pe;
		for (std::uint32_t i = 0; i < nNames && i < 65536; ++i) {
			std::uint32_t nameRva = 0, funcRva = 0;
			std::uint16_t ord = 0;
			if (!Get(d, *no + 4ull * i, nameRva) || !Get(d, *oo + 2ull * i, ord) || ord >= nFuncs || !Get(d, *fo + 4ull * ord, funcRva))
				continue;
			const auto so = RvaToOff(pe, nameRva);
			if (!so)
				continue;
			std::string name;
			for (std::uint64_t k = *so; k < d.size() && d[k] && name.size() < 256; ++k)
				name += static_cast<char>(d[k]);
			pe.exports.emplace(std::move(name), funcRva);
		}
		return pe;
	}

	// F4SEPluginVersionData (f4se/PluginAPI.h), as F4SE 0.7.x reads it from the file and from memory.
	struct VersionDataLayout
	{
		std::uint32_t dataVersion;             // 0x000
		std::uint32_t pluginVersion;           // 0x004
		char          name[256];               // 0x008
		char          author[256];             // 0x108
		std::uint32_t addressIndependence;     // 0x208
		std::uint32_t structureIndependence;   // 0x20C
		std::uint32_t compatibleVersions[16];  // 0x210
		std::uint32_t seVersionRequired;       // 0x250
		std::uint32_t reservedNonBreaking;     // 0x254
		std::uint32_t reservedBreaking;        // 0x258
	};
	static_assert(sizeof(VersionDataLayout) == 0x25C);

	inline Rules::Loader::VersionData ToVersionData(const VersionDataLayout& a_raw)
	{
		Rules::Loader::VersionData v;
		v.dataVersion = a_raw.dataVersion;
		v.hasName = a_raw.name[0] != 0;
		v.addr = a_raw.addressIndependence;
		v.structs = a_raw.structureIndependence;
		for (const auto c : a_raw.compatibleVersions) {
			if (!c)
				break;
			v.compat.push_back(c);
		}
		v.f4seRequired = a_raw.seVersionRequired;
		v.reservedBreaking = a_raw.reservedBreaking;
		return v;
	}

	inline std::optional<VersionDataLayout> ReadVersionDataRaw(Bytes d, const PeInfo& a_pe)
	{
		const auto it = a_pe.exports.find("F4SEPlugin_Version");
		if (it == a_pe.exports.end())
			return std::nullopt;
		const auto        off = RvaToOff(a_pe, it->second);
		VersionDataLayout raw{};
		if (!off || !Get(d, *off, raw))
			return std::nullopt;
		return raw;
	}

	// ---------------------------------------------------------------- Address Library (F4SE "version-X-Y-Z-W.bin")
	// Format 0, the only one F4SE uses on every runtime: a u64 count, then count {u64 id, u64 offset} pairs sorted by id.
	// There is no version header: the file name is the game version. CommonLib maps the file and trusts the count, so a
	// truncated file makes it read past the end, and an unsorted one makes its lookups miss and end the game.
	namespace AddressLib
	{
		enum class State
		{
			ok,
			too_small,  // shorter than the 8-byte count
			empty,      // count is 0
			truncated,  // the count says more entries than the file holds
			unsorted,   // ids not strictly increasing (lookups would miss)
		};

		inline const char* Name(State a_s)
		{
			switch (a_s) {
			case State::ok: return "ok";
			case State::too_small: return "too_small";
			case State::empty: return "empty";
			case State::truncated: return "truncated";
			case State::unsorted: return "unsorted";
			}
			return "?";
		}

		struct Table
		{
			State         state{ State::too_small };
			std::uint64_t count{ 0 };
		};

		inline Table Check(Bytes d)
		{
			Table t;
			if (!Get(d, 0, t.count))
				return t;
			if (!t.count) {
				t.state = State::empty;
				return t;
			}
			if (t.count > (d.size() - 8) / 16) {
				t.state = State::truncated;
				return t;
			}
			std::uint64_t prev = 0;
			for (std::uint64_t i = 0; i < t.count; ++i) {
				std::uint64_t id = 0;
				std::memcpy(&id, d.data() + 8 + i * 16, 8);
				if (i && id <= prev) {
					t.state = State::unsorted;
					return t;
				}
				prev = id;
			}
			t.state = State::ok;
			return t;
		}

		// Binary search of a table that passed Check.
		inline std::optional<std::uint64_t> Find(Bytes d, std::uint64_t a_count, std::uint64_t a_id)
		{
			std::uint64_t lo = 0, hi = a_count;
			while (lo < hi) {
				const auto    mid = lo + (hi - lo) / 2;
				std::uint64_t id = 0, off = 0;
				if (!Get(d, 8 + mid * 16, id) || !Get(d, 16 + mid * 16, off))
					return std::nullopt;
				if (id == a_id)
					return off;
				(id < a_id ? lo = mid + 1 : hi = mid);
			}
			return std::nullopt;
		}
	}

	// ---------------------------------------------------------------- plugin header (TES4 record)
	struct PluginHeader
	{
		bool                     ok{ false };
		bool                     light{ false };
		bool                     master{ false };  // ESM flag (or .esm/.esl): the game loads it before every non-master
		std::vector<std::string> masters;          // UTF-8
	};

	// a_ext: the file's extension, lower case with the dot (".esp").
	inline PluginHeader ParseTes4(Bytes d, std::string_view a_ext)
	{
		PluginHeader  h;
		std::uint32_t sig = 0, size = 0, flags = 0;
		if (!Get(d, 0, sig) || sig != 0x34534554 /* "TES4" */ || d.size() < 24)
			return h;
		Get(d, 4, size);
		Get(d, 8, flags);
		h.ok = true;
		h.master = flags & 0x1;
		h.light = flags & 0x200;
		std::uint64_t       o = 24;
		const std::uint64_t end = std::min<std::uint64_t>(d.size(), 24ull + size);
		while (end >= 6 && o <= end - 6) {
			std::uint16_t len = 0;
			Get(d, o + 4, len);
			if (std::memcmp(d.data() + o, "MAST", 4) == 0) {
				std::string name;
				for (std::uint64_t k = o + 6; k < o + 6 + len && k < d.size() && d[k]; ++k)
					name += static_cast<char>(d[k]);
				h.masters.push_back(Rules::PluginNameToUtf8(name));  // UTF-8 if valid, else the Windows code page
			}
			o += 6ull + len;
		}
		if (a_ext == ".esl")
			h.light = true;
		if (a_ext == ".esm" || a_ext == ".esl")
			h.master = true;
		return h;
	}
}
