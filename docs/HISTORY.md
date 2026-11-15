# How Vitals was made

Vitals was written for **Defib**, a project to revive abandoned F4SE mods on every Fallout 4 build
(1.10.163 Old-Gen, 1.10.984 Next-Gen, 1.11.x Anniversary). It is AI Assisted: the code and documents were written
by Claude (Anthropic) in cloud sessions, and every version was tested in game by the project owner on their PC.

## Why it exists

Porting a dead mod means answering the same questions every time: what was running, did the plugin load, are the
engine addresses the port uses really the functions it thinks they are, who else hooks them, and where did it crash.
Vitals answers them in one launch and writes them down in a form tools can compare. Stage 0 of Defib ("the
workbench") was about building it and proving it right on two game builds.

## Timeline

| Version | Date | What happened |
| --- | --- | --- |
| 0.1.0 | 2026-11-10 | Environment report, non-fatal address check (1,772 library IDs), breadcrumbs, Papyrus natives, log analyzer. Written before anything ran in game |
| CI | 2026-11-11 | First Windows build. CI caught a real bug: the version block was not compiled in, so F4SE 0.7.x would have ignored the DLL. CI now fails on that |
| 0.2.0 | 2026-11-11 | Hook detection, exe-derived baselines, pointer re-check, smoke test. First in-game run on 1.11.240: 1,772/1,772 IDs resolve, 991 Papyrus natives |
| 0.2.1-0.2.3 | 2026-11-11/12 | BSTimer probe fixed, local baselines, singleton classes, hooks per plugin, `--compare`. Confirmed on 1.11.240 and 1.10.163 (through MO2) |
| census | 2026-11-12 | First census launch of the A StoryWealth list on 1.11.240: refused DLLs, old interface files, inactive masters, the Runtime Database dependency. These became the doctor |
| census | 2026-11-13 | First runs of 0.3.0 on both builds: 1.11.240 A StoryWealth 68 minutes with several saves and no crash, doctor listed 20 DLLs F4SE silently skips ("no version data"), 16 built before April 2024 and 4 after; 1.10.163 near-vanilla clean with 0 problems. Start-up box tested; button labels shortened to fit |
| 0.3.0 | 2026-11-13 | Vitals becomes a public plugin doctor: plain-language report, load-order and master checks, knowledge rules, session history, hook owners, FOMOD installer, offline preflight |
| 0.3.1 | 2026-11-13 | Three outside code reviews of 0.3.0. Confirmed findings fixed: skipped plugins were called crash risks, the box could hide a returning problem, the loader rules missed `compatibleVersions` and the minimum F4SE version. The loader rules moved into one tested header shared with the scanner through `tests/loader_cases.txt`. Then three reviews of build 40: Vitals reads the Address Library itself (CommonLib's lookup could end the game when the file was missing or broken), DLL/AL/plugin readers moved to a fuzzed header after a 4 GB offset could wrap a bounds check, masters checked against the real load order, popup keys with spaces, user-name redaction limited to paths |
| 0.3.2 | 2026-11-15 | The start-up box shows on every launch until the player presses Don't show; Continue no longer hides it |
| Nexus | 2026-11-15 | Released on Nexus Mods. First player report: works on Next-Gen 1.10.984 (36 of 43 plugins running, 772-plugin load order) |
| 0.4.0 | 2026-11-15 | Opt-in share pack with a tested scrubber (sent by the player through a private Discord channel), Copy report button, knowledge rules limited to game versions, and evidence from the mod manager (which mod installed a DLL, other installed copies, Vortex's and MO2's own update check). Three more outside code reviews of this build fixed a possible start-up stall, the wrong MO2 instance, privacy gaps (save and character names) and a share pack that was never written when the player pressed Stop game |

## What Stage 0 found (lessons worth keeping)

* **The library's default plugin template lists only 1.11.240** and sets flags F4SE 0.7.5+ ignores. Plugins built
  from it load on one build. Vitals and Defib ports use their own template listing every supported build.
* **`IDDB::offset()` terminates the game on a missing ID.** Vitals reads the ID table itself, so an unknown ID is a
  report line, not a crash.
* **The Steam 1.10.163 exe is encrypted on disk (SteamStub).** Expected bytes cannot be read from the file, so
  Old-Gen baselines are captured from memory once (`bCaptureBaseline`) and kept local.
* **The game timer pauses during loading screens.** A one-shot timer probe failed after every load; sampling twice,
  2 s apart, fixed it. `BSTimer.lastTime` is at 0x20 on 1.11.240 and 0x18 on 1.10.163, as the library says.
* **"Singletons" are three different things**: pointers, `GetSingleton()` functions, and objects stored in place.
  Reading all of them as pointers made 11 look broken.
* **Not every jump at a function start is a mod.** The game has its own jump stubs; trampolines (Addictol) live
  outside any DLL. 0.3.0 follows them to the owner.
* **Crash loggers cannot see what never loaded.** The census's worst problems (refused DLLs, inactive masters, old
  menu files) left no crash log pointing at them. That gap is why the doctor exists.
* **CommonLibF4RD plugins need `f4rd-runtime.bin`** (Runtime Database). Confirmed with LightGoboFixRD.dll.

## How the work was done

* Code on GitHub, built on GitHub Actions (`windows-2022`, MSVC, XMake 3.1.2), with a Linux
  cross-compile (clang-cl + xwin) for quick checks from the cloud.
* The library is pinned to one Dear-Modding-FO4/commonlibf4 commit.
* Every claim was checked in game before being written down as fact; open questions stayed marked as unverified.
