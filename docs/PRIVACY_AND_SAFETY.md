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
Only inside `Documents\My Games\Fallout4\F4SE\Vitals\`: the report, summary, `Vitals.jsonl` (plus kept history), `breadcrumbs.bin`, `status.json`, `popup_seen.txt`, `popup_disabled.txt`, `hashes.txt`, and, only if you turn it on once, local baselines. Nothing is written anywhere else.

## What it sends
**Nothing.** It has no network code. No telemetry, no update check, no account. Files never leave your PC unless you send them.

## Personal data
Your Windows user name and profile path are replaced with `<user>` / `%USERPROFILE%` in everything Vitals writes, so the report and log are safe to share when asking for help. Mod and file names are included, since that is the point of the report.

## Local baselines
`bCaptureBaseline=1` (developer, one launch) saves the first bytes of game functions as seen in memory. Those files stay on your PC and are never committed or uploaded. Vitals refuses to capture while other F4SE plugins are loaded, so their patches are not saved as "expected".

The shipped list `Vitals\addresses\commonlibf4.txt` does carry short expected-byte signatures (the first 16 bytes of each function) for Anniversary 1.11.240, the same kind of short pattern many F4SE plugins ship to find functions. No game executable, no full function and nothing from encrypted executables is shipped.

## Removing it
Delete `Vitals.dll`, `Vitals.ini` and the `Vitals` folder from `Data\F4SE\Plugins`. Optionally delete the output folder. Nothing else holds a reference to it.

## Mod authors
Vitals' `Vitals_Breadcrumb` export is the only thing other plugins can call; it writes two short strings into Vitals' own memory-mapped file.
