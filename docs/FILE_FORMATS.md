# Vitals file formats

Everything Vitals reads or writes. Keep this file in step with the code: tools and other people's scripts rely on it.
Text files are UTF-8. Paths in output have the Windows user name replaced with `<user>`.

## Output folder

`Documents\My Games\Fallout4\F4SE\Vitals\` (the Windows Documents known folder, so OneDrive-redirected Documents
work; the `Fallout4` part follows F4SE's save folder name, for example `Fallout4 VR`). See [USER_GUIDE.md](USER_GUIDE.md#output-files) for the list of files.

Rotation at start-up (`Common.cpp`, `OpenReport`): the old `Vitals.jsonl` is copied to
`history\Vitals-<YYYYMMDD-HHMMSS>.jsonl` (time = when that session last wrote, in UTC), the oldest history files beyond
`iKeepSessions` are deleted, then `Vitals.jsonl` is renamed to `Vitals.prev.jsonl`. With `iKeepSessions=0` no copy is made
and nothing in `history\` is deleted: files already there stay.

## Vitals.jsonl

One JSON object per line, flushed after every line, so a crash leaves at most one cut-off last line (readers must
skip a line that does not parse). Every record has `t` (when it was written, UTC, like `2026-11-17T01:02:03.456Z`) and `kind`. Fields can be added in any version; readers must
ignore unknown fields and must not rely on field order.

| kind | When | Main fields |
| --- | --- | --- |
| `start` | Vitals loaded | `vitals_version`, `doctor`, `papyrus_natives`, `layout_probes` |
| `environment` | PostPostLoad | `game_version`, `runtime` (`OG`/`NG`/`AE`), `f4se_version`, `f4se_release_index`, `vitals_version`, `address_library_file`, `address_library_present`, `address_library_usable`, `address_library_state` (`ok`, `missing`, `unreadable`, `too_small`, `empty`, `truncated`, `unsorted`, `blocked`), `address_library_entries`, `game_dir` (UTF-8) |
| `plugin` | one per DLL in `Data\F4SE\Plugins` | `file`, `size`, `sha256` (cached in `hashes.txt` by size and file time), `file_version`, `in_process`, `export_query`, `export_load`, `export_preload`, `plugin_name`, `plugin_author`, `plugin_version`, `address_independence`, `structure_independence`, `compatible_versions` (list of version strings), `f4se_minimum`, `f4se_registered`. A DLL that is not running has only `file`, `size`, `sha256`, `file_version` and `in_process`; the rest need a running DLL (one without `F4SEPlugin_Version` has `plugin_name` as `null`) |
| `doctor_issue` | one per doctor finding | `severity` (`problem` may crash the game / `warning` will not work / `info`), `category` (`plugin_dll`, `data_file`, `load_order`, `conflict`, `loose_file`, `known_issue`, `ini_setup`), `subject`, `message`, `advice`; for `plugin_dll`: `reason` (the loader reason code, below) and `built` (`pre`/`post`/`unknown`, for plugins without version data) |
| `doctor` | after the doctor | `problems`, `warnings`, `issues`, `rules` (rules loaded), `plugins_active` (0.4.2: active plugins found in plugins.txt; absent when plugins.txt was not read), `ms` (time taken) |
| `doctor_popup` | the message box was shown | `crash_risks`, `not_running` (0.3.0: `problems`) |
| `doctor_popup_choice` | the user pressed a button | `choice`: `continue`, `dont_show_again`, `stop_game` |
| `doctor_popup_copy` | the user pressed Copy report (0.4.0) | `copied`, `chars` |
| `share_pack` | a share pack was written (opt-in, 0.4.0) | `when` (`start`, `main_menu`), `files`, `bytes`, `scrubbed` (items replaced), `left_out` (files dropped by the size cap) |
| `address` | one per checked ID | `name`, `id`, `source` (list file), `entry_kind` (`fn`/`ptr`/`data`), `checked_as`, `result`, `offset`, `bytes`, `expected`, `ptr_class` (also `listed_as_code_but_is_data`), `jump_target`, `hooked_by`, `owner_category`, `jump_hops`, `stub`, `expected_bytes_not_checked`, `normally_patched_by` (0.4.2: a running DLL that the `patchedby` rules name as the usual patcher of a function that is `patched_in_place` or `differs`), `source`, `runtime`, `id_text` (`bad_id_text`), `pointer`, `pointer_readable`, `first_qword` |
| `address_check_unavailable` | instead of the address check, when the Address Library is not usable | `address_library_file`, `state`, `problem` |
| `error` | a step stopped on an exception (the game keeps running) | `step`, `what` |
| `hooks_by_module` | after the address check | `modules`: a list of `{"module": DLL name, "functions": number of detoured functions}` (mods only; `?` is a trampoline outside any DLL) |
| `pointer_recheck` | after a load, for pointers that were null | `name`, `readable_now`, `pointer_now` |
| `papyrus_native` | developer option | `module`, `script`, `function`, `static`, `returns`, `params`, `param_types` |
| `layout_probe` | developer option | `probe`, `method`, `when`, `runtime`, `library_offset`, `advancing_ms_slots`, `verdict`, `agrees_with_library` (`null` when the timer was paused: nothing observed), `slots` |
| `phase` | every F4SE message | `event` (`PostLoad`, `PostPostLoad`, `InputLoaded`, `GameLoaded`, `GameDataReady`, `NewGame`, `PreLoadGame`, `PostLoadGame`, `PreSaveGame`, `PostSaveGame`, `DeleteGame`; `GameDataReady` is the game's data being ready, not proof that the menu is drawn), `ms_since_vitals_load` (a string, `"97400"`) |
| `session` | save loaded / new game | `event` |
| `auto_exit` | test-rig exit | `reason` |

### Address `result` values

| result | Meaning |
| --- | --- |
| `match` | Bytes equal the expected bytes (over the bytes read: `expected_bytes_not_checked` counts any left out because `iBytesToRead` is shorter) |
| `hooked` | Starts with a jump into a mod (see `hooked_by`, `owner_category`) |
| `patched_in_place` | Starts with a return stub (`stub`: `ret`, `xor eax,eax; ret`...): a fix mod switched the function off. The bytes do not name the mod |
| `differs` | Differs from the expected bytes in another way: patched in place, redirected inside the game (then `jump_target`, `owner_category` `game`/`system`), or a wrong ID. 0.2.x wrote `MISMATCH` |
| `resolved_no_baseline` | The ID resolved but there are no expected bytes for this build |
| `checked_data` | Data entry: read, never compared |
| `unresolved` | The Address Library has no offset for this ID on this build |
| `no_id_for_runtime` | The list has `-` as the ID for this runtime, so nothing was looked up |
| `bad_id_text` | The list's ID for this runtime is not a number (`id_text` holds it): fix the list line |
| `unreadable` | The ID resolved, but the memory at that address could not be read |
| `bad_expected_value` | The list's expected value is not hex (odd number of digits, or a pair that is not hex or `??`): fix the list line. CI checks the shipped lists |

`owner_category`: `game` (Fallout4.exe's own jump stubs), `system` (Windows, MSVC runtime), `plugin` (a DLL in
`Data\F4SE\Plugins`), `other` (another DLL, such as an ENB), `unattributed` (a trampoline outside any DLL, followed
up to 4 jumps without finding one). `ptr_class` for `Singleton` IDs: `pointer`, `accessor_function`,
`object_in_place`, `null_at_check`.

## Address lists: `Data\F4SE\Plugins\Vitals\addresses\*.txt`

```
# comment
name | kind | og_id | ng_id | ae_id | og_bytes | ng_bytes | ae_bytes
BSTimer::Singleton | ptr | 1256126 | 2703179 | 2703179 | - | - | -
```
`kind`: `fn`, `ptr` or `data`. `-` = unknown. Bytes are hex pairs separated by spaces, `??` = any byte.
`commonlibf4.txt` is generated from the pinned library (`tools/gen_addresses_from_commonlib.py`); a port adds its
own file. Vitals reads every `.txt` in the folder.

## Baselines: `Documents\...\F4SE\Vitals\baselines\<OG|NG|AE>-<list>.txt`

Same format as an address list; supplies expected bytes where the list has `-`. Written by `bCaptureBaseline=1`.
They are copies of game code: keep them local, never commit or upload them.

## Knowledge rules: `Data\F4SE\Plugins\Vitals\knowledge\*.txt`

```
kind | match | extra | severity | message
```
Since 0.4.0 a kind can name the game versions it applies to: `oldfile@NG,AE | ...` (`OG` = 1.10.163, `NG` =
1.10.980/984, `AE` = 1.11.x, any order and case). Without a list the rule applies to every version. Older Vitals see an
unknown kind and skip the rule. `Rules::RuleRuntimes` decides; `tools/defib_preflight.py` does the same.

| kind | match | extra | Issue when |
| --- | --- | --- | --- |
| `dll` | DLL file name in `Data\F4SE\Plugins` | `refused` or `-` | the DLL is present (and, with `refused`, not running) |
| `needs` | text found inside a DLL (case-insensitive) | file it needs, relative to the game folder | a DLL contains the text and the file is missing |
| `conflict` | file relative to the game folder | another file | both exist |
| `oldfile` | loose file relative to the game folder | date `YYYY-MM-DD` | the file is older than the date |
| `file` | file relative to the game folder | `-` | the file exists |
| `dllbuild` (0.4.2) | DLL file name in `Data\F4SE\Plugins` | the plugin version the DLL reports (`0.0.4.13`; a shorter `0.0.4` matches every sub-build) | the DLL is running and its own plugin version matches. Only that version counts, never a file date |
| `patchedby` (0.4.2) | part of a game function's name, any case | the DLL that normally patches it | never an issue: the address check says "normally patched by X, which is running" for a changed function, only when X is running. `severity` is not used (write `info`) |

Special names usable as files: `{ENB}` (an ENB is installed: `enbseries.ini` or `enblocal.ini` plus `d3d11.dll`
or `dxgi.dll` in the game folder) and `{ADDRESS_LIBRARY}` (the Address Library file for the running build).
`severity`: `problem` (may crash the game; always shown in the message box), `warning`, `info`. `#` or `;` starts a comment. The message may
contain `|`. `defib-known.txt` and `defib-census.txt` (generated by the census repository) are replaced by updates; put your own
rules in another file. The same rule (kind, match, extra) is used once, from the file read first. Files without a
`# Generated: YYYY-MM-DD` line near the top are read first, in name order (so your own file is never outranked by a
generated one); then the dated files, newest first (so a newer rules file downloaded on its own wins over the one that came with
Vitals). Before 0.4.2 every file was read in name order. The report header lists the files used and how many rules came from
each. `severity` `warn` is read as `warning`; any other word is used as `warning` and noted in `Vitals-summary.txt`. An `oldfile`
date before 2015-11-10 on the file itself (some archives store 1980-01-01) is ignored. A
`dll | X.dll | refused` rule's message becomes the "What to do" of X.dll's not-running finding (its severity is used
when higher). Every shipped rule
must come from a finding confirmed in game, with the date and build in a comment above it.
`tools/defib_preflight.py` reads the same files.

