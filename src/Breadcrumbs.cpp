#include "Vitals.h"

// Breadcrumb ring. File: Documents\My Games\Fallout4\F4SE\Vitals\breadcrumbs.bin
// Layout (little endian), read by tools/defib_logs.py:
//   header 64 bytes: magic "VTLB", u32 version=1, u32 capacity, u32 entry_size, u64 next_seq, rest zero
//   entries capacity * 128 bytes: u64 seq, i64 qpc, u32 thread_id, u32 pad, char port[40], char where[64], 0-pad
// A seq of 0 means empty. The newest entry is the one with the highest seq.
// The previous session's file is kept as breadcrumbs.prev.bin.

namespace Vitals
{
	namespace
	{
		constexpr std::uint32_t kCapacity = 256;
		constexpr std::uint32_t kEntrySize = 128;

		struct Header
		{
			char          magic[4];
			std::uint32_t version;
			std::uint32_t capacity;
			std::uint32_t entrySize;
			std::uint64_t nextSeq;
			std::uint8_t  pad[40];
		};
		static_assert(sizeof(Header) == 64);

		struct Entry
		{
			std::uint64_t seq;
			std::int64_t  qpc;
			std::uint32_t threadId;
			std::uint32_t pad;
			char          port[40];
			char          where[64];
		};
		static_assert(sizeof(Entry) == kEntrySize);

		Header* g_header{ nullptr };
		Entry*  g_entries{ nullptr };

		void CopyStr(char* a_dst, std::size_t a_cap, const char* a_src)
		{
			std::size_t i = 0;
			if (a_src)
				for (; i + 1 < a_cap && a_src[i]; ++i)
					a_dst[i] = a_src[i];
			for (; i < a_cap; ++i)
				a_dst[i] = '\0';
		}
	}

	void InitBreadcrumbs()
	{
		if (!GetSettings().breadcrumbs || g_header)
			return;
		std::error_code ec;
		const auto      dir = OutputDir();
		std::filesystem::create_directories(dir, ec);
		const auto path = dir / L"breadcrumbs.bin";
		if (std::filesystem::exists(path, ec))
			std::filesystem::rename(path, dir / L"breadcrumbs.prev.bin", ec);

		const std::uint32_t size = sizeof(Header) + kCapacity * kEntrySize;
		auto file = REX::W32::CreateFileW(path.c_str(),
			static_cast<std::uint32_t>(REX::W32::GENERIC_READ | REX::W32::GENERIC_WRITE),
			REX::W32::FILE_SHARE_READ, nullptr, REX::W32::OPEN_ALWAYS, 0x80 /*NORMAL*/, nullptr);
		if (file == REX::W32::INVALID_HANDLE_VALUE) {
			REX::WARN("breadcrumbs: could not create file");
			return;
		}
		auto map = REX::W32::CreateFileMappingW(file, nullptr, REX::W32::PAGE_READWRITE, 0, size, nullptr);
		REX::W32::CloseHandle(file);  // the mapping keeps the file open
		if (!map) {
			REX::WARN("breadcrumbs: could not map file");
			return;
		}
		auto view = static_cast<std::uint8_t*>(REX::W32::MapViewOfFile(map, REX::W32::FILE_MAP_WRITE, 0, 0, size));
		if (!view) {
			REX::WARN("breadcrumbs: could not map a view of the file");
			REX::W32::CloseHandle(map);
			return;
		}
		// on success the mapping handle is kept open on purpose for the life of the process
		std::memset(view, 0, size);
		g_header = reinterpret_cast<Header*>(view);
		g_entries = reinterpret_cast<Entry*>(view + sizeof(Header));
		std::memcpy(g_header->magic, "VTLB", 4);
		g_header->version = 1;
		g_header->capacity = kCapacity;
		g_header->entrySize = kEntrySize;
		g_header->nextSeq = 1;
	}

	void Crumb(const char* a_port, const char* a_where)
	{
		if (!g_header)
			return;
		const auto seq = std::atomic_ref<std::uint64_t>(g_header->nextSeq).fetch_add(1);
		auto&      e = g_entries[seq % kCapacity];
		e.seq = 0;  // mark in-progress so a torn entry is never read as valid
		REX::W32::QueryPerformanceCounter(&e.qpc);
		e.threadId = REX::W32::GetCurrentThreadId();
		CopyStr(e.port, sizeof(e.port), a_port);
		CopyStr(e.where, sizeof(e.where), a_where);
		std::atomic_ref<std::uint64_t>(e.seq).store(seq, std::memory_order_release);
	}
}

// Exported for ports (see include/Vitals/Breadcrumbs.h).
extern "C" __declspec(dllexport) void Vitals_Breadcrumb(const char* a_port, const char* a_where)
{
	Vitals::Crumb(a_port, a_where);
}
