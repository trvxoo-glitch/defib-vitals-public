# FAQ

**The game started with no box. Did Vitals run?** Most likely yes. The box opens only when something needs attention. Open `Documents\My Games\Fallout4\F4SE\Vitals\Vitals-Report.txt`: it is written on every launch, and when there is nothing wrong it begins with "ALL CLEAR". If the file is missing or old, Vitals did not load: see [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

**Will Vitals fix my mods?** No. It tells you what is wrong and what to do. It never edits, disables or moves files.

**Is it safe to leave installed?** Yes. It is read only and sends nothing anywhere. See [PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md).

**Does it slow the game down?** It adds about a second at start-up (more with a huge Vortex or MO2 mod list and several plugins that are not running; the part that looks through your mods stops after 2 seconds) and nothing while you play.

**Do I still need Buffout 4 / Addictol / CLASSIC?** Yes, keep them. They explain crashes after they happen. Vitals explains problems that never crash visibly, such as plugins that did not load.

**It says a plugin "is not running", but the mod works.** Check `f4se.log` for that DLL. Vitals applies F4SE's own rules for whether a DLL loads, so a plugin F4SE loaded is not listed as not running. Helper DLLs that are not F4SE plugins are listed as notes, and a mod can work without its DLL if the DLL was only one of its optional parts. If you think a message is wrong, report it with `Vitals-Report.txt` (see "Where do I report a problem?" below).

**What does "no F4SEPlugin_Version data" mean?** The DLL only has the old plugin entry point. F4SE 0.7.x (Next-Gen and Anniversary) ignores such plugins. If it was built before April 2024 it was made for Old-Gen 1.10.163 and needs an updated version; if built later it may have a newer file.

**The start-up box is annoying.** Press **Don't show** in the box: it stops listing those plugins, and only a problem that may crash the game opens it again. **Continue** only starts the game, so the box comes back next launch. Or set `iPopupMode=0` in `Vitals.ini` to turn it off completely. The report is still written.

**Which game versions work?** One file for all of them: 1.10.163 (Old-Gen), 1.10.980 and 1.10.984 (Next-Gen), and the Anniversary builds 1.11.137, 1.11.159, 1.11.169, 1.11.191, 1.11.221 and 1.11.240. The list is exact on purpose: on a build not listed F4SE skips Vitals instead of risking a crash. Vitals needs an F4SE that supports your build.

**Does it work with Mod Organizer 2?** Yes. Under MO2 the load order Vitals reads is the profile's `plugins.txt` as the game sees it, and for plugins that are not running it reads the MO2 instance that manages your Fallout 4 folder and its selected profile's `modlist.txt`.

**Does it work with Vortex?** Yes. It also reads Vortex's `vortex.deployment.json` and its newest state backup (the update check Vortex already did) to say which mod installed a DLL and whether Nexus has a newer version. Vitals itself never goes online.

**What about the Epic and Microsoft Store versions?** F4SE does not support them, so neither does Vitals.

**How do I share my results?** Send `Vitals-Report.txt`. Your Windows user name is already replaced, but the report still names your mods and your load order, so read it first. To help find broken mods across many setups, see the optional share pack in [PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md#the-share-pack-opt-in-off-by-default).

**Why does it say my `Fallout4Custom.ini` is missing settings?** Loose meshes, textures and interface files are ignored unless `bInvalidateOlderFiles=1` and an empty `sResourceDataDirsFinal=` are set under `[Archive]`. See the message's "what to do".

**Does Vitals send my data anywhere?** No. It has no network code. If you opt in to the share pack, it writes a zip
of your logs with personal details scrubbed out, and you decide whether to send it. See
[PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md#the-share-pack-opt-in-off-by-default).

**I opted in to the share pack. How do I stop it?** Reinstall and pick **No thanks**, delete
`Data\F4SE\Plugins\Vitals\share-optin.txt`, or set `bSharePack=0` in `Vitals.ini`. The next launch deletes the pack
and its random id.

**Can a rule apply to one game version only?** Yes, since 0.4.0: write the kind as `oldfile@NG,AE` (see
[FILE_FORMATS.md](FILE_FORMATS.md#knowledge-rules-dataf4sepluginsvitalsknowledgetxt)).

**Where do I report a problem?** On the project's GitHub page: https://github.com/trvxoo-glitch/defib-vitals-public/issues. An issue
is public, so read what you attach first (the report names your mods; the share pack is scrubbed but open it anyway). Or send
the share pack through the Vitals Discord server (see [PRIVACY_AND_SAFETY.md](PRIVACY_AND_SAFETY.md)).

**What does the "Last launch" line at the top of the report mean?** It is a recap of the previous launch taken from its own
records, with the game's data-ready and save-loaded times, and a crash log's name if one was written after it started. See
[USER_GUIDE.md](USER_GUIDE.md).
