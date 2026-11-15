# Privacy and safety: exactly what Vitals does

Vitals is meant to be left installed, so this page says plainly what it reads, writes and sends. If anything here stops being true, that is a bug.

## It never changes your game
- It writes **nothing** to game memory, installs **no** engine hooks or patches, adds **no** .esp/.esm/.esl, and writes **nothing** to your saves.
- Every read of game memory goes through a checked read (the page must be committed and readable first), so a bad address is a report line, never a crash.
- It does not disable, move, rename or delete any mod file. The doctor only tells you what is wrong.
- The single exception to "keeps running": the **Stop game** button in the start-up box ends the game when you press it. It is never pressed for you; Esc and the close button cannot trigger it.

## What it reads
- The DLL files in `Data\F4SE\Plugins` (headers and export names, to explain why F4SE skipped them), and their build dates.
- `Documents\My Games\Fallout4\F4SE\f4se.log` (F4SE's own log), `Fallout4Custom.ini` and `Fallout4.ini` (the [Archive] settings only), `%LOCALAPPDATA%\Fallout4\plugins.txt`, `Fallout4.ccc`, and the headers of the plugins in the load order (masters and flags).
- A few file names and dates in the game folder (for the knowledge rules).
- Engine addresses listed in `Vitals\addresses\*.txt`, to compare their first bytes.

## What it writes
Only inside `Documents\My Games\Fallout4\F4SE\Vitals\`: the report, summary, `Vitals.jsonl` (plus kept history), `breadcrumbs.bin`, `status.json`, `popup_disabled.txt`, `hashes.txt`, and, only if you turn them on, local baselines and the share pack (`Vitals-Share.zip`, `share-id.txt`). Nothing is written anywhere else. The **Copy report** button in the start-up box puts the report on the clipboard when you press it.

## What it sends
**Nothing.** It has no network code. No telemetry, no update check, no account. Files never leave your PC unless you send them.

## The share pack (opt-in, off by default)
If you pick **Yes, help improve Vitals** in the installer (or set `bSharePack=1`), Vitals writes
`Vitals-Share.zip` next to the report after each session. It is for you to send if you want to help find broken mods;
Vitals never sends it.

**What is in it.** A fixed list, never a whole folder:

| File | Taken from | Notes |
| --- | --- | --- |
| `manifest.json` | made by Vitals | Vitals, game and F4SE versions, the date (day only), a random install id, each file's size and checksum |
| `Vitals-Report.txt`, `Vitals-summary.txt`, `Vitals.jsonl`, `Vitals.prev.jsonl`, `history/` (newest 3), `status.json`, `popup_disabled.txt` | the Vitals folder | |
| `breadcrumbs.txt` | `breadcrumbs.bin` and `.prev.bin` | port and step names as text; the binary file is not included |
| `f4se.log`, `f4se_loader.log` | `My Games\Fallout4\F4SE` | |
| `crash/crash-*.log` | Buffout 4-style crash logs there: up to 3, last 14 days | text read from game memory is removed (below) |
| `papyrus-summary.txt` | `Papyrus.0.log`, if it exists | counts of errors per script; the log itself is not included |
| `plugins.txt`, `Fallout4.ccc` | `%LOCALAPPDATA%\Fallout4`, the game folder | the load order |
| `ini-settings.txt` | `Fallout4.ini`, `Fallout4Prefs.ini`, `Fallout4Custom.ini` | 22 named keys only (loose files, Papyrus logging, grids, resolution, frame rate) |
| `dll-sources.txt` | `Data\vortex.deployment.json` (Vortex only) | which mod folder installed each F4SE DLL |
| `system.txt` | Windows | Windows build, CPU model, core count, RAM in whole GB, GPU name, mod manager |
| `scrub-report.txt` | made by Vitals | how many items of each kind were removed, never the values |

Each file keeps its newest 2 MB and the whole pack stays under 8 MB. Never included: saves or save names,
screenshots, baselines, DLLs or other mod files, whole INI files, the raw Papyrus log.

**The scrubber** (`include/Vitals/Scrub.h`) runs over every file before it goes in:

| Found | Becomes |
| --- | --- |
| The game folder, Documents, AppData, `%USERPROFILE%`, Vortex's staging folder | `<game>`, `<docs>`, `<appdata>`, `<localappdata>`, `%USERPROFILE%`, `<staging>` |
| Your Windows user name as a folder or a whole word, any `\Users\<name>` or `/home/<name>` | `<user>` |
| Your PC and domain name | `<pc>` |
| Mod Organizer 2 profile names | `<profile>` |
| Steam account ids (`userdata\<n>`, SteamID64) | `<account>` |
| Windows account ids (SIDs), GUIDs | `<sid>`, `<guid>` |
| Email addresses | `<email>` |
| `apikey=`, `token=`, `password=`, `Bearer ...` values and long random-looking keys | `<secret>` |
| IP addresses (not version numbers such as 1.11.240.0) and network card addresses | `<ip>`, `<mac>` |
| Any other full path: all but its last two names | `<path>\Folder\File.dll` |
| Crash logs: quoted text read from game memory unless it looks like a file, form or script name (a character's name, a save path) | `<text>` |
| Papyrus: a name printed next to the player (form 00000014) | `<player>` |

Mod and plugin file names and SHA-256 hashes stay: they are the point of the pack. The scrubber is tested against
`tests/scrub_cases.txt` (made-up personal data) in CI; a case is added for every miss someone reports.

**Ten layers.** The list above is an allowlist; raw files are summarised where a summary is enough; files are scrubbed
twice (Vitals, then the upload server with the same rules); paths are folded; the install id is random (not from your
PC) and a new one is made if you delete `share-id.txt`; you can read the zip before sending it and untick files on the
upload page; nothing leaves without you; the server keeps nothing about the sender; packs are deleted after 90 days or
on request; and only counts across five or more installs are ever published.

**Sending it.** Your choice, any time: attach it to a [GitHub issue](https://github.com/trvxoo-glitch/defib-vitals-public/issues),
or (once it is live) drag it onto the upload page, which gives you a code to quote in a bug report and to delete the
pack. The server is in `server/` (see `server/README.md` for exactly what it stores: the pack, the Vitals and game
version, the random install id and the date; no IP address, account or cookie).

**Turning it off.** Delete `Data\F4SE\Plugins\Vitals\share-optin.txt`, set `bSharePack=0`, or reinstall and pick
**No thanks**. At the next launch Vitals deletes `Vitals-Share.zip` and `share-id.txt`.

## Personal data
Your Windows user name and profile path are replaced with `<user>` / `%USERPROFILE%` in everything Vitals writes, so the report and log are safe to share when asking for help. Mod and file names are included, since that is the point of the report. The share pack goes further (above).

## Local baselines
`bCaptureBaseline=1` (developer, one launch) saves the first bytes of game functions as seen in memory. Those files stay on your PC and are never committed or uploaded. Vitals refuses to capture while other F4SE plugins are loaded, so their patches are not saved as "expected".

The shipped list `Vitals\addresses\commonlibf4.txt` does carry short expected-byte signatures (the first 16 bytes of each function) for Anniversary 1.11.240, the same kind of short pattern many F4SE plugins ship to find functions. No game executable, no full function and nothing from encrypted executables is shipped.

## Removing it
Delete `Vitals.dll`, `Vitals.ini` and the `Vitals` folder from `Data\F4SE\Plugins`. Optionally delete the output folder (it holds the share pack, if you made one). Nothing else holds a reference to it.

## Mod authors
Vitals' `Vitals_Breadcrumb` export is the only thing other plugins can call; it writes two short strings into Vitals' own memory-mapped file.
