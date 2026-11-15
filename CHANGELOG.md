# Vitals changelog

All releases are AI Assisted (see AI_DISCLOSURE.md) and tested in game by a person on the builds listed.

## 0.4.0 (share pack, Copy report)
Not yet tested in game.
- **Optional share pack, off unless you choose it.** The installer has a second page, *Help improve Vitals*, with
  **No thanks** selected. If you pick **Yes, help improve Vitals**, Vitals writes `Vitals-Share.zip` next to the
  report after each session: its own reports and logs, F4SE's logs, your load order, 22 named game INI settings, a short
  PC summary (Windows, CPU, cores, RAM, GPU), up to 3 recent crash logs, a count-only summary of the Papyrus log, and
  (with Vortex) which mod installed each F4SE DLL. **Every file is scrubbed first:** your user name, PC name, emails, IP
  and MAC addresses, Steam ids, Windows account ids, keys and tokens, Mod Organizer profile names and private folder
  paths become placeholders, and text read from game memory in crash logs is removed. `scrub-report.txt` inside says
  how many of each. **Vitals still has no network code:** you decide whether to send the zip, by uploading it on the share page (https://vitals-share.vitalsguy.workers.dev) or attaching it to a GitHub issue. Turn it off by deleting
  `Vitals\share-optin.txt` or setting `bSharePack=0`; the next launch deletes the pack. Full list and reasons:
  `docs/PRIVACY_AND_SAFETY.md`.
- **Fairer wording for plugins F4SE skips, and a wrong build date fixed.** A Nexus user reported a current mod flagged
  "built 2020, outdated". Reproducible builds (`/Brepro`) store a hash where the build date goes, and it can look like any
  date; Vitals now spots them (a type 16 debug entry) and uses the file date instead, saying it is weaker. The word
  "outdated" is gone: the report now says that if the file is already the newest on its mod page, the author has not
  released a version for your game yet and nothing is wrong with your setup.
- **Spot an older copy overwriting a mod.** For each plugin that is not running, the report now shows the DLL's own
  file version (to compare with the mod page) and, with Vortex, which mod installed that copy. A newer mod whose DLL is
  overwritten by an older copy from another mod (a bundle, a patch, a leftover) used to look exactly like an outdated mod.
  The advice says how to check the file conflict in Vortex or MO2.
- **Copy report** button in the start-up box: copies the whole report to the clipboard and leaves the box open.
- **Knowledge rules can name game versions** (`oldfile@NG,AE`). The old main-menu and font-file rules now apply only
  to Next-Gen and Anniversary, where those files break; Old-Gen players no longer see them. The offline preflight
  reads the same tags.
- **Upload server and inbox for share packs** (in the repository, not in the mod): `server/` is a small Cloudflare
  Worker with a private bucket that checks and scrubs every pack again, keeps nothing about the sender and deletes
  packs after 90 days; `tools/share_inbox.py` counts not-running DLLs, crash locations and unbound Papyrus functions
  across installs. Not deployed yet.
- Next-Gen 1.10.984: a player confirmed Vitals works (36 of 43 plugins running, 772-plugin load order).
- `Vitals.ini`: the `iPopupMode` comment describes 0.3.2's behaviour (the box shows every launch while it has
  something to list).

## 0.3.2 (start-up box)
Not yet tested in game.
- **The start-up box now shows on every launch until you press Don't show or fix the problem.** Before, pressing
  Continue marked the items as seen and the box stayed away next time. Now only **Don't show** silences plugins that
  are not running; problems that may crash the game always show. **Continue** and **Stop game** change nothing.
  `iPopupMode` 1 and 2 now behave the same. `popup_seen.txt` is no longer written or read and can be deleted.

## 0.3.1 (code review fixes)
Fixes from three outside code reviews of 0.3.0, then from three more reviews of build 40.
Tested in game 2026-11-15: 1.11.240 with A StoryWealth (708 plugins, 59 of 72 F4SE DLLs running, saves loaded and saved, no crash, no new false warnings), 1.10.163 with the Address Library (clean, saves loaded and saved), 1.10.163 with the Address Library file missing and only Vitals installed (game reached the menu, the report named the missing file and skipped the address check), and "Don't show" for a skipped DLL with a space in its name (remembered across a restart). 1.10.980/984 is built but untested. A damaged-DLL file was not tried in game; a test of it runs in CI.

