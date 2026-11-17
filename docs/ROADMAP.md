# Vitals roadmap

Updated 2026-11-17 with 0.4.2. Items move to CHANGELOG.md when done. Ask the project owner before starting a release.

## Done
* 0.3.0: preflight report without launching, last 5 sessions, load order from inside the game, hook owners and game
  stubs, plain-language report.
* 0.4.2: report layout (details of a not-running DLL on their own line); last-launch recap and "changed since last launch";
  rules stamp, newest rules file wins, version-tagged census rules, `dllbuild` and `patchedby` rule kinds; scrubber and image-scan fixes from the code review.
* 0.4.1: ALL CLEAR line in the report; the download split into the main file and an optional Extra Docs file.
* 0.4.0: the opt-in share pack and its scrubber, other-copies and mod-manager update evidence, Copy report, per-version
  knowledge rules.

## Next (0.4.x / 0.5)
1. **More test setups**: in-game tests of the 0.4.x line on 1.10.984 (Next-Gen; a player confirmed it runs) and under Mod Organizer 2
   (test matrix in MAINTAINING.md). VR is not supported.
2. **Crash triage in the analyzer.** Combine the phase reached (crash before GameDataReady = plugin or master
   problem; in the main menu = interface file), the crash log's top frames (Buffout 4 / Addictol format), and the
   doctor's findings into one verdict.
3. **Knowledge feed from the census.** Each confirmed census finding becomes a rule; a generated
   `defib-census.txt` ships with each release (rules are already tagged with the game versions they apply to).
   Later: generate the troubleshooting table of named mods from it.
4. **Doctor: more checks.** BA2 count against the archive limit (the limit may depend on the game version: verify it before
   writing a number anywhere); F4SE version older than the plugins need; duplicate DLLs (same plugin name twice).
5. **A plugin that loads but lists other game builds** (SUP_F4SE 0.77 on 1.11.240 crashed at about 46 s and was reported as 0
   problems): the `dllbuild` rule kind exists; add the rule only after a fix test confirms the cause.
6. **Doctor findings in the address check:** a port's own address list failing becomes a doctor problem naming the mod.

## Later
7. Batch-bisect helper (disable half the mods, retest) for crashes that name no mod.
8. Opt-in Papyrus call trace for remaking dead mods.
9. Share packs (0.4.0 writes them): collect them in the Discord server (`docs/DISCORD_INBOX.md`), then turn
   `tools/share_inbox.py summary` candidates into confirmed rules.
10. Nexus: each release's upload is the owner's action.
11. Translations of the report messages (all player-facing text is in `Doctor.cpp` and the rules).

## Not planned
* Fixing things automatically (disabling plugins, editing load orders). Vitals stays read-only; it tells you what to do.
* Replacing crash loggers. Buffout 4 / Addictol / CLASSIC do that well; Vitals covers what they cannot see.
