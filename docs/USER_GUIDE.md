# Vitals user guide

For players. Mod authors also want [FILE_FORMATS.md](FILE_FORMATS.md) and [ARCHITECTURE.md](ARCHITECTURE.md).

## What happens when you start the game

1. F4SE loads `Vitals.dll` with the other plugins. Vitals reads `Vitals.ini`.
2. When all F4SE plugins have loaded (before the game reads any .esp), the **plugin doctor** runs:
   * every DLL in `Data\F4SE\Plugins` is checked: is it running? If not, why not?
   * the load order is read and every plugin's masters are checked;
   * the rules in `Data\F4SE\Plugins\Vitals\knowledge\*.txt` are applied;
   * for plugins that are not running, your mod manager's records are read (Vortex or MO2, see below). This has a
     2-second limit; the report's "Mod manager:" line says what was looked at and how long it took.
   Every step runs on its own: if one cannot finish, the report says which and the rest still runs.
3. If it found something that needs attention, a message box lists it in two parts: **May crash the game**
   (missing or inactive masters, a missing Address Library, known crash causes) and **Mods not running** (F4SE plugins
   that are installed but did not load). Press **Continue** and the game carries on. Nothing is changed or disabled for you.
4. When the game data is ready (just before the main menu) the **address check** runs (about a second) and adds a section to the report. It runs again after a save loads or a new game starts.
5. Everything is written to `Documents\My Games\Fallout4\F4SE\Vitals\`.

## Output files

| File | What it is |
| --- | --- |
| `Vitals-Report.txt` | **Start here.** Plain-language problems, warnings, notes, and what to do |
| `Vitals-summary.txt` | Short technical summary (versions, counts, address check) |
| `Vitals.jsonl` | Every detail, one JSON object per line. For tools and for sharing when asking for help |
| `Vitals.prev.jsonl` | The previous session |
| `history\Vitals-YYYYMMDD-HHMMSS.jsonl` | Earlier sessions (`iKeepSessions`, default 5) |
| `breadcrumbs.bin`, `breadcrumbs.prev.bin` | Which port hook ran last (only useful with Defib ports) |
| `status.json` | How far the game got (for scripts) |
| `hashes.txt` | Remembered SHA-256 of each F4SE DLL, so unchanged DLLs are not read again (safe to delete) |
| `Vitals-Share.zip`, `share-id.txt` | Only if you opted in: your logs with personal details scrubbed, for you to send if you want to help. See [Share pack](#share-pack) |
| `Vitals-Share-new.zip` | Only if `Vitals-Share.zip` was open in another program when Vitals tried to replace it: this launch's pack |

Your Windows user name is replaced with `<user>` in every file, so the files are safe to share.

## Start-up box buttons

| Button | What it does |
| --- | --- |
| Continue | Starts the game. The box shows again next launch while something is listed |
| Don't show | Stops listing the plugins shown (problems that may crash the game still show) |
| Stop game | Closes the game so you can fix things first |
| Copy report | Copies the whole report to the clipboard (the box stays open), to paste into a comment or bug report |

## Share pack

Optional, off unless you picked **Yes, help improve Vitals** in the installer (or set `bSharePack=1` in
`Vitals.ini`). Each launch Vitals writes `Vitals-Share.zip` next to the report, a few seconds after the start-up
report (while the start-up box is open, so **Stop game** does not lose it) and again at the main menu: the report,
F4SE's logs, your load order, a few game settings, a short PC summary, a count-only summary of the Papyrus logs and
recent crash logs, with your user name, PC name, save and character names, emails, IP addresses, keys and private
folder paths removed. Open it to see exactly what is inside; `scrub-report.txt` says how
many items were removed. Vitals never sends it. To help, join https://discord.gg/XghtBZUFNW, press **Send my pack** and drag it into your private channel, or attach it to a
[GitHub issue](https://github.com/trvxoo-glitch/defib-vitals-public/issues). Full details and how to turn it off:
[PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md#the-share-pack-opt-in-off-by-default).

## Severities

* **Problem**: likely to crash the game or stop mods from working. Fix these first.
* **Warning**: may cause trouble, or is known to on some setups.
* **Note**: for your information (counts, helper DLLs that are not F4SE plugins).

## Messages and what to do

### "X.dll is in Data\F4SE\Plugins but is not running: ..."

Vitals reads the DLL's own F4SE data and applies the same rules F4SE uses, so the reason is exact:

| Reason | Meaning | What to do |
| --- | --- | --- |
| plugin without F4SEPlugin_Version data | F4SE 0.7.x skips plugins that only have the old entry point. Vitals adds when the DLL was built: before 25 April 2024 means it was most likely made for Old-Gen 1.10.163; after that date it may be maintained and only lack the new data. Either way, if it is already the newest file on the mod page, the author has not released a version for your game yet and nothing is wrong with your setup | Look for an NG/AE version or a port. Many have one on Nexus under a different page |
| built for Next-Gen 1.10.980/984 only | Made between April and November 2024 | Look for an update; F4SE 0.7.5+ needs Anniversary flags |
| built for other game versions only: ... | The DLL lists specific builds and yours is not one | Update the mod, or use the game version it supports |
| built for Next-Gen/Anniversary only | You are on Old-Gen 1.10.163 | Get the Old-Gen file of the mod |
| not an F4SE plugin | A helper library (another DLL loads it) or a misplaced file | Leave it if a mod needs it; otherwise remove it |
| declares support for this game version, so F4SE should have loaded it | It should work | Check the quoted `f4se.log` line; usually a missing dependency |

When `f4se.log` has a line about the DLL, it is quoted.

Below the reason, Vitals adds what it can find on your own PC, with no internet and no mod page:

- **File version**: the version number inside the DLL, to compare with the mod page.
- **Installed by**: which Vortex or MO2 mod the game's copy came from (under MO2, the mod that wins in your selected
  profile). If your mod manager's update check has already seen a newer version on Nexus, that is shown too (run
  "Check for updates" in Vortex or MO2 first for the freshest answer; Vitals does not go online itself).
- **Other installed copies**: other mods in your Vortex staging folder or MO2 mods folder that carry the same DLL. If
  one of them would run on your game, the advice tells you to let that mod win the file conflict. This is the usual
  cause when an up-to-date mod shows up here: an older mod (or an old bundled copy) overwrites its DLL. On Old-Gen a
  copy that "decides for itself at start-up" may run, so it is offered as something to try. Under MO2 only the
  instance that manages your Fallout 4 folder is used. If the report says the game is not loading the winning mod's
  copy, the file comes from MO2's **Overwrite** folder (bottom of the left pane) or was put in `Data\F4SE\Plugins` by
  hand: remove that old copy, moving mods in the left pane will not help.
- **Time limit**: this look-up stops after 2 seconds on a huge mod list and the report says so; a Vortex state backup
  over 64 MB is not read.
- **Build date**: checked against the release date of the compiler that built the DLL, so a wrong date inside the DLL
  or a file date reset by an archive does not make a new mod look old.

### "The Address Library file for this game version ... is missing"

Almost every F4SE plugin needs [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327),
the file for your exact game version (for 1.11.240: `version-1-11-240-0.bin`).

### "Data\F4SE\Plugins\f4rd-runtime.bin is missing, and X.dll need(s) it"

Plugins built on CommonLibF4RD need the [Runtime Database](https://www.nexusmods.com/fallout4/mods/108394) mod.
Without it they show an error box and stop.

### "A.esp needs the master B.esm, which is installed but NOT active" / "NOT installed"

The game crashes (often in `Actor::InitItemImpl`) when a plugin's master is missing. Activate or install the
master, or deactivate the plugin. Patches made for a mod you do not use are the usual cause.

### "A.esp loads before its master B.esm"

The game does not move a master forward, so this crashes the same way as an inactive master. Sort with LOOT or your
mod manager so masters load first. If the message says A.esp **has the master (ESM) flag**, sorting cannot fix it:
the game always loads master-flagged plugins before normal ones, so A.esp can never come after B.esp. Deactivate
A.esp, or find a version that does not need B.esp.

### "The Address Library file ... is there, but ..."

The file is cut short, damaged, or an old file that CommonLib-based plugins refuse. F4SE still loads those plugins,
and they show an error and close the game. Download the file for your exact game version again and replace it.
Vitals keeps running and skips its own address check.

### "N full plugins are active" / "N light plugins"

The game allows 254 full plugins (plus the base game) and 4096 light (ESL) ones.

### "Loose mod files are installed, but the game's INI settings do not turn on loose files"

Mods with loose meshes, textures or interface files only work when `Fallout4Custom.ini` has, under `[Archive]`,
`bInvalidateOlderFiles=1` and an empty `sResourceDataDirsFinal=`. Otherwise the game silently ignores those files.
Add the two lines (BethINI and most managers can do it). Vitals reads the INI files the way the game does, so under
Mod Organizer 2 with profile-specific INI files it already reads the profile's files: add the lines there. Empty
asset folders a mod manager left behind do not count as loose files.

### Knowledge rules (old interface files, conflicts, known DLLs)

These come from `Vitals\knowledge\*.txt`. Each message says what was found and why it matters. For example a
`Data\Interface\MainMenu.swf` made before 2024-04-25 crashed the main menu on 1.11.240 in our testing with three
different mods. Find which mod provides the file (in Vortex: the file conflicts view; in MO2: the Data tab) and
disable it.

### "Functions detoured by mods" (address check)

Lists, per DLL, how many engine functions it hooks at the main menu. This is normal for mods like Buffout 4,
Addictol and X-Cell. "unattributed trampoline" means the hook jumps to memory outside any DLL, which is how some
frameworks hook. The game's own jump stubs are no longer counted. Use `defib_logs.py --compare` to see which hooks a
newly installed mod added.

## Settings

All in `Data\F4SE\Plugins\Vitals.ini`, each with a comment. Reinstalling replaces the file, so note your changes.
The most useful:

* `iPopupMode=0` turns the message box off (the report is still written). The box's **Don't show** button stops it listing those plugins again; problems that may crash the game still show.
* `iPopupMode=1` (default) shows the box on every launch while there is something to list. **Continue** and **Stop game** change nothing; only **Don't show** silences plugins. `iPopupMode=2` behaves the same.
* `iKeepSessions=10` keeps more history.
* `bSharePack=0` always off, `1` on; commented out (default) follows the installer's choice (`share=1` in `Data\F4SE\Plugins\Vitals\share-optin.txt`).

## Comparing launches

```bat
python tools\defib_logs.py "%USERPROFILE%\Documents\My Games\Fallout4" --compare
python tools\defib_logs.py "%USERPROFILE%\Documents\My Games\Fallout4" --history
python tools\defib_logs.py "%USERPROFILE%\Documents\My Games\Fallout4" --compare 3
```
The first compares with the previous launch, `--history` lists kept sessions, `--compare 3` compares with the
third newest. To find what one mod changes: launch without it, launch with it, compare.

## Checking without launching

```bat
python tools\defib_preflight.py "C:\Program Files (x86)\Steam\steamapps\common\Fallout 4"
```
Same DLL, load-order and rule checks, plus which mod deployed each file in `Data\Interface` (Vortex). For MO2
pass the profile's plugins.txt with `--plugins-txt`. Exit code 1 means problems were found.

## Asking for help

Share `Vitals-Report.txt` (and `Vitals.jsonl` if asked). They contain no personal paths.
