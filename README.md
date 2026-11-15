# Vitals: the F4SE plugin doctor

**Vitals tells you, in plain words, why your Fallout 4 mods are not working, before they crash your game.**

One small F4SE plugin. Install it, start the game once, and read
`Documents\My Games\Fallout4\F4SE\Vitals\Vitals-Report.txt`. It answers the questions a broken load order raises:

* **Which F4SE plugins did not load, and why?** "Made for 1.10.163 only", "needs a newer F4SE", "the Address Library
  file for your game version is missing", "needs the Runtime Database file", with the matching `f4se.log` line.
* **Is my load order going to crash?** Masters that are missing, installed but not active, or loading after the
  plugin that needs them (the classic `Actor::InitItemImpl` crash at the main menu or on load).
* **Will your loose-file mods even load?** Warns when loose meshes, textures or interface files are installed but `Fallout4Custom.ini` does not turn on loose files.
* **Are known-bad files installed?** Old main-menu and font files that break the Anniversary menus, ENB + Community
  Shaders fighting over the same passes, and other rules found by testing real mod lists (you can add your own).
* **Is an older copy of a mod's DLL overwriting the new one?** For each plugin that is not running, the report shows the
  DLL's own version, which Vortex or MO2 mod's copy the game is using, other installed mods that carry a working copy
  (the usual cause when an up-to-date mod "does not load"), and whether your mod manager's own update check found a
  newer version on Nexus. All read from files on your PC; Vitals never goes online.
* **What changed since my last launch?** Every session is kept; one command shows plugins added or removed and
  engine functions newly hooked, and by which DLL.

A message box appears at start-up **only** when something needs attention: a problem that may crash the game,
or an F4SE plugin that is installed but not running. The box shows on every launch until you fix the problem or press **Don't show**, and **Copy report** puts the whole report on the clipboard for a comment or bug report. Everything else stays in the report.

