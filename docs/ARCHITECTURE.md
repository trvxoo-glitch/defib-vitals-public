# Vitals architecture

How the plugin is put together, so it can be changed safely years from now.

## Design rules (do not break these)

1. **Read only.** Vitals never writes to game memory, installs no hooks or patches (`F4SE::Init` with `.hook = false`),
   adds no .esp and no save data.
   Every read of game memory goes through `SafeRead` (checks the page is committed and readable first). This is
   what makes it safe to ship to every player and to leave installed.
2. **Never stop the game.** No ID lookup may throw or call the library's fatal paths. Vitals reads the Address
   Library file itself (`AddressLibrary()` in `Common.cpp`) and never constructs CommonLib's `REL::IDDB` on its own,
   because IDDB ends the game when the file is missing, cut short or one CommonLib blocks. Library singletons
   (`RE::...::GetSingleton()`) are used only after `LibraryIdResolves` says the file is usable and has their ID.
   Unknown IDs are reported as `unresolved`. Every F4SE message handler runs inside `Guarded`, so an exception is
   logged and that step skipped. Vitals must keep working on a build the address list has never seen.
3. **Only library-provided addresses and types.** Vitals uses what CommonLibF4 already exposes. New offsets are
   measured (layout probes) and reported, never assumed.
4. **Exact builds, not version independence.** `F4SEPlugin_Version` lists exactly the builds the pinned library
   supports, so a future patch makes F4SE skip Vitals with a log line instead of loading it into a game it does
   not understand.
5. **Plain language first.** Every finding a player can see has a message and a "what to do". Technical detail
   goes to `Vitals.jsonl`.
6. **Evidence-based rules.** Doctor rules come from findings confirmed in game, dated, with the build.
7. **No personal data.** Paths are redacted (`Redact`) before anything is written.
8. **No network code in the plugin.** `Vitals.dll` never sends anything. The opt-in share pack is a file the player chooses to send;
   everything in it passes the scrubber (`include/Vitals/Scrub.h`), and the list of files is fixed in `Share.cpp`.

## Start-up order

| F4SE stage | What runs | File |
| --- | --- | --- |
| `F4SEPlugin_Query` (OG) / `F4SEPlugin_Version` (NG/AE) | Loader decides whether to load Vitals | `res/vitals/commonlibf4-plugin.cpp.in`, `main.cpp` |
| `F4SEPlugin_Load` | `LoadSettings`, `OpenReport` (rotate history), `InitBreadcrumbs`, `start` record, register message listener | `main.cpp`, `Common.cpp`, `Breadcrumbs.cpp` |
| `kPostPostLoad` (all F4SE plugins loaded, before the game reads plugins) | `ReportEnvironment`, then `RunDoctor` (report, then `WriteSharePack("start")` on a background thread when opted in, then the message box: "Stop game" ends the process, so the pack is started before the box) | `Environment.cpp`, `Doctor.cpp`, `Share.cpp` |
| `kGameDataReady` (data loaded, main menu next) | `CheckAddresses` (adds the address section to the report; skipped with `address_check_unavailable` when the Address Library is not usable), `WriteSharePack("main_menu")` when opted in, `RunLayoutProbes`; `ListPapyrusNatives` one task tick later, after every plugin's own `kGameDataReady` handler | `Addresses.cpp`, `Natives.cpp`, `Probes.cpp` |
| `kPostLoadGame` / `kNewGame` | pointer re-check, probes, `session` record | `Addresses.cpp`, `Probes.cpp` |
| every message | `MarkPhase` (`phase` record, `status.json`), optional test-rig exit | `Status.cpp` |

`RunDoctor` also compares with the last launch (`CompareWithLastLaunch` in `Doctor.cpp`): it reads only `Vitals.prev.jsonl` (the
`start`, `environment`, `plugin`, `doctor` and `phase` lines; 300 ms limit) and the names of `crash*.log` files in the F4SE folder
(1 s limit), as its own guarded step, so a failure there leaves the rest of the report intact. `plugins_active` in the `doctor`
record is what the next launch compares.

The doctor runs at PostPostLoad on purpose: every DLL that is going to load has loaded, so "not running" is final,
and a message box there appears before a load-order crash at the main menu.

## Source files

