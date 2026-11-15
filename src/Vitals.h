#pragma once
// Vitals: shared declarations. Everything here only READS game memory.

namespace Vitals
{
	// ---- Settings (Data\F4SE\Plugins\Vitals.ini) ----
	struct Settings
	{
		bool environment{ true };     // environment report at startup
		bool addressCheck{ true };    // check IDs listed in Vitals\addresses\*.txt
		bool breadcrumbs{ true };     // crash-safe ring of "which port hook ran last"
		bool papyrusNatives{ false }; // opt-in: walk the Papyrus VM's type table
		bool layoutProbes{ false };   // opt-in: read known fields at candidate offsets
		bool captureBaseline{ false };// opt-in: save the bytes seen in memory as this build's baseline (My Games\\...\\F4SE\\Vitals\\baselines)
		bool          timeline{ true };         // log every F4SE event + status.json (for smoke tests)
		std::uint32_t exitAfterMenuSeconds{ 0 };  // TEST RIGS ONLY: 0 = off. Hard-exit N seconds after the main menu is ready
		std::uint32_t exitAfterLoadSeconds{ 0 };  // TEST RIGS ONLY: 0 = off. Hard-exit N seconds after a save/new game loads
		std::uint32_t bytesToRead{ 16 };
		bool          doctor{ true };           // plugin doctor: why DLLs were refused, load-order masters, known problems
		std::uint32_t popupMode{ 1 };           // 0 = never, 1 = when the problem list changes, 2 = every launch
		std::uint32_t keepSessions{ 5 };        // copies of earlier Vitals.jsonl kept in Vitals\history (0 = none)
		bool          shareSend{ true };          // send the share pack when sharing is on (bShareSend)
		int           sharePack{ -1 };          // -1 = follow the installer (Vitals\share-optin.txt), 0 = off, 1 = on
	};
	const Settings& GetSettings();
	void            LoadSettings();

	// ---- Paths ----
	std::filesystem::path GameDir();     // folder holding Fallout4.exe
	std::filesystem::path PluginDir();   // <game>\Data\F4SE\Plugins
	std::filesystem::path OutputDir();   // Documents\My Games\<save folder>\F4SE\Vitals
	std::string           Redact(std::string a_text);  // strip Windows user name / profile path
	std::string           Narrow(const std::wstring& a_wide);

	// ---- Output: JSON lines + summary ----
	class Json
	{
	public:
		Json& Str(std::string_view a_key, std::string_view a_val);
		Json& Num(std::string_view a_key, std::int64_t a_val);
		Json& UNum(std::string_view a_key, std::uint64_t a_val);
		Json& Bool(std::string_view a_key, bool a_val);
		Json& Raw(std::string_view a_key, std::string_view a_json);  // pre-built JSON value
		Json& Null(std::string_view a_key);
		std::string Build() const { return "{" + _body + "}"; }
		static std::string Quote(std::string_view a_text);
		static std::string Array(const std::vector<std::string>& a_items);  // items are JSON values

	private:
		void        Key(std::string_view a_key);
		std::string _body;
	};

	void OpenReport();                               // rotates Vitals.jsonl -> Vitals.prev.jsonl
	void Emit(std::string_view a_kind, Json a_json); // one line: {"t":..,"kind":..,...}
	void Summary(std::string_view a_line);           // human summary line (Vitals-summary.txt)

	// ---- Safe memory reads (VirtualQuery before memcpy; never writes) ----
	bool        IsReadable(const void* a_addr, std::size_t a_len);
	bool        SafeRead(const void* a_addr, void* a_out, std::size_t a_len);  // fails instead of faulting
	std::uint32_t ProcessId();
	bool        IsExecutable(const void* a_addr);  // inside a code page
	std::string Hex(const std::uint8_t* a_data, std::size_t a_len);
	std::string ModuleNameForAddress(const void* a_addr);  // file name of the DLL/EXE owning an address
	std::filesystem::path ModulePathForAddress(const void* a_addr);  // full path, empty if no module owns it
	std::string Lower(std::string a_text);
	std::wstring Widen(std::string_view a_utf8);
	std::optional<std::chrono::sys_seconds> FileTimeToSys(std::filesystem::file_time_type a_t);  // never throws

	// ---- Address Library, read by Vitals itself (never through CommonLib's REL::IDDB, which ends the game) ----
	struct AddressLibraryInfo
	{
		std::string   file;             // version-1-10-163-0.bin
		bool          present{ false };
		bool          usable{ false };  // CommonLib would accept this file too
		std::string   state;            // ok | missing | unreadable | too_small | empty | truncated | unsorted | blocked
		std::string   problem;          // why it is not usable, in words; empty when usable
		std::uint64_t count{ 0 };       // entries
	};
	const AddressLibraryInfo&    AddressLibrary();  // read once, on first use
	std::optional<std::uint64_t> LookupOffset(std::uint64_t a_id);  // nullopt when the ID or a usable file is missing
	// Does an entry of the shipped address list (e.g. "BSTimer::Singleton") resolve on this build? Features that go
	// through CommonLib's own lookups (which end the game on a missing ID) check this first.
	bool LibraryIdResolves(std::string_view a_listName);
	int                          RuntimeIndex();  // 0 = OG, 1 = NG, 2 = AE
	const char*                  RuntimeName();

	// ---- Parts ----
	void ReportEnvironment();  // kPostPostLoad
	void RunDoctor();          // kPostPostLoad: plugin doctor, load-order and known-problem checks, Vitals-Report.txt
	void DoctorAddressNote(std::string_view a_line);  // kGameDataReady: add the address-check result to Vitals-Report.txt
	bool SharePackEnabled();   // opt-in: bSharePack, or the installer's share-optin.txt
	void WriteSharePack(std::string_view a_when);  // background thread; when off, removes an old pack
	std::string ShareReportText();  // the report's share-pack section (empty when off)
	void CheckAddresses();     // kGameDataReady
	void RecheckPointers();    // after a save loads: singletons that were null at the menu
	void ListPapyrusNatives(); // kGameDataReady (opt-in)
	void RunLayoutProbes(std::string_view a_when);  // game data ready + save loaded/new game (opt-in)
	void MarkPhase(std::string_view a_phase);          // status.json + timeline record
	void ScheduleExit(std::uint32_t a_seconds, std::string_view a_reason);  // opt-in, test rigs only
	void InitBreadcrumbs();    // at load
	void Crumb(const char* a_port, const char* a_where);

	// Runs a_fn; an exception is written to Vitals-summary.txt and Vitals.jsonl instead of reaching the game.
	template <class F>
	void Guarded(std::string_view a_what, F&& a_fn) noexcept
	{
		try {
			a_fn();
		} catch (const std::exception& e) {
			try {
				Summary(std::format("Vitals: {} stopped early ({}); the game is not affected", a_what, e.what()));
				Emit("error", Json().Str("step", a_what).Str("what", e.what()));
			} catch (...) {
			}
		} catch (...) {
			try {
				Summary(std::format("Vitals: {} stopped early (unknown error); the game is not affected", a_what));
			} catch (...) {
			}
		}
	}
}
