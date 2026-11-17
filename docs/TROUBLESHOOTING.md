# Troubleshooting

## Vitals itself

| Symptom | Cause and fix |
| --- | --- |
| The game started with no box | Normal when nothing needs attention. Open `Vitals-Report.txt` in the Vitals folder: it is written on every launch and starts with "ALL CLEAR" when there is nothing to show. A missing or old file means Vitals did not load (next row) |
| No `Documents\My Games\Fallout4\F4SE\Vitals` folder | Vitals did not load. Check `f4se.log` for `Vitals.dll`. You need F4SE for your game version; Vitals supports 1.10.163, 1.10.980/984 and 1.11.137 to 1.11.240 |
| `f4se.log`: Vitals "disabled, incompatible with current runtime" | A game patch newer than this Vitals. Vitals lists the exact builds it supports on purpose, so F4SE skips it instead of crashing. Wait for an update |
| I pressed "Don't show" and want the plugin list back | Delete `popup_disabled.txt` in the Vitals output folder |
| The message box every launch | Normal: it shows every launch while there is something to list, and **Continue** does not silence it. Press **Don't show** to silence plugins that are not running (problems that may crash the game still show), fix the problem, or set `iPopupMode=0` |
| No message box although the report lists problems | `iPopupMode=0`, you pressed **Don't show** earlier (delete `popup_disabled.txt`), or the items are warnings other than plugins that are not running, which never open it |
| `Vitals-Report.txt` has no ADDRESS CHECK section | The game did not reach the main menu, or `bAddressCheck=0` |
| Address check: `patched_in_place` | A fix mod switched that function off (it now returns at once). The bytes do not say which mod; the report names a running DLL that normally does it when it knows one |
| Address check: many `differs` | The function was patched in some other way, or the address list does not match your build. Report it with `Vitals.jsonl` |
| A "Last launch" line says "Vitals saw no later event" | The previous launch ended without a clean exit. That can be a crash, Task Manager, or a power cut: if a crash log was written after it started, the line names it |
| Address check: `resolved_no_baseline` on Old-Gen | Expected. The Steam 1.10.163 exe is encrypted on disk. Optional: capture a local baseline once (`bCaptureBaseline=1`, F4SE + Vitals only, then back to 0) |
| A load-order warning for a plugin that is fine | Vitals reads `%LOCALAPPDATA%\Fallout4\plugins.txt` and `Fallout4.ccc`. If your manager keeps the order elsewhere and it does not reach the game, report it with the report file |
| Start-up a little slower | The doctor reads each plugin that is not running once and looks through your mod folders for other copies (limited to 2 seconds; the report's "Mod manager:" line shows the time). `bDoctor=0` turns it off |
| The report says the other-copies look-up "stopped at the time limit" | A very large mod list on a slow disk. The rest of the report is complete; the list of other copies may be short |
| No `Vitals-Share.zip`, or it is old | The pack is only made when `Data\F4SE\Plugins\Vitals\share-optin.txt` has a `share=1` line (installer: **Yes**) or `bSharePack=1`. An older test build's marker file does not count: reinstall and pick Yes. If the zip was open in Explorer or Discord, look for `Vitals-Share-new.zip` |
| I picked No thanks but still get a pack | A merge-style reinstall from a version before 0.4.0 can leave an old marker. Reinstall the newest Vitals and pick **No thanks** (it writes `share=0`), or set `bSharePack=0` |

## Problems Vitals points at

| Report says | Usually means |
| --- | --- |
| DLL built for Old-Gen only | Mod abandoned before the 2024 updates. Search Nexus for "NG", "AE" or "port" versions; the Defib project tracks them |
| Address Library missing | Install the "All in one" Address Library file or the one for your build. Vitals reports it as a problem only when there is more than one DLL in `Data\F4SE\Plugins` (with Vitals alone it is not needed) |
| Address Library there but not usable | Download it again and replace the file: it is cut short, damaged, or an old file CommonLib refuses |
| A.esp has the master (ESM) flag and loads before its master B.esp | Sorting cannot fix it. Deactivate A.esp or get a version that does not need B.esp |
| `f4rd-runtime.bin` missing | Install Runtime Database (Nexus 108394) |
| Master installed but not active | Usually a patch for a mod you disabled. Disable the patch |
| Old `MainMenu.swf` | Crash at the main menu on Anniversary. Disable the mod providing it; in Vortex make sure no other mod provides the same file |
| Old `FontConfig.txt` | Boxes instead of text |
| ENB and Community Shaders both installed | ENB effects may not apply. Use one |

## Where else to look

* `Documents\My Games\Fallout4\F4SE\f4se.log`: which DLLs F4SE loaded.
* Buffout 4 / Addictol crash logs (`Documents\My Games\Fallout4\F4SE\crash-*.log`) after a crash, and CLASSIC to read them.
* `tools\defib_logs.py` combines all of these with Vitals' files into one report.
