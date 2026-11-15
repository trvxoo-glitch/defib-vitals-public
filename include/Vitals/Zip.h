#pragma once
// A minimal zip writer for the share pack: "stored" entries only (no compression), UTF-8 names, no zip64.
// Stored entries keep the format trivial to read back
// and the share pack is small text. Plain C++20; tested in tests/cpp/rules_test.cpp.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Vitals::Zip
{
	inline std::uint32_t Crc32(std::string_view a_data)
	{
		static const auto table = [] {
			std::array<std::uint32_t, 256> t{};
			for (std::uint32_t n = 0; n < 256; ++n) {
				std::uint32_t c = n;
				for (int k = 0; k < 8; ++k)
					c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				t[n] = c;
			}
			return t;
		}();
		std::uint32_t c = 0xFFFFFFFFu;
		for (const unsigned char b : a_data)
			c = table[(c ^ b) & 0xFF] ^ (c >> 8);
		return c ^ 0xFFFFFFFFu;
	}

	// MS-DOS date and time, as zip stores them.
	struct DosTime
	{
		std::uint16_t time{ 0 };
		std::uint16_t date{ (2026 - 1980) << 9 | 1 << 5 | 1 };
		static DosTime From(int a_year, int a_month, int a_day, int a_hour = 0, int a_min = 0, int a_sec = 0)
		{
			DosTime t;
			t.date = static_cast<std::uint16_t>(((a_year - 1980) & 0x7F) << 9 | (a_month & 0xF) << 5 | (a_day & 0x1F));
			t.time = static_cast<std::uint16_t>((a_hour & 0x1F) << 11 | (a_min & 0x3F) << 5 | ((a_sec / 2) & 0x1F));
			return t;
		}
	};

	class Writer
	{
	public:
		explicit Writer(DosTime a_when = {}) : _when(a_when) {}

		// Entry names use '/' and must be relative. Returns false (and adds nothing) past 4 GB or 65,535 entries.
		bool Add(std::string_view a_name, std::string_view a_data)
		{
			if (a_data.size() > 0xFFFFFFFEu || _entries.size() >= 0xFFFF || _out.size() + a_data.size() + a_name.size() + 64 > 0xFFFFFFFEu)
				return false;
			Entry e{ std::string(a_name), Crc32(a_data), static_cast<std::uint32_t>(a_data.size()), static_cast<std::uint32_t>(_out.size()) };
			U32(0x04034b50);
			U16(20);       // version needed: 2.0
			U16(0x0800);   // flags: UTF-8 names
			U16(0);        // stored
			U16(_when.time);
			U16(_when.date);
			U32(e.crc);
			U32(e.size);
			U32(e.size);
			U16(static_cast<std::uint16_t>(e.name.size()));
			U16(0);
			_out += e.name;
			_out.append(a_data);
			_entries.push_back(std::move(e));
			return true;
		}

		std::string Finish()
		{
			const auto dirStart = static_cast<std::uint32_t>(_out.size());
			for (const auto& e : _entries) {
				U32(0x02014b50);
				U16(20);  // made by: 2.0 (MS-DOS attributes)
				U16(20);
				U16(0x0800);
				U16(0);
				U16(_when.time);
				U16(_when.date);
				U32(e.crc);
				U32(e.size);
				U32(e.size);
				U16(static_cast<std::uint16_t>(e.name.size()));
				U16(0);  // extra
				U16(0);  // comment
				U16(0);  // disk
				U16(0);  // internal attributes
				U32(0);  // external attributes
				U32(e.offset);
				_out += e.name;
			}
			const auto dirSize = static_cast<std::uint32_t>(_out.size()) - dirStart;
			U32(0x06054b50);
			U16(0);
			U16(0);
			U16(static_cast<std::uint16_t>(_entries.size()));
			U16(static_cast<std::uint16_t>(_entries.size()));
			U32(dirSize);
			U32(dirStart);
			U16(0);
			return std::move(_out);
		}

	private:
		struct Entry
		{
			std::string   name;
			std::uint32_t crc, size, offset;
		};

		void U16(std::uint16_t a_v)
		{
			_out += static_cast<char>(a_v & 0xFF);
			_out += static_cast<char>(a_v >> 8);
		}
		void U32(std::uint32_t a_v)
		{
			U16(static_cast<std::uint16_t>(a_v & 0xFFFF));
			U16(static_cast<std::uint16_t>(a_v >> 16));
		}

		DosTime            _when;
		std::string        _out;
		std::vector<Entry> _entries;
	};
}