**Optional: help find broken mods.** The installer's second page offers a share pack (off unless you pick **Yes**): a zip
of your logs with your user name, PC name, save and character names, emails, IP addresses, keys and private folder paths
scrubbed out, for you to send if you want to (drag it into a private channel in the Vitals Discord server, or attach it to a GitHub issue). Vitals never sends anything. See [docs/PRIVACY_AND_SAFETY.md](docs/PRIVACY_AND_SAFETY.md#the-share-pack-opt-in-off-by-default).

Vitals is part of **Defib**, a project to bring abandoned F4SE mods back to life on every game version. The same
plugin records what a mod porter needs: which engine functions are hooked and by whom, whether the Address Library
IDs a port relies on point at the expected code, Papyrus native functions per DLL, and crash breadcrumbs.

| | |
| --- | --- |
| Game versions | 1.10.163 (Old-Gen, tested), 1.10.980/984 (Next-Gen, a player confirmed 1.10.984), 1.11.137 to 1.11.240 (Anniversary, tested on 1.11.240). One DLL for all. |
| Requires | [F4SE](https://www.nexusmods.com/fallout4/mods/42147) for your game version, and [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327) (without it Vitals still starts and says so; only the address check is skipped) |
| Safe | Reads only. Never writes to game memory, installs no hooks, adds no plugin (.esp) and no save data, and has no network code. Its files go only in `Documents\My Games\Fallout4\F4SE\Vitals`. Remove it any time. |
| Works with | Buffout 4, Buffout 4 NG, Addictol and their crash loggers, CLASSIC, LOOT, Vortex, Mod Organizer 2 |
| AI Assisted | Written with Claude (Anthropic), tested in game by a person. See `AI_DISCLOSURE.md`. |

## Install

**Vortex or Mod Organizer 2:** install the archive like any other mod. The installer asks two questions:
*Player* (recommended) or *Mod author / developer* (adds the Papyrus natives list and the engine layout probes), and
whether to make a share pack you can send (default: no).

**By hand:** open the archive, copy the contents of `00 Core` into your game's `Data` folder, then the contents of
`10 Settings - Player` (or `11 Settings - Developer`) into `Data` too. You should end up with
`Data\F4SE\Plugins\Vitals.dll`, `Vitals.ini` and the `Vitals` folder.

**Uninstall:** remove those three. Vitals keeps nothing in your saves.

## Read the report

After one launch (the main menu is enough), open `Documents\My Games\Fallout4\F4SE\Vitals\Vitals-Report.txt`.

```
Vitals 0.4.0 report | game 1.11.240.0 (AE) | F4SE 0.7.9.0
1 problem(s), 1 warning(s), 0 note(s). Problems may crash the game; warnings are mods that will not work as installed.

  Mod manager: Vortex; looked through 1041 installed mods for other copies of the 1 not-running DLL(s) in 205 ms
  F4SE plugins: 72 DLLs in Data\F4SE\Plugins, 71 running, 1 not running

PROBLEMS
  - eXoSorter - Patch.esp needs the master SomeMod.esm, which is installed but NOT active.
    What to do: Activate SomeMod.esm, or deactivate eXoSorter - Patch.esp. ...

WARNINGS
  - OldThing.dll is in Data\F4SE\Plugins but is not running: it is an older-style plugin (Query only) without
    F4SEPlugin_Version data, which F4SE 0.7.x needs, so F4SE skips it. File version 1.0.1.0. Installed by the Vortex
    mod "Old Thing-1234-1-0-1".
    What to do: Look for a version of this mod for your game version, or a replacement. ...
```

[docs/USER_GUIDE.md](docs/USER_GUIDE.md) explains every message and setting, and
[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) lists common problems and fixes.

### How Vitals fits with other tools

| Tool | What it does | Vitals |
| --- | --- | --- |
| Buffout 4 / NG, Addictol crash logger | Write a crash log **after** a crash | Explains problems **before** the crash, including plugins that never loaded (crash loggers do not see those) |
| CLASSIC | Reads crash logs and checks your setup afterwards | Works from inside the running game, with the real loaded plugin list. Use both. |
| LOOT | Sorts plugins and warns about missing masters from its own database | Also checks F4SE DLLs and loose files, and catches inactive masters on your exact setup |
| `f4se.log` | Says "disabled, incompatible" | Says *why*, and what to install instead |

## Settings (`Data\F4SE\Plugins\Vitals.ini`)

| Setting | Default | What it does |
| --- | --- | --- |
| `bDoctor` | 1 | The plugin doctor and `Vitals-Report.txt` |
| `iPopupMode` | 1 | 0 never, 1 every launch while there is something to list (2 does the same) |
| `iKeepSessions` | 5 | Earlier sessions kept in `Vitals\history` |
| `bSharePack` | follows the installer | Share pack: -1/commented out follows the installer choice, 0 always off, 1 on |
| `bEnvironmentReport` | 1 | Game build, F4SE, every DLL and whether it loaded |
| `bAddressCheck` | 1 | Check the engine addresses in `Vitals\addresses\*.txt`; find hooked functions |
| `bBreadcrumbs` | 1 | Crash-safe "which port hook ran last" ring |
| `bTimeline` | 1 | F4SE events and `status.json` |
| `bPapyrusNatives` | 0 | Developer: every Papyrus native function and the DLL that registered it |
| `bLayoutProbes` | 0 | Developer: measure known engine fields in the running game |
| `bCaptureBaseline` | 0 | Developer, one launch only: save this build's code bytes as a local baseline |
| `iExitAfterMenuSeconds`, `iExitAfterLoadSeconds` | 0 | Test rigs only: quit without saving after N seconds |

## Tools (Python 3.9+, no installs needed)

| Tool | Use |
| --- | --- |
| `tools/defib_preflight.py "<game folder>"` | Check DLLs, load order, loose interface files and known-bad files **without starting the game** |
| `tools/defib_logs.py "<Documents>\My Games\Fallout4"` | One Markdown report from f4se.log, Vitals, breadcrumbs, the newest crash log and the Papyrus log. `--compare` (what changed since last run), `--history`, `--compare N` |
| `tools/defib_natives.py` | Papyrus native stubs and diffs between versions of a mod |
| `tools/f4se_dll_scan.py` | Predict whether an F4SE DLL loads on each game version, without the game |
| `tools/smoke_test.ps1` | Start the game, wait for the main menu, stop it, write the report |
| `tools/make_package.py` | Build and check the FOMOD release folder |

## For mod authors and maintainers

* Crash breadcrumbs from your own plugin: `#include <Vitals/Breadcrumbs.h>` then
  `VITALS_CRUMB("MyPort", "hook:PlayerCharacter::Update");` (no-op when Vitals is absent).
* Check your own addresses: drop `Data\F4SE\Plugins\Vitals\addresses\MyMod.txt` next to the shipped list.
* Add doctor rules: drop `Data\F4SE\Plugins\Vitals\knowledge\MyRules.txt`.

| Document | Contents |
| --- | --- |
| [docs/USER_GUIDE.md](docs/USER_GUIDE.md) | Every report message, setting and output file, for players |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Problems and fixes |
| [docs/FAQ.md](docs/FAQ.md) | Short answers |
| [docs/PRIVACY_AND_SAFETY.md](docs/PRIVACY_AND_SAFETY.md) | Exactly what Vitals reads, writes and sends (it sends nothing) |
| [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) | Adding rules, bug reports, code changes |
| [docs/FILE_FORMATS.md](docs/FILE_FORMATS.md) | Vitals.jsonl records, address lists, baselines, knowledge rules, breadcrumbs, status.json |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | How the plugin is built: start-up order, each source file, design rules |
| [docs/BUILDING.md](docs/BUILDING.md) | Building on Windows, in CI and from Linux |
| [docs/MAINTAINING.md](docs/MAINTAINING.md) | Patch day, updating the library, releases, test matrix |
| [docs/HISTORY.md](docs/HISTORY.md) | How Vitals was made and what Stage 0 of Defib found |
| [docs/ROADMAP.md](docs/ROADMAP.md) | What comes next |

## Licence

GPL-3.0 with the modding and linking exceptions in `EXCEPTIONS`, matching CommonLibF4 and its template.
All releases are tagged **AI Assisted**; see `AI_DISCLOSURE.md`.
