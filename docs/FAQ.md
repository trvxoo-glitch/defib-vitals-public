# FAQ

**Will Vitals fix my mods?** No. It tells you what is wrong and what to do. It never edits, disables or moves files.

**Is it safe to leave installed?** Yes. It is read only and sends nothing anywhere. See [PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md).

**Does it slow the game down?** It adds under a second at start-up (it reads each DLL in `Data\F4SE\Plugins` once) and nothing while you play.

**Do I still need Buffout 4 / Addictol / CLASSIC?** Yes, keep them. They explain crashes after they happen. Vitals explains problems that never crash visibly, such as plugins that did not load.

**It says a plugin "will not load", but the mod works.** Check `f4se.log` for that DLL. Vitals applies F4SE's own rules, so a mod that really works is not listed. If you think a message is wrong, report it with `Vitals-Report.txt`.

**What does "no F4SEPlugin_Version data" mean?** The DLL only has the old plugin entry point. F4SE 0.7.x (Next-Gen and Anniversary) ignores such plugins. If it was built before April 2024 it was made for Old-Gen 1.10.163 and needs an updated version; if built later it may have a newer file.

**The start-up box is annoying.** Press **Don't show** in the box: it stops listing those plugins, and only a problem that may crash the game opens it again. **Continue** only starts the game, so the box comes back next launch. Or set `iPopupMode=0` in `Vitals.ini` to turn it off completely. The report is still written. By default each item is shown once.

**Which game versions work?** 1.10.163 (Old-Gen, tested), 1.10.980/984 (Next-Gen, built, untested), 1.11.137 to 1.11.240 (Anniversary, tested on 1.11.240). One file for all.

**Does it work with Mod Organizer 2?** Yes. Under MO2 the load order Vitals reads is the profile's `plugins.txt` as the game sees it.

**What about the Epic and Microsoft Store versions?** F4SE does not support them, so neither does Vitals.

**How do I share my results safely?** Send `Vitals-Report.txt`. Your Windows user name is already replaced.

**Why does it say my `Fallout4Custom.ini` is missing settings?** Loose meshes, textures and interface files are ignored unless `bInvalidateOlderFiles=1` and an empty `sResourceDataDirsFinal=` are set under `[Archive]`. See the message's "what to do".