| File | Role |
| --- | --- |
| `src/Vitals.h` | Settings struct and every function shared between files |
| `src/pch.h` | Precompiled header: CommonLibF4, REX::W32 headers, the standard library |
| `src/main.cpp` | Entry points and the F4SE message listener |
| `src/Common.cpp` | Settings, output folder, `Emit` (jsonl), `Summary`, `Json` builder, `Redact`, `SafeRead`, module lookup by address, history rotation |
| `src/Environment.cpp` | Game build, F4SE version, every DLL with its F4SE data and whether it is in the process |
| `src/Doctor.cpp` | Plugin doctor: PE parsing, plain-language reasons (`Explain`), f4se.log lines, knowledge rules (with per-runtime tags), load order and masters, report, message box (with Copy report) |
| `src/ModScan.cpp` | What Vortex or MO2 knows about the not-running DLLs: the deploying mod, other installed copies, the mod manager's update check (one bounded pass) |
| `src/Share.cpp` | Opt-in share pack: picks the files, builds the scrub context (known folders, user and PC name, Vortex staging), summaries, `Vitals-Share.zip`, the report's share section |
| `include/Vitals/Scrub.h` | The share-pack scrubber, plain C++20. Tested by `tests/cpp/rules_test.cpp` with `tests/scrub_cases.txt`; the share packs are scrubbed once, by this code, before the zip is written |
| `include/Vitals/Share.h` | Share-pack summaries that need no game: Papyrus log counts, Vortex manifest to DLL sources, breadcrumbs as text, the INI allowlist, size caps |
| `include/Vitals/Zip.h` | Stored-only zip writer (CRC-32) |
| `tools/share_inbox.py` | Downloads packs from the Discord server (maintainers) and counts not-running DLLs, crash locations and unbound natives across installs |
| `include/Vitals/Launch.h` | Report text that compares launches, plain C++20, tested with the rest: reading one launch's `Vitals.jsonl` (cut-off lines skipped), the "Last launch" recap, "CHANGED SINCE LAST LAUNCH" (DLLs by SHA-256), rules-file order by `# Generated:` date and the rules stamp, `patchedby` owner lookup, `dllbuild` matching |
| `include/Vitals/Rules.h` | The parts that need no game, plain C++20: F4SE loader rules (`Loader::Check`), plugin-name decoding, INI values, return-stub detection, popup decision and key files, the real load order, UTF-8-safe cuts, user-folder redaction. Tested by `tests/cpp/rules_test.cpp` |
| `include/Vitals/Formats.h` | Readers for files on disk, plain C++20: PE exports and `F4SEPlugin_Version` data, the Address Library table, TES4 plugin headers. Every bound is checked by subtraction, so a crafted size or offset cannot wrap. Fuzzed and tested by `tests/cpp/rules_test.cpp` (CI also runs it under AddressSanitizer) |
| `src/Addresses.cpp` | Address check: resolve IDs, read bytes, compare with list/baseline, jump decoding and hook owners (`ResolveOwner`), singleton classes, baseline capture |
| `src/Probes.cpp` | Layout probes (BSTimer two-sample millisecond counter) |
| `src/Natives.cpp` | Papyrus native functions and the DLL that registered each |
| `src/Breadcrumbs.cpp` | Memory-mapped crash-safe ring and the `Vitals_Breadcrumb` export |
| `src/Status.cpp` | Phases, `status.json`, test-rig auto-exit |
| `src/Sha256.h` | Small SHA-256 for DLL hashes |
| `include/Vitals/Breadcrumbs.h` | Header-only API for other plugins |

## The plugin doctor in detail

