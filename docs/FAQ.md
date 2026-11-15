# FAQ

**Will Vitals fix my mods?** No. It tells you what is wrong and what to do. It never edits, disables or moves files.

**Is it safe to leave installed?** Yes. It is read only and sends nothing anywhere. See [PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md).

**Does it slow the game down?** It adds about a second at start-up (more with a huge Vortex or MO2 mod list and several plugins that are not running; the part that looks through your mods stops after 2 seconds) and nothing while you play.

**Do I still need Buffout 4 / Addictol / CLASSIC?** Yes, keep them. They explain crashes after they happen. Vitals explains problems that never crash visibly, such as plugins that did not load.

**It says a plugin "will not load", but the mod works.** Check `f4se.log` for that DLL. Vitals applies F4SE's own rules, so a mod that really works is not listed. If you think a message is wrong, report it with `Vitals-Report.txt`.

**What does "no F4SEPlugin_Version data" mean?** The DLL only has the old plugin entry point. F4SE 0.7.x (Next-Gen and Anniversary) ignores such plugins. If it was built before April 2024 it was made for Old-Gen 1.10.163 and needs an updated version; if built later it may have a newer file.

**The start-up box is annoying.** Press **Don't show** in the box: it stops listing those plugins, and only a problem that may crash the game opens it again. **Continue** only starts the game, so the box comes back next launch. Or set `iPopupMode=0` in `Vitals.ini` to turn it off completely. The report is still written.

**Which game versions work?** 1.10.163 (Old-Gen, tested), 1.10.980/984 (Next-Gen, a player confirmed 1.10.984), 1.11.137 to 1.11.240 (Anniversary, tested on 1.11.240). One file for all.

**Does it work with Mod Organizer 2?** Yes. Under MO2 the load order Vitals reads is the profile's `plugins.txt` as the game sees it, and for plugins that are not running it reads the MO2 instance that manages your Fallout 4 folder and its selected profile's `modlist.txt`.

**Does it work with Vortex?** Yes. It also reads Vortex's `vortex.deployment.json` and its newest state backup (the update check Vortex already did) to say which mod installed a DLL and whether Nexus has a newer version. Vitals itself never goes online.

**What about the Epic and Microsoft Store versions?** F4SE does not support them, so neither does Vitals.

**How do I share my results safely?** Send `Vitals-Report.txt`. Your Windows user name is already replaced. To help find broken mods across many setups, see the optional share pack in [PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md#the-share-pack-opt-in-off-by-default).

**Why does it say my `Fallout4Custom.ini` is missing settings?** Loose meshes, textures and interface files are ignored unless `bInvalidateOlderFiles=1` and an empty `sResourceDataDirsFinal=` are set under `[Archive]`. See the message's "what to do".

**Does Vitals send my data anywhere?** No. It has no network code. If you opt in to the share pack, it writes a zip
of your logs with personal details scrubbed out, and you decide whether to send it. See
[PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md#the-share-pack-opt-in-off-by-default).

**I opted in to the share pack. How do I stop it?** Reinstall and pick **No thanks**, delete
`Data\F4SE\Plugins\Vitals\share-optin.txt`, or set `bSharePack=0` in `Vitals.ini`. The next launch deletes the pack
and its random id.

**Can a rule apply to one game version only?** Yes, since 0.4.0: write the kind as `oldfile@NG,AE` (see
[FILE_FORMATS.md](FILE_FORMATS.md#knowledge-rules-dataf4sepluginsvitalsknowledgetxt)).