Second round (reviews of build 40):
- **A missing or broken Address Library can no longer close the game through Vitals.** Vitals now reads the Address
  Library file itself and never lets CommonLib's lookup run on its own: that lookup shows an error and ends the game
  when the file is missing, cut short, or one CommonLib refuses. Before, on Old-Gen with no file, the address check
  did exactly that. The address check, Papyrus native list and timer probe now run only when the file is usable;
  otherwise the report says why (`address_check_unavailable`) and the game keeps going. The doctor reports a file
  that is there but unusable as a problem, and Vitals no longer sets the Address Library flags in its own version
  data, so F4SE still loads Vitals when the file is missing (that is when the report is needed).
- **A crafted or damaged DLL in the Plugins folder can no longer crash the game.** The DLL reader's bounds checks
  could wrap around with a header offset near 4 GB. The readers for DLLs, the Address Library and plugin headers
  moved into `include/Vitals/Formats.h`, check every bound by subtraction, and are fuzzed in CI under
  AddressSanitizer (the old check fails that test).
- **Masters are checked against the order the game really uses.** Master-flagged plugins load before every normal
  plugin whatever plugins.txt says, so an ESM-flagged .esp that needs a normal .esp is now caught. A master that
  loads after its plugin is a **problem** (it crashes like an inactive master), so it opens the box. Same in the
  offline preflight, which also checks the 4096 light-plugin limit and reads master names as UTF-8 when valid.
- **"Don't show" works for DLL names with spaces.** The popup key files have a version line and are read back
  exactly as written. Older files are still read.
- **The report no longer mangles words that contain the Windows user name** (a user called Rob turned PROBLEMS
  into P<user>LEMS). The name is replaced only where it is a folder name in a path.