1. **DLL check.** For each `*.dll` in `Data\F4SE\Plugins`: in the process (`GetModuleHandleW`)? If not, parse the
   PE file (exports, `F4SEPlugin_Version` data through the section table) and apply the loader's rules:
   0.6.x needs `F4SEPlugin_Query`; 0.7.x reads `F4SEPlugin_Version` (address-independence bits Signatures 1,
   AL 1.10.980 2, AL 1.11.137 4; structure bits NoStructs 1, 1.10.980 2, 1.11.137 4; compatible versions packed
   `major<<24 | minor<<16 | build<<4`). The decision is `Rules::Loader::Check` in `include/Vitals/Rules.h`, in
   F4SE 0.7.x's own order: bad data, no name, independence (cancelled by `reservedBreaking`), the Address Library
   file for the current flag, the exact build in `compatibleVersions`, then the minimum F4SE version.
   `tools/f4se_dll_scan.py` has the same rules; `tests/loader_cases.txt` is run through both in CI, so they cannot drift.
   A DLL the rules accept that is still not running is explained from f4se.log ("reported as incompatible during
   load": its own Load declined; "fatal error": it crashed while loading).
2. **f4se.log.** Lines naming the DLL with words like disabled, incompatible, failed, not loaded are quoted.
3. **Knowledge rules** (`needs`, `dll`, `conflict`, `oldfile`, `file`) from `Vitals\knowledge\*.txt`.
   Format in [FILE_FORMATS.md](FILE_FORMATS.md#knowledge-rules-dataf4sepluginsvitalsknowledgetxt).
4. **Address Library** check built in (the file name is built at run time by plugins, so it cannot be found by
   scanning their text). Missing is a problem; present but not usable (cut short, empty, out of order, or a file
   CommonLib blocks) is a problem too, because F4SE still loads plugins that then show an error and close the game.
5. **Build date.** For plugins with no `F4SEPlugin_Version` data, the COFF build timestamp (file date if the
   timestamp is zero or implausible, as in reproducible builds) is compared with the Next-Gen update (2024-04-25):
   before = made for Old-Gen, after = may still be maintained (and the advice points at the mod page). Issues get a `group`
   so the message box can list them together. A DLL that is not running is a `warning` (the mod does not work);
   `problem` is kept for what may crash the game.
   The build date is checked against the linker version in the PE header (`Formats::LinkerFloor`: no DLL predates the
   Visual Studio release that made it). A date earlier than that is ignored; a linker released after April 2024 alone
   puts the DLL in the "after" group.
   Then the mod manager's own data (`src/ModScan.cpp`, parsing in `include/Vitals/ModManagers.h`), once for all the
   not-running DLLs together, with one 2-second deadline (`ModScan::Result::deadline`) shared by the folder walk, the
   byte compares, the Vortex state backup and the doctor's reading of other copies (16 MB each), because it runs
   before the start-up box. Running DLLs are never read from disk: a `needs` rule whose file is missing searches the
   loaded image. Under MO2, when the winning mod's copy is not the loaded file, it is kept apart
   (`DllCopies::shadowed`) and the advice points at Overwrite. Under MO2
   (`usvfs_x64.dll` loaded) the portable `ModOrganizer.ini` next to it, or else the instance in
   `%LOCALAPPDATA%\ModOrganizer` whose `gamePath` is this game folder (the newest if several), gives the mods folder
   and the selected profile; its `modlist.txt` (first line wins) says which mod's copy the game sees. Otherwise
   Vortex's `vortex.deployment.json` gives the staging folder and the deploying mod. One walk lists each mod's
   `F4SE\Plugins` folder; copies that are byte-identical to the deployed file are skipped, the others are read and
   judged by `Loader::Check`. A copy that would run (or, on Old-Gen, may run) is added to the advice ("make that mod
   win the file conflict"); a knowledge rule's advice stays first.
   The mod manager's update check is reported when its `newestVersion` is newer than `version`: MO2's `meta.ini`, or
   for Vortex `persistent.mods.fallout4.<id>.attributes` in the newest `%APPDATA%\Vortex\temp\state_backups_full\*.json`
   (its full state backup; the live state is a LevelDB database), matched by `installationPath`. All of this only reads files, and only
   for DLLs that are not running. A Vortex state backup over 64 MB is skipped. Each doctor check runs on its own, so
   one that stops early leaves a line in the report and the others, the report and the box still run.
6. **Load order.** Base masters, then `Fallout4.ccc` entries present in Data, then active (`*`) lines of
   `%LOCALAPPDATA%\Fallout4\plugins.txt`. Under MO2 the virtual file system makes the profile's file appear
   there. Each plugin's TES4 header gives the master flag (0x1), the light flag (0x200) and the `MAST` list. Masters
   are checked against the order the game really uses (`Rules::ActualLoadOrder`): master-flagged plugins load
   before every normal plugin whatever plugins.txt says. A master that is missing, inactive or loads later is a
   `problem`: the game does not move it forward, and all three crash the same way.
7. **Loose-file setup.** If a real loose asset file exists (Meshes, Textures, Materials, Sound, Music, Interface, Vis,
   LODSettings; empty folders do not count), `Fallout4Custom.ini`, then `Fallout4Prefs.ini`, then `Fallout4.ini` must
   give `[Archive] bInvalidateOlderFiles=1` and an empty `sResourceDataDirsFinal=`. They are read with
   `GetPrivateProfileStringW`, as the game reads them (UTF-16 or ANSI; under MO2 through its virtual file system).
8. Issues are emitted, the report is written, and the message box is shown when `Rules::DecidePopup` finds an item
   to list (problems, plus plugins that are not running and not muted; see FILE_FORMATS.md). The box
   is a standard Abort/Retry/Ignore message box whose buttons a short-lived CBT hook relabels (Stop game / Don't show /
   Continue; no Cancel, so Esc cannot stop the game). Don't show writes the listed plugins to `popup_disabled.txt`
   (a versioned key file, read back verbatim, so DLL names with spaces work).
   Plugin names from plugins.txt, Fallout4.ccc and MAST records are decoded as UTF-8 when valid, otherwise Windows-1252.

## Hook owners

`JumpTarget` decodes `E9 rel32`, `EB rel8`, `FF 25 [rip+disp32]`, `48 B8 imm64; FF E0` and `49 BB imm64; 41 FF E3`.
`ResolveOwner` follows up to 4 jumps through memory that belongs to no module (trampolines) until it lands in one,
then classifies it (game, system, plugin, other, unattributed). Only plugin, other and unattributed count as mods.

## Build pieces

* `xmake.lua`: target, version (`plugin_version`, the one place the version lives), compile flags.
* `res/vitals/commonlibf4-plugin.cpp.in`: Vitals' own copy of the library's plugin template, listing every
  supported build (the library's default listed only 1.11.240).
* `lib/COMMONLIBF4_COMMIT`: the pinned Dear-Modding-FO4/commonlibf4 commit, fetched by `tools/fetch_commonlib.*`.
* `fomod/`: installer; `tools/make_package.py` builds the release folder.

## Python tools

All standard library, Python 3.9+, tested by `tests/test_tools.py` in CI. `f4se_dll_scan.py` is imported by
`defib_preflight.py`; keep its `scan_file` result shape (`verdicts` keyed `"OG ..."`, `"NG ..."`, `"AE ..."`).