## breadcrumbs.bin

A 32,832-byte memory-mapped ring (written by the OS even if the game crashes).

| Offset | Header (64 bytes) |
| --- | --- |
| 0 | `char magic[4]` = `VTLB` |
| 4 | `u32 version` = 1 |
| 8 | `u32 capacity` = 256 |
| 12 | `u32 entrySize` = 128 |
| 16 | `u64 nextSeq` |

Entry *n* is at `64 + (seq % capacity) * 128`: `u64 seq`, `i64 qpc` (QueryPerformanceCounter), `u32 thread_id`,
`u32 pad`, `char port[40]`, `char where[64]`. The entry with the highest `seq` ran last. Ports write entries
through the `Vitals_Breadcrumb` export (see `include/Vitals/Breadcrumbs.h`).

## status.json

Rewritten on every F4SE event (written to `status.json.tmp` and renamed over it, so a reader never sees half a file), for scripts such as `tools/smoke_test.ps1`: `vitals_version`, `pid` (the game's process id, so the smoke test stops only that game), `game_version`,
`last_phase`, `main_menu_ready`, `save_or_new_game_loaded`, `phases` (list of `{phase, ms}`, `ms` a string such as `"7260"`).

## popup_disabled.txt

Starts with the line `# vitals-popup-keys 2`, then holds one `category:subject` key per line (the file name is lower-cased, like
`plugin_dll:foo.dll`; a key typed by hand in another case does not match), read back exactly
as written (DLL names may contain spaces or `;`). Other lines starting with `#` are comments. Files without the first
line are from 0.3.0 or earlier 0.3.1 test builds and are still read (keys split on `;`).

The box itself remembers nothing: it opens on every launch while it has something to list. A `popup_seen.txt` left
by 0.3.1 is no longer used and can be deleted.

`popup_disabled.txt`: written by **Don't show** with the `plugin_dll` items the box listed. Those never open the box
again; problems that may crash the game (`problem` severity) always do. A 0.3.0 file (a sentence, no keys, no first
line) mutes every plugin that is not running; a new file with no keys mutes nothing.

## hashes.txt

`size|file time|sha256|dll name (lower case)`, one line per DLL in `Data\F4SE\Plugins`, so the environment report
hashes only DLLs that changed. Safe to delete. `doctor_popup_choice` records the button pressed (`continue`, `dont_show_again`, `stop_game`).

### Loader reason codes (`reason` in `doctor_issue`, `why` in `f4se_dll_scan.py --json`)

`not_plugin`, `og_no_query`, `og_query_decides`, `no_version_data_query_only`, `no_version_data`, `bad_version_data`,
`no_name`, `address_library_missing`, `ng_flags_only`, `other_builds`, `not_declared`, `needs_newer_f4se`, and for a
DLL the rules accept: `independent`, `listed`. Defined in `include/Vitals/Rules.h`; cases in `tests/loader_cases.txt`.

## Share pack: Vitals-Share.zip (opt-in, 0.4.0)

On when `Data\F4SE\Plugins\Vitals\share-optin.txt` has a `share=1` line (the installer's **Yes** option; **No
thanks** installs the same file with `share=0`, and a file with neither, such as an early 0.4.0 test build's, is off)
unless `Vitals.ini` sets `bSharePack=0`; `bSharePack=1` turns it on without the file. Off: the next launch deletes
`Vitals-Share.zip`, `Vitals-Share-new.zip` and `share-id.txt`. Written on a background thread at PostPostLoad (after
the report, before the start-up box shows) and again at the main menu (`Vitals-Share.zip.tmp`, then renamed over the old one;
when the old one is open in another program, renamed to `Vitals-Share-new.zip` and a line in `Vitals-summary.txt`
says so).

A zip with **stored** (uncompressed) entries and UTF-8 names (`include/Vitals/Zip.h`). Entries, in this order:
`manifest.json`, `Vitals-Report.txt`, `Vitals-summary.txt`, `status.json`, `f4se.log`, `f4se_loader.log`,
`crash/crash-*.log`, `plugins.txt`, `Fallout4.ccc`, `ini-settings.txt`, `system.txt`, `dll-sources.txt` (Vortex),
`papyrus-summary.txt`, `breadcrumbs.txt`, `popup_disabled.txt`, `Vitals.jsonl`, `Vitals.prev.jsonl`,
`scrub-report.txt`. A missing source file is left out. Every entry, `manifest.json` too, passed through
`include/Vitals/Scrub.h` (crash logs through `Scrub::CrashLog`). A crash log keeps its file name only when it is a
date (`crash-2026-11-15-10-42-18.log`); otherwise it is `crash/crash-1.log` and so on. `papyrus-summary.txt` has one
section per Papyrus log (`== Papyrus.0.log (this launch) ==`, `== Papyrus.1.log (an earlier launch) ==`, by file
date against Vitals' start). A file cut to its newest part starts with `[Vitals: N older bytes left out; ...]`, N
counted in the original file. Each source keeps its
newest 2 MB (a first line `[Vitals: N older bytes left out ...]` says so); files that would take the pack past 8 MB are
left out and named in the manifest. These are the only names in a pack.

`manifest.json`: `format` (`vitals-share 1`), `vitals_version`, `game_version`, `runtime`, `f4se_version`,
`install_id` (32 hex digits, random, from `share-id.txt`), `date` (`YYYY-MM-DD`), `written_at` (`start` or
`main_menu`), `files` (list of `{name, bytes, crc32}`), `left_out_for_size` (names).

`share-id.txt`: the random install id, one line. Delete it for a new id.

## Vitals.ini

Values are read once at load, with `GetPrivateProfileInt`: a missing key uses the default, a value that is not a number counts as 0.

| Key | Default | Range | Does |
| --- | --- | --- | --- |
| `bDoctor` | 1 | 0/1 | The plugin doctor and Vitals-Report.txt |
| `iPopupMode` | 1 | 0-2 | The start-up box: 0 never, 1 or 2 every launch while there is something to list |
| `iKeepSessions` | 5 | 0-50 | Earlier sessions kept in `history\` (0: none copied, none deleted) |
| `bSharePack` | not set | -1, 0, 1 | Not set (commented out): follow `share-optin.txt`. 0 always off, 1 on |
| `bEnvironmentReport` | 1 | 0/1 | The `environment` and `plugin` records |
| `bAddressCheck` | 1 | 0/1 | The address check at the main menu |
| `bCaptureBaseline` | 0 | 0/1 | Save the bytes seen as this build's local baseline (one launch) |
| `bBreadcrumbs` | 1 | 0/1 | The breadcrumb ring |
| `bPapyrusNatives` | 0 | 0/1 | Developer: list Papyrus natives |
| `bLayoutProbes` | 0 | 0/1 | Developer: layout probes |
| `iBytesToRead` | 16 | 4-64 | Bytes read at each checked address |
| `bTimeline` | 1 | 0/1 | `phase` records and `status.json` |
| `iExitAfterMenuSeconds`, `iExitAfterLoadSeconds` | 0 | 0-3600 | Test rigs only: end the game without saving after N seconds |

Any value other than 0 counts as on for the `b` keys. The last-launch recap and the "changed since last launch" section
of the report read `Vitals.prev.jsonl`, so they need `bEnvironmentReport=1` (for DLLs and versions) and `bTimeline=1` (for the recap).