- **Loose-file INI check reads the INI files the way the game does** (`GetPrivateProfileStringW`: UTF-16 files work,
  `Fallout4Prefs.ini` counts, and under Mod Organizer 2 the profile's files are seen), so the Mod Organizer special
  case is gone. It warns only when a real loose asset file exists (empty folders left by a mod manager do not
  count), and it now also looks at sound, music, Vis and LOD settings folders.
- **Nothing Vitals does at start-up can stop the game on an error:** every step runs inside a guard that logs the
  error and moves on. File dates no longer go through the time zone database.
- **Papyrus native list** (developer option) runs one task tick after GameDataReady, so natives registered in other
  plugins' GameDataReady handlers are included; it copies plain strings under the VM lock, checks each function's
  vtable before calling it, and formats after the lock is released.
- **Smaller fixes:** `VITALS_CRUMB` retries until Vitals.dll is found instead of remembering a miss forever;
  `status.json` is replaced in one step; the environment report lists `.DLL` files in any case, writes `game_dir`
  as UTF-8 and remembers DLL hashes in `hashes.txt`; knowledge rules with an unknown severity become warnings (and
  are logged) instead of vanishing; a list line with a non-hex expected value is `bad_expected_value`, not a byte
  difference (CI checks the shipped lists); message-box text is cut on character boundaries; `.hook = false` makes
  "no hooks" explicit; a module in a folder named like `PluginsOld` is not counted as an F4SE plugin.

First round (reviews of 0.3.0):
- **Plugins that are not running are warnings, not crash problems.** "Problem" now means "may crash the game"
  (missing or inactive masters, a missing Address Library, rules seen crashing). The start-up box lists the two
  apart: **May crash the game** and **Mods not running**.
- **The box can no longer hide a crash warning.** Each item is shown once and forgotten when it goes away, so a
  problem that comes back warns again; the order of the list no longer matters; **Stop game** no longer marks the
  list as seen. **Don't show** now silences only the plugins it listed; a new crash risk always opens the box.
  An old 0.3.0 `popup_disabled.txt` keeps the plugin list silent.
- **F4SE loader rules match F4SE exactly**, in its own order: `reservedBreaking` only cancels version independence
  (the list still works), the exact game build in `compatibleVersions` is checked (a plugin built like Vitals itself
  was wrongly called "built for other game versions"), the minimum F4SE version is checked ("update F4SE" instead
  of "find another version"), plus bad version data, no name and a missing Address Library file. The rules live in
  one header (`include/Vitals/Rules.h`) and `tests/loader_cases.txt` (33 cases) runs through both the plugin's rules
  and `f4se_dll_scan.py` in CI. A plugin the rules accept that still is not running is explained from f4se.log.
- **Advice fits the reason**: a plugin built after the Next-Gen update points at the mod page; "needs a newer
  F4SE" says to update F4SE.
- **Plugin names** in plugins.txt, Fallout4.ccc and master lists are read as Windows-1252 when they are not UTF-8,
  so an accented name no longer gives a false "not installed" problem.
- **'needs' rule messages** containing `|` are no longer cut off.
- **Loose-file INI check:** `bInvalidateOlderFiles=1 ; comment` counts. Plugin-limit sentence corrected (254
  slots, the base game and DLCs included).
- **Address check:** functions switched off in place with a return stub are `patched_in_place` (not "differs");
  jumps into the game itself are no longer counted as "hooked by another plugin"; entries listed as code that are
  really data (globals that move with ASLR) are no longer byte-compared, which removes false `differs` on Old-Gen;
  an expected value longer than `iBytesToRead` is compared over the bytes read instead of counting as different.
  `bCaptureBaseline` refuses to run while other F4SE plugins are loaded and never saves a return stub.
- **Developer options:** the Papyrus native list is copied under the VM lock and written after it (no disk writes
  while scripts wait); the native list and the BSTimer probe first check that their Address Library IDs resolve
  (CommonLib's own lookup would end the game); a paused-timer probe reports `agrees_with_library: null`.
- **Safer reads:** memory reads go through ReadProcessMemory on the game's own process after the page check, so a
  page freed by another thread in between fails the read instead of crashing.
- **Tools:** `smoke_test.ps1` stops only the game it started (Vitals writes the process id to `status.json`);
  `make_package.py` refuses to delete the repo, a parent folder or a folder that is not a package;
  `f4se_dll_scan.py` reports a damaged DLL and keeps scanning; preflight skips a rule with a bad date instead of
  crashing; `gen_addresses_from_commonlib.py --keep` regenerates the shipped list without losing its signatures
  (it reproduces the current list exactly).
- **Census rules shipped:** `Vitals\knowledge\defib-census.txt`, 16 rules from the Defib census, each confirmed in
  game on 1.11.240 (generated by the census repository; replaced on update). New: Workshop Framework's Old-Gen
  Replacers, Addictol + Daytripper 4, Rusty Face Fix, SUP F4SE, Lighthouse Papyrus Extender, Console Util, Custom
  Scale Fix, Disable Companion Collision, ToggleEquip, Escape Freeze. `defib-known.txt` keeps the format notes; the
  same rule in two files is used once. A `dll ... refused` rule now becomes the advice on that DLL's finding instead
  of a second line.
- Both test timers (`iExitAfterMenuSeconds`, `iExitAfterLoadSeconds`) can be set together. The doctor records how
  long it took (`ms`). Duplicate helpers moved to the shared header; stale comments and docs fixed.

## 0.3.0 (plugin doctor)
Tested in game 2026-11-13: 1.11.240 with A StoryWealth (68 minutes, several saves, no crash; doctor listed the 20
plugins F4SE silently skips; start-up box and its three buttons) and 1.10.163 near-vanilla (clean, 0 problems, address
check 1,487 match, BSTimer lastTime 0x18 OK). 1.10.980/984 is built but untested.
- **Plugin doctor.** At start-up (PostPostLoad) Vitals explains, in plain words, every F4SE DLL that is not running
  (the same rules F4SE uses, plus the f4se.log line), a missing Address Library file, masters that are missing,
  inactive or loading late, too many plugins, and the knowledge rules in `Vitals\knowledge\*.txt`. Writes
  `Vitals-Report.txt` with "What to do" for each finding. `bDoctor` (on).
- **Message box** when problems that may crash the game are found: `iPopupMode` 0 never, 1 when the list changes
  (default), 2 every launch.
- **Knowledge rules** (`defib-known.txt`), from the first census launch: Runtime Database dependency, ENB +
  Community Shaders, pre-2024 `MainMenu.swf` and `FontConfig.txt`, Papyrus Common Library and XDI on Anniversary.
- **Session history:** the last `iKeepSessions` (5) sessions in `Vitals\history`. Analyzer `--history`, `--compare N`.
- **Hook owners:** follows trampolines up to 4 jumps (also `mov rax/r11, imm64; jmp`), names the owning DLL, and no
  longer counts the game's own jump stubs or system DLLs as mod hooks (fixes the 0.2.3 known issue). New fields
  `owner_category`, `jump_hops`.
- **FOMOD installer** for Vortex and MO2 with a Player / Developer choice. CI checks every path it names.
- **`tools/defib_preflight.py`:** the doctor's checks without starting the game, plus which mod deployed each
  `Data\Interface` file (Vortex).
- Plugins F4SE skips for lack of version data get a second check, the DLL's build date against the April 2024
  Next-Gen update (file date if the build date is unusable): "outdated" vs "may still be maintained". Listed together in the box.
- The start-up box has **Continue / Don't show / Stop game** buttons (`popup_disabled.txt`).
- **Loose-file setup check:** warns when loose meshes/textures/interface files are installed but `Fallout4Custom.ini`
  lacks `bInvalidateOlderFiles=1` or an empty `sResourceDataDirsFinal=` (also in `defib_preflight.py`).
- Analyzer: a "Plugin doctor" section.
- Docs: new README, user guide, troubleshooting, FAQ, privacy and safety, contributing, file formats, architecture,
  building, maintaining, history.
- Removed `tools/f4se_log_check.py` (unused; `defib_logs.py` covers it). Stage 0 notes were later removed from the repository.

## 0.2.3 (Stage 0 final)
Tested in game: 1.11.240 (F4SE 0.7.9) and 1.10.163 (F4SE 0.6.23) before release; the tag is pushed only after both pass.
- **Local baselines.** Expected bytes for a build can come from `My Games\Fallout4\F4SE\Vitals\baselines\<RT>-<list>.txt`,
  outside the game folder, so updating Vitals never wipes them. `bCaptureBaseline=1` writes them there.
- **Singletons classified.** The 11 "unreadable singletons" were never pointers: 6 are `GetSingleton()` functions,
  3 are objects stored in place, 2 read as zero (a null pointer, or an object whose first field is zero). Each is now labelled (`ptr_class`), and accessor
  functions are byte-checked like other functions. The pointer re-check only follows the ones that were null.
- **BSTimer probe fixed.** The one-shot "close to elapsed time" test failed after any load because the game timer
  pauses during loading screens. The probe now samples twice, 2 s apart, on a background thread, and reports the
  slot that advances like a millisecond counter (INCONCLUSIVE if the timer is paused).
- **Hooks per plugin.** The summary and a `hooks_by_module` record total the detoured functions per DLL.
- Data entries are reported as `checked_data` instead of "no baseline".
- Analyzer: `--compare` (what changed since the previous run), a Stage 0 gate section, warnings when f4se.log
  comes from a launch outside the mod manager or a later launch than the Vitals report.
- `smoke_test.ps1`: `-LaunchWith`/`-LaunchArgs` for MO2 (`moshortcut://:F4SE`), `-PluginsDir`, `-Compare`.

Confirmed in game on 2026-11-12: 1.11.240 (1,450 match, BSTimer lastTime 0x20 OK) and 1.10.163 through MO2
(local baseline in use, BSTimer lastTime 0x18 OK), no crashes.

Known issue: "Hooked functions by plugin" also counts functions that start with a jump in the clean game (the game's
own stubs into Fallout4.exe, MSVCR110.dll, d3d11.dll), and "?" means a trampoline outside any DLL, which is how
Addictol hooks. Cosmetic; fixed in the next version.

## 0.2.2
- `bCaptureBaseline`: write the bytes seen in memory as a baseline (for the encrypted Steam 1.10.163 exe).

## 0.2.1
- BSTimer probe recognises a millisecond counter; confirmed `lastTime` at 0x20 on 1.11.240.

## 0.2.0
- Hook detection (jump at function start, owner module), baselines from the exe, `differs`, timeline and
  `status.json`, test-rig auto-exit, Papyrus natives with parameter types, layout probes, smoke test script.

## 0.1.0
- Environment report, address check, breadcrumbs, log analyzer.
