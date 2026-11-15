#include "Vitals.h"
#include "Vitals/Rules.h"

// In-game address check: the in-game half of Pulse Check.
// Reads Data\F4SE\Plugins\Vitals\addresses\*.txt. One entry per line:
//   name | kind | og_id | ng_id | ae_id | og_bytes | ng_bytes | ae_bytes
// kind is "fn" (code: compare the first bytes) or "ptr" (data: report the pointer and whether it is readable).
// Use "-" for an unknown ID or unknown expected bytes. Expected bytes are hex, "??" is a wildcard byte.
// Nothing in the game is ever written; an ID that is missing is reported as "unresolved".
//
// Local baselines: Documents\My Games\Fallout4\F4SE\Vitals\baselines\<RT>-<list>.txt (same format) fill
// expected bytes the shipped list leaves empty for this runtime. They live outside the game folder, so
// installing a new Vitals never wipes them. bCaptureBaseline=1 writes them from what is in memory, for
// builds whose Fallout4.exe is encrypted on disk (Steam 1.10.163). They are game data: never commit them.

namespace Vitals
{
	namespace
	{
		using Rules::Split;
		using Rules::Trim;

		// Returns: 1 match, 0 differs, -1 nothing to compare. a_diff gets the number of differing bytes. Only the bytes
		// that were read are compared: an expected value longer than iBytesToRead is checked over the bytes read
		// (a_unchecked says how many were left out), never counted as a difference. -2: the expected value is not hex
		// (an odd number of digits, or a pair that is not hex or "??"): a broken list line, not a byte difference.
		int CompareBytes(const std::string& a_expected, const std::uint8_t* a_actual, std::size_t a_len, std::size_t& a_diff, std::size_t& a_unchecked)
		{
			a_diff = 0;
			a_unchecked = 0;
			if (a_expected.empty() || a_expected == "-")
				return -1;
			std::string e;
			for (char c : a_expected)
				if (!std::isspace(static_cast<unsigned char>(c)))
					e += c;
			if (e.size() % 2)
				return -2;
			for (std::size_t i = 0; i < e.size(); i += 2)
				if (!(e[i] == '?' && e[i + 1] == '?') && !(std::isxdigit(static_cast<unsigned char>(e[i])) && std::isxdigit(static_cast<unsigned char>(e[i + 1]))))
					return -2;
			if (!a_len)
				return -1;
			for (std::size_t i = 0; i * 2 < e.size(); ++i) {
				if (i >= a_len) {
					++a_unchecked;
					continue;
				}
				const auto pair = e.substr(i * 2, 2);
				if (pair == "??")
					continue;
				unsigned v = 0;
				std::from_chars(pair.data(), pair.data() + 2, v, 16);  // checked above
				if (static_cast<std::uint8_t>(v) != a_actual[i])
					++a_diff;
			}
			return a_diff ? 0 : 1;
		}
	}

	namespace
	{
		struct PtrEntry
		{
			std::string   name;
			std::uint64_t id;
			std::uintptr_t addr;
			bool          readableAtCheck;
		};
		std::vector<PtrEntry> g_ptrs;

		// If the first instruction at a function is a jump, another plugin (or the game's own thunk)
		// has detoured it. Returns the jump target, or 0. Decodes the forms hook libraries write:
		// jmp rel32, jmp rel8, jmp [rip+disp32], mov rax/r11, imm64 + jmp rax/r11.
		std::uintptr_t JumpTarget(std::uintptr_t a_addr, const std::uint8_t* a_bytes, std::size_t a_len)
		{
			if (a_len >= 5 && a_bytes[0] == 0xE9) {  // jmp rel32
				std::int32_t rel;
				std::memcpy(&rel, a_bytes + 1, 4);
				return a_addr + 5 + rel;
			}
			if (a_len >= 2 && a_bytes[0] == 0xEB)  // jmp rel8
				return a_addr + 2 + static_cast<std::int8_t>(a_bytes[1]);
			if (a_len >= 6 && a_bytes[0] == 0xFF && a_bytes[1] == 0x25) {  // jmp [rip+disp32]
				std::int32_t disp;
				std::memcpy(&disp, a_bytes + 2, 4);
				std::uintptr_t target = 0;
				if (SafeRead(reinterpret_cast<const void*>(a_addr + 6 + disp), &target, sizeof(target)))
					return target;
			}
			if (a_len >= 12 && a_bytes[0] == 0x48 && a_bytes[1] == 0xB8 && a_bytes[10] == 0xFF && a_bytes[11] == 0xE0) {  // mov rax, imm64; jmp rax
				std::uintptr_t target;
				std::memcpy(&target, a_bytes + 2, 8);
				return target;
			}
			if (a_len >= 13 && a_bytes[0] == 0x49 && a_bytes[1] == 0xBB && a_bytes[10] == 0x41 && a_bytes[11] == 0xFF && a_bytes[12] == 0xE3) {  // mov r11, imm64; jmp r11
				std::uintptr_t target;
				std::memcpy(&target, a_bytes + 2, 8);
				return target;
			}
			return 0;
		}

