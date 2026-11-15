# Privacy and safety: exactly what Vitals does

Vitals is meant to be left installed, so this page says plainly what it reads, writes and sends. If anything here stops being true, that is a bug.

## It never changes your game
- It writes **nothing** to game memory, installs **no** engine hooks or patches, adds **no** .esp/.esm/.esl, and writes **nothing** to your saves.
- Every read of game memory goes through a checked read (the page must be committed and readable first), so a bad address is a report line, never a crash.
- It does not disable, move, rename or delete any mod file. The doctor only tells you what is wrong.
- The single exception to "keeps running": the **Stop game** button in the start-up box ends the game when you press it. It is never pressed for you; Esc and the close button cannot trigger it.

## What it reads
- The DLL files in `Data\F4SE\Plugins` (headers and export names, to explain why F4SE skipped them), and their build dates.
- `Documents\My Games\Fallout4\F4SE\f4se.log` (F4SE's own log), `Fallout4Custom.ini`, `Fallout4Prefs.ini` and `Fallout4.ini` (the [Archive] settings only; the share pack reads 22 named keys, see below), `%LOCALAPPDATA%\Fallout4\plugins.txt`, `Fallout4.ccc`, and the headers of the plugins in the load order (masters and flags).
- A few file names and dates in the game folder (for the knowledge rules).
- Only when an F4SE plugin is not running: your mod manager's own records, to explain it. For Vortex,
  `Data\vortex.deployment.json`, the copies of that DLL in its staging folder, and the newest state backup in
  `%APPDATA%\Vortex\temp\state_backups_full` (only each mod's folder name, version and newest version are used). For
  MO2, the `ModOrganizer.ini` of the instance that manages this game folder (other games' instances are skipped),
  its selected profile's `modlist.txt` (which mod wins a file conflict), the copies of that DLL in its mods folder,
  and those mods' `meta.ini`. Nothing from these goes into the share pack except what the report already says and, for Vortex, the DLL-to-mod list in `dll-sources.txt`.
- Engine addresses listed in `Vitals\addresses\*.txt`, to compare their first bytes.

## What it writes
Only inside `Documents\My Games\Fallout4\F4SE\Vitals\`: the report, summary, `Vitals.jsonl` (plus kept history), `breadcrumbs.bin`, `status.json`, `popup_disabled.txt`, `hashes.txt`, and, only if you turn them on, local baselines and the share pack (`Vitals-Share.zip`, `share-id.txt`). Nothing is written anywhere else. The **Copy report** button in the start-up box puts the report on the clipboard when you press it.

## What it sends
**Nothing.** It has no network code. No telemetry, no update check of its own, no account. (It can show what your mod manager's update check already found.) Files never leave your PC unless you send them.

## The share pack (opt-in, off by default)
If you pick **Yes, help improve Vitals** in the installer (or set `bSharePack=1`), Vitals writes
`Vitals-Share.zip` next to the report, a few seconds after the start-up report (while the box is open) and again when the main menu is up
(if the zip is open in another program at that moment, that launch's pack is `Vitals-Share-new.zip` instead). It is
for you to send if you want to help find broken mods; Vitals never sends it.

**What is in it.** A fixed list, never a whole folder:

| File | Taken from | Notes |
| --- | --- | --- |
| `manifest.json` | made by Vitals | Vitals, game and F4SE versions, the date (day only), a random install id, each file's size and checksum; scrubbed like the other files |
| `Vitals-Report.txt`, `Vitals-summary.txt`, `Vitals.jsonl`, `Vitals.prev.jsonl` (the session before; older history is not included), `status.json`, `popup_disabled.txt` | the Vitals folder | |
| `breadcrumbs.txt` | `breadcrumbs.bin` and `.prev.bin` | port and step names as text; the binary file is not included |
| `f4se.log`, `f4se_loader.log` | `My Games\Fallout4\F4SE` | |
| `crash/crash-*.log` | Buffout 4-style crash logs there: up to 3, last 14 days | text read from game memory is removed (below); a file name that is not just a date is renamed `crash-1.log` etc. |
| `papyrus-summary.txt` | `Papyrus.0.log` and `Papyrus.1.log` (this launch and the one before), if they exist | counts of errors per script; the logs themselves are not included |
| `plugins.txt`, `Fallout4.ccc` | `%LOCALAPPDATA%\Fallout4`, the game folder | the load order |
| `ini-settings.txt` | `Fallout4.ini`, `Fallout4Prefs.ini`, `Fallout4Custom.ini` | 22 named keys only (loose files, Papyrus logging, grids, resolution, frame rate) |
| `dll-sources.txt` | `Data\vortex.deployment.json` (Vortex only) | which mod folder installed each F4SE DLL |
| `system.txt` | Windows | Windows build, CPU model, core count, RAM in whole GB, GPU name, mod manager |
| `scrub-report.txt` | made by Vitals | how many items of each kind were removed, never the values |

Each file keeps its newest 2 MB and the whole pack stays under 8 MB. Never included: save files, screenshots,
baselines, DLLs or other mod files, whole INI files, the raw Papyrus logs. Save names that a log mentions are replaced
(below).

**The scrubber** (`include/Vitals/Scrub.h`) runs over every file before it goes in:

| Found | Becomes |
| --- | --- |
| The game folder, Documents, AppData, `%USERPROFILE%`, Vortex's staging folder | `<game>`, `<docs>`, `<appdata>`, `<localappdata>`, `%USERPROFILE%`, `<staging>` |
| Your Windows user name as a folder name, any `\Users\<name>` or `/home/<name>` | `<user>` |
| Your Windows user name as a whole word, when it is 3 letters or more and not a common word (`admin`, `player`, `steam`, `user` and a few more) | `<user>` |
| Your PC and domain name as a whole word, when 3 letters or more | `<pc>` |
| Mod Organizer 2 profile names | `<profile>` |
| Steam account ids (`userdata\<n>`, SteamID64) | `<account>` |
| Windows account ids (SIDs), GUIDs | `<sid>`, `<guid>` |
| Email addresses | `<email>` |
| `apikey=`, `token=`, `password=`, `Bearer ...` values and long random-looking keys | `<secret>` |
| IP addresses (not version numbers such as 1.11.240.0) and network card addresses | `<ip>`, `<mac>` |
| Any other full path: all but its last two names | `<path>\Folder\File.dll` |
| Save file names (`*.fos`, `*.f4se`), in a path or on their own | `<save>.fos` |
| A value after a label such as `Save name:`, `Player name =`, `"characterName":` | `<save>`, `<player>` |
| Crash logs: quoted text read from game memory unless it is a file or script name (has a `.`, `:`, `\` or `/`), or follows `EditorID:` (a character's name, `"JohnSmith"`, `"Nate_2077"`, a save path) | `<text>` |
| Papyrus: the name printed just before the player's form id (00000014), in any sentence | `<player>` |

Mod and plugin file names, Vortex mod folder names and SHA-256 hashes stay: they are the point of the pack. The
scrubber is tested against `tests/scrub_cases.txt` (made-up personal data) in CI; a case is added for every miss
someone reports.

**What the scrubber cannot know.** It removes what it can recognise. A user name of 1 or 2 letters, or one that is a
common word, is removed only as a folder name, not every time the word appears. A character name that a mod prints
in a sentence of its own, with no label, file extension or form id next to it, can stay. Open the zip and read it
before you send it if that matters to you.

**The install id.** `manifest.json` carries a random id so packs from the same install can be counted once. It is
not made from anything on your PC, but every pack from your install carries the same one, and a long load order is
distinctive too: someone who sees two of your packs can tell they came from the same setup. Delete `share-id.txt` for a
new id. Packs sent to the private Discord channel are only seen by the maintainer; a GitHub issue is public.

**Nine layers.** The list above is an allowlist; raw files are summarised where a summary is enough; files are
scrubbed (user name, PC name, emails, IPs, keys and tokens) before the zip is written; paths are folded; the install id
is random (not from your PC) and a new one is made if you delete `share-id.txt`; you can open the zip and read every
file before sending it; nothing leaves without you; each pack goes into a private channel only you and the maintainer
can see; packs are deleted after 90 days or on request; and only counts across five or more installs are ever published.

**Sending it.** Your choice, any time: join the Vitals Discord server (https://discord.gg/XghtBZUFNW), press **Send my pack**, and drag
`Vitals-Share.zip` into the private channel that opens. Or attach it to a
[GitHub issue](https://github.com/trvxoo-glitch/defib-vitals-public/issues) if you do not use Discord (an issue is public, so
read the zip first). Instructions for the person running the server are in [DISCORD_INBOX.md](DISCORD_INBOX.md).

**Vitals itself never connects to it.** Sending is you dragging a file into Discord, not the plugin: Vitals has no network code.

**Who can read packs.** Only the project maintainer, and the read-only helper tool the maintainer runs to download them
(`tools/share_inbox.py`), which reads the private channels only. Other members cannot see them. Raw packs are never
published, committed, posted or given to other people or services. What is published is counts across five or more
installs (for example "this DLL fails to load on 40 setups"), and only after a person confirms it in game. A pack is
deleted after 90 days, or on request: ask in your channel, or close it.

**What Discord sees.** Your upload is an ordinary Discord attachment, so Discord, who run the service, handle it under
their own terms and privacy policy, and your Discord name and account are visible to the maintainer in the channel. That
is the trade-off compared with a plain web form: if you want no account link at all, do not send a pack.

**Turning it off.** Set `bSharePack=0`, reinstall and pick **No thanks** (that installs the same file with
`share=0`, so it replaces an earlier Yes even when the mod manager merges files), change the first line of
`Data\F4SE\Plugins\Vitals\share-optin.txt` to `share=0`, or delete that file. Only a `share=1` line turns the
pack on. At the next launch Vitals deletes `Vitals-Share.zip`, `Vitals-Share-new.zip` and `share-id.txt`.

## Personal data
Your Windows user name and profile path are replaced with `<user>` / `%USERPROFILE%` in everything Vitals writes, so the report and log are safe to share when asking for help. Mod and file names are included, since that is the point of the report. The share pack goes further (above).

## Local baselines
`bCaptureBaseline=1` (developer, one launch) saves the first bytes of game functions as seen in memory. Those files stay on your PC and are never committed or uploaded. Vitals refuses to capture while other F4SE plugins are loaded, so their patches are not saved as "expected".

The shipped list `Vitals\addresses\commonlibf4.txt` does carry short expected-byte signatures (the first 16 bytes of each function) for Anniversary 1.11.240, the same kind of short pattern many F4SE plugins ship to find functions. No game executable, no full function and nothing from encrypted executables is shipped.

## Removing it
Delete `Vitals.dll`, `Vitals.ini` and the `Vitals` folder from `Data\F4SE\Plugins`. Optionally delete the output folder (it holds the share pack, if you made one). Nothing else holds a reference to it.

## Mod authors
Vitals' `Vitals_Breadcrumb` export is the only thing other plugins can call; it writes two short strings into Vitals' own memory-mapped file.
