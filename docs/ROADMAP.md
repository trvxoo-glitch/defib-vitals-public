# Vitals roadmap

Updated 2026-11-15 with 0.4.0. Items move to CHANGELOG.md when done. Ask the project owner before starting a release.

## Done
* 0.3.0: preflight report without launching, last 5 sessions, load order from inside the game, hook owners and game
  stubs, plain-language report.
* 0.4.0: the opt-in share pack and its scrubber, other-copies and mod-manager update evidence, Copy report, per-version
  knowledge rules.

## Next (0.4.x / 0.5)
1. **0.4.0 on more setups**: in-game test on 1.10.163 and 1.10.984 and under Mod Organizer 2 (test matrix in MAINTAINING.md).
2. **Crash triage in the analyzer.** Combine the phase reached (crash before GameDataReady = plugin or master
   problem; in the main menu = interface file), the crash log's top frames (Buffout 4 / Addictol format), and the
   doctor's findings into one verdict.
3. **Knowledge feed from the census.** Each confirmed census finding becomes a rule; a generated
   `defib-census.txt` ships with each release. Rule IDs and a "found on" date per rule.
4. **Doctor: more checks.** BA2 count against the archive limit; F4SE version older than the plugins need;
   duplicate DLLs (same plugin name twice); ESL-flagged .esp beyond the limit; `Fallout4Custom.ini` sanity
   (archive invalidation for loose files).
5. **Doctor findings in the address check:** a port's own address list failing becomes a doctor problem naming the mod.

## Later
6. Batch-bisect helper (disable half the mods, retest) for crashes that name no mod.
7. Opt-in Papyrus call trace for remaking dead mods.
8. Test on 1.10.984 (NG) by the owner (a player confirmed it works), and VR if available.
9a. Share packs (0.4.0 writes them): collect them in the Discord server (`docs/DISCORD_INBOX.md`), then turn
   `tools/share_inbox.py summary` candidates into confirmed rules.
9. Nexus: each release's upload is the owner's action.
10. Translations of the report messages (all player-facing text is in `Doctor.cpp` and the rules).

## Not planned
* Fixing things automatically (disabling plugins, editing load orders). Vitals stays read-only; it tells you what to do.
* Replacing crash loggers. Buffout 4 / Addictol / CLASSIC do that well; Vitals covers what they cannot see.