		// Who a detour belongs to. A hook library usually jumps first into a small trampoline in memory it allocated
		// (no module owns it), and the trampoline jumps on into the plugin's DLL: follow up to 4 jumps to name the DLL.
		struct JumpOwner
		{
			std::uintptr_t target{ 0 };  // first jump target
			std::string    module;       // owning module file name, "?" if none
			std::string    category;     // game | system | plugin | other | unattributed
			std::uint32_t  hops{ 0 };
		};

		JumpOwner ResolveOwner(std::uintptr_t a_first)
		{
			JumpOwner o;
			o.target = a_first;
			auto     cur = a_first;
			std::filesystem::path path;
			for (std::uint32_t i = 0; i < 4 && cur; ++i) {
				o.hops = i + 1;
				path = ModulePathForAddress(reinterpret_cast<const void*>(cur));
				if (!path.empty())
					break;
				std::uint8_t b[16]{};
				if (!SafeRead(reinterpret_cast<const void*>(cur), b, sizeof(b)))
					break;
				cur = JumpTarget(cur, b, sizeof(b));
			}
			if (path.empty()) {
				o.module = "?";
				o.category = "unattributed";  // trampoline outside any DLL that does not jump on (some hook libraries)
				return o;
			}
			o.module = Narrow(path.filename().wstring());
			const auto full = Lower(Narrow(path.wstring()));
			static const auto exe = Lower(Narrow((GameDir() / L"Fallout4.exe").wstring()));
			static const auto plugins = Lower(Narrow(PluginDir().wstring()));
			if (full == exe || Lower(o.module) == "fallout4.exe")
				o.category = "game";
			else if (full.find("\\windows\\") != std::string::npos || Lower(o.module).starts_with("msvc") || Lower(o.module).starts_with("vcruntime"))
				o.category = "system";
			else if (full.size() > plugins.size() && full.starts_with(plugins) && (full[plugins.size()] == '\\' || full[plugins.size()] == '/'))
				o.category = "plugin";
			else
				o.category = "other";  // e.g. an ENB or ReShade d3d11.dll/dxgi.dll in the game folder
			return o;
		}
	}

	void RecheckPointers()
	{
		static bool done = false;
		if (done || g_ptrs.empty())
			return;
		done = true;
		std::size_t was = 0, now = 0;
		for (const auto& e : g_ptrs) {
			if (e.readableAtCheck)
				continue;
			++was;
			std::uintptr_t ptr = 0;
			const bool ok = SafeRead(reinterpret_cast<const void*>(e.addr), &ptr, sizeof(ptr)) && IsReadable(reinterpret_cast<const void*>(ptr), 8);
			now += ok;
			Emit("pointer_recheck", Json().Str("name", e.name).UNum("id", e.id).Bool("readable_now", ok).Str("pointer_now", std::format("0x{:X}", ptr)));
		}
		Summary(std::format("Pointer re-check after load: {} of {} singletons that were null at the menu are now set", now, was));
	}

	namespace
	{
		std::filesystem::path BaselineDir() { return OutputDir() / L"baselines"; }

		std::mutex                                g_resolvedLock;
		std::unordered_map<std::string, bool>     g_resolved;  // list entry name -> its ID resolves (filled by CheckAddresses)

		// Other F4SE plugins in the game. A baseline captured with them loaded would store their patches as "expected".
		std::vector<std::string> OtherPluginsLoaded()
		{
			std::vector<std::string> out;
			std::error_code          ec;
			for (const auto& e : ListDir(PluginDir())) {
				const auto name = Narrow(e.path().filename().wstring());
				if (Lower(Narrow(e.path().extension().wstring())) == ".dll" && Lower(name) != "vitals.dll" &&
					REX::W32::GetModuleHandleW(e.path().filename().c_str()))
					out.push_back(name);
			}
			return out;
		}

		// name -> expected bytes for this runtime, from a local baseline file (if any)
		std::unordered_map<std::string, std::string> LoadLocalBaseline(const std::filesystem::path& a_file, int a_rt)
		{
			std::unordered_map<std::string, std::string> out;
			std::ifstream in(a_file);
			std::string   line;
			while (std::getline(in, line)) {
				line = Trim(line);
				if (line.empty() || line[0] == '#' || line[0] == ';')
					continue;
				const auto f = Split(line, '|');
				const auto col = 5 + static_cast<std::size_t>(a_rt);
				if (f.size() > col && !f[col].empty() && f[col] != "-")
					out.emplace(f[0], f[col]);
			}
			return out;
		}
	}

	void CheckAddresses()
	{
		Crumb("Vitals", "address_check");
		if (const auto& al = AddressLibrary(); !al.usable) {
			// Nothing can be looked up. Reported here and by the doctor; the game keeps running.
			Emit("address_check_unavailable", Json().Str("address_library_file", al.file).Str("state", al.state).Str("problem", al.problem));
			const auto line = std::format("Address check skipped: Address Library {} is not usable ({}).", al.file, al.problem);
			Summary(line);
			DoctorAddressNote(line);
			return;
		}
		const auto dir = PluginDir() / L"Vitals" / L"addresses";
		const int  rt = RuntimeIndex();
		const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
		const auto nread = GetSettings().bytesToRead;
		bool       capture = GetSettings().captureBaseline;
		if (capture) {
			if (const auto others = OtherPluginsLoaded(); !others.empty()) {
				capture = false;
				Summary(std::format("Baseline capture SKIPPED: {} other F4SE plugin(s) are loaded (for example {}), and their patches would be saved as the expected bytes. "
				                    "Capture with only F4SE and Vitals installed.", others.size(), others.front()));
			}
		}

		std::size_t checked = 0, unresolved = 0, unreadable = 0, matched = 0, noBaseline = 0, hooked = 0, differs = 0, localUsed = 0, gameStubs = 0, patched = 0, partial = 0, malformed = 0;
		std::map<std::string, std::size_t> hooksByModule;
		std::error_code ec;
		for (const auto& entry : ListDir(dir)) {
			if (entry.path().extension() != L".txt")
				continue;
			const auto source = Narrow(entry.path().filename().wstring());
			const auto localFile = BaselineDir() / std::format("{}-{}", RuntimeName(), source);
			const auto local = LoadLocalBaseline(localFile, rt);
			if (!local.empty())
				Summary(std::format("Local baseline in use: baselines\\{}-{} ({} entries)", RuntimeName(), source, local.size()));

			std::ifstream in(entry.path());
			std::string   line;
			std::string   captured;  // rewritten list (bCaptureBaseline only)
			std::size_t   capturedCount = 0;
			while (std::getline(in, line)) {
				line = Trim(line);
				auto f = Split(line, '|');
				if (line.empty() || line[0] == '#' || line[0] == ';' || f.size() < 5) {
					if (capture)
						captured += line + "\n";
					continue;
				}
				f.resize(std::max<std::size_t>(f.size(), 8), "-");  // all columns exist; no reference is taken before this
				const std::string name = f[0];
				const std::string listedKind = f[1];
				const std::string idText = f[2 + rt];
				const auto        col = 5 + static_cast<std::size_t>(rt);
				std::string       expected = f[col];
				bool              fromLocal = false;
				if (expected.empty() || expected == "-") {
					if (const auto it = local.find(name); it != local.end()) {
						expected = it->second;
						fromLocal = true;
						++localUsed;
					}
				}
				++checked;

				Json j;
				j.Str("source", source).Str("name", name).Str("entry_kind", listedKind).Str("runtime", RuntimeName());
				auto finish = [&](Json& a_j) {
					if (capture) {
						std::string out;
						for (std::size_t i = 0; i < f.size(); ++i)
							out += (i ? "|" : "") + f[i];
						captured += out + "\n";
					}
					Emit("address", std::move(a_j));
				};
				if (idText.empty() || idText == "-") {
					j.Str("result", "no_id_for_runtime");
					++unresolved;
					finish(j);
					continue;
				}
				std::uint64_t id = 0;
				if (std::from_chars(idText.data(), idText.data() + idText.size(), id).ec != std::errc{}) {
					j.Str("result", "bad_id_text").Str("id_text", idText);
					++unresolved;
					finish(j);
					continue;
				}
				j.UNum("id", id);
				const auto off = LookupOffset(id);
				{
					std::scoped_lock l(g_resolvedLock);
					g_resolved[name] = off.has_value();
				}
				if (!off) {
					j.Str("result", "unresolved");
					++unresolved;
					finish(j);
					Summary(std::format("  UNRESOLVED {} (id {}) from {}", name, id, source));
					continue;
				}
				const auto addr = base + *off;
				j.Str("offset", std::format("0x{:X}", *off));

				std::uint8_t bytes[64]{};
				if (!SafeRead(reinterpret_cast<const void*>(addr), bytes, nread)) {
					j.Str("result", "unreadable");
					++unreadable;
					finish(j);
					continue;
				}
				j.Str("bytes", Hex(bytes, nread));

				// The generated list calls every "Singleton" ID a pointer, but on these builds some of them are the
				// GetSingleton() accessor (code) and some are the object itself (stored in place). Classify by memory.
				std::string kind = listedKind;
				// The list generator guesses "fn" for every non-singleton ID, but some are globals (pointers that move with
				// ASLR or point into the heap). Code lives in executable pages; anything else is data, never byte-compared.
				if (kind == "fn" && !IsExecutable(reinterpret_cast<const void*>(addr))) {
					kind = "data";
					j.Str("ptr_class", "listed_as_code_but_is_data");
				}
				if (kind == "ptr") {
					std::uintptr_t ptr = 0;
					std::memcpy(&ptr, bytes, sizeof(ptr));
					if (IsExecutable(reinterpret_cast<const void*>(addr))) {
						kind = "fn";
						j.Str("ptr_class", "accessor_function");
					} else if (IsReadable(reinterpret_cast<const void*>(ptr), 8)) {
						j.Str("ptr_class", "pointer").Str("pointer", std::format("0x{:X}", ptr)).Bool("pointer_readable", true);
					} else if (ptr == 0) {
						j.Str("ptr_class", "null_at_check").Bool("pointer_readable", false);
						g_ptrs.push_back({ name, id, addr, false });
					} else {
						j.Str("ptr_class", "object_in_place").Str("first_qword", std::format("0x{:X}", ptr));
					}
				}
				j.Str("checked_as", kind);
				if (fromLocal)
					j.Bool("expected_from_local_baseline", true);

				const auto  jump = kind == "fn" ? JumpTarget(addr, bytes, nread) : 0;
				const auto  owner = jump ? ResolveOwner(jump) : JumpOwner{};
				const auto& jumpOwner = owner.module;
				// Only a jump into a plugin (or an unowned trampoline, or another injected DLL) is a hook by a mod.
				// Jumps into the game itself or the C runtime are the game's own stubs in a clean install.
				const bool  byMod = jump && (owner.category == "plugin" || owner.category == "unattributed" || owner.category == "other");
				const auto  stub = kind == "fn" && !jump ? Rules::ReturnStub(bytes, nread) : std::string{};
				if (capture && kind == "fn" && !jump && stub.empty() && (expected.empty() || expected == "-")) {
					f[col] = Hex(bytes, nread);
					++capturedCount;
				} else if (capture && fromLocal) {
					f[col] = expected;  // keep what the local baseline already had
				}
				if (kind != "fn") {
					j.Str("result", "checked_data");  // data is expected to change at run time: no byte compare
					finish(j);
					continue;
				}
				std::size_t diffCount = 0, unchecked = 0;
				const auto  cmp = CompareBytes(expected, bytes, nread, diffCount, unchecked);
				if (unchecked) {
					j.UNum("expected_bytes_not_checked", unchecked);  // iBytesToRead is shorter than the expected value
					++partial;
				}
				switch (cmp) {
				case 1:
					j.Str("result", "match");
					++matched;
					break;
				case 0:
					if (byMod) {
						// Not an error: a mod detoured this function. Report who.
						j.Str("result", "hooked").Str("expected", expected)
							.Str("jump_target", std::format("0x{:X}", jump)).Str("hooked_by", jumpOwner)
							.Str("owner_category", owner.category).UNum("jump_hops", owner.hops);
						++hooked;
						++hooksByModule[jumpOwner];
						break;
					}
					if (!stub.empty()) {
						// Switched off in place: the function now returns at once (a fix mod's patch). No owner in the bytes.
						j.Str("result", "patched_in_place").Str("expected", expected).Str("stub", stub);
						++patched;
						break;
					}
					// Patched in place, redirected inside the game, or a wrong address. Informational; it matters only if a
					// port uses this ID.
					j.Str("result", "differs").Str("expected", expected).UNum("bytes_different", diffCount);
					if (jump)
						j.Str("jump_target", std::format("0x{:X}", jump)).Str("hooked_by", jumpOwner).Str("owner_category", owner.category);
					++differs;
					break;
				case -2:
					j.Str("result", "bad_expected_value").Str("expected", expected);
					++malformed;
					break;
				default:
					if (jump) {
						j.Str("jump_target", std::format("0x{:X}", jump)).Str("hooked_by", jumpOwner)
							.Str("owner_category", owner.category).UNum("jump_hops", owner.hops);
						if (byMod)
							++hooksByModule[jumpOwner];
						else
							++gameStubs;  // starts with a jump into the game or the C runtime: the game's own stub
					}
					if (!stub.empty())
						j.Str("stub", stub);
					j.Str("result", "resolved_no_baseline");
					++noBaseline;
				}
				finish(j);
			}
			if (capture && capturedCount) {
				std::error_code e2;
				std::filesystem::create_directories(BaselineDir(), e2);
				std::ofstream(localFile, std::ios::trunc)
					<< std::format("# Vitals baseline captured from memory on {} ({}). Game data: keep local, never commit.\n", RuntimeName(), source)
					<< captured;
				Summary(std::format("Baseline captured from memory: baselines\\{}-{} ({} new entries). Set bCaptureBaseline=0 now.", RuntimeName(), source, capturedCount));
			}
		}
		const auto addrLine = std::format("Address check: {} checked, {} match, {} hooked by another plugin, {} switched off in place, {} differ, {} without baseline ({} of them the game's own jump stubs), {} unresolved, {} unreadable{}{}{}",
			checked, matched, hooked, patched, differs, noBaseline, gameStubs, unresolved, unreadable,
			localUsed ? std::format(" ({} expected values from the local baseline)", localUsed) : ""s,
			partial ? std::format(" ({} compared over iBytesToRead={} bytes only)", partial, nread) : ""s,
			malformed ? std::format(", {} list lines with an expected value that is not hex (fix the list)", malformed) : ""s);
		Summary(addrLine);
		DoctorAddressNote(addrLine);
		if (!hooksByModule.empty()) {
			std::vector<std::pair<std::string, std::size_t>> v(hooksByModule.begin(), hooksByModule.end());
			std::ranges::sort(v, [](auto& a, auto& b) { return a.second > b.second; });
			std::string line;
			for (const auto& [m, n] : v)
				line += std::format("{}{} {}", line.empty() ? "" : ", ", m == "?" ? "unattributed trampoline" : m, n);
			Summary("Hooked functions by plugin: " + line);
			DoctorAddressNote("Functions detoured by mods (from the checked list): " + line);
			std::vector<std::string> items;
			for (const auto& [m, n] : v)
				items.push_back(Json().Str("module", m).UNum("functions", n).Build());
			Emit("hooks_by_module", Json().Raw("modules", Json::Array(items)));
		}
	}

	bool LibraryIdResolves(std::string_view a_listName)
	{
		if (!AddressLibrary().usable)
			return false;  // CommonLib would end the game on its first lookup
		{
			std::scoped_lock l(g_resolvedLock);
			if (const auto it = g_resolved.find(std::string(a_listName)); it != g_resolved.end())
				return it->second;
		}
		// Address check off or not run yet: look the entry up in the shipped list directly.
		std::ifstream in(PluginDir() / L"Vitals" / L"addresses" / L"commonlibf4.txt");
		std::string   line;
		const auto    rt = static_cast<std::size_t>(RuntimeIndex());
		while (std::getline(in, line)) {
			const auto f = Split(line, '|');
			if (f.size() < 5 || f[0] != a_listName)
				continue;
			std::uint64_t id = 0;
			const auto&   t = f[2 + rt];
			return std::from_chars(t.data(), t.data() + t.size(), id).ec == std::errc{} && LookupOffset(id).has_value();
		}
		return false;  // not in the list: do not risk it
	}
}
