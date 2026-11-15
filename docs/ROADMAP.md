# Vitals roadmap

Updated 2026-11-13 with 0.3.0. Items move to CHANGELOG.md when done. Ask the project owner before starting a release.

## Done in 0.3.0
Preflight report without launching (old item 1), last 5 sessions (3), load order from inside the game (4), hook
owners and game stubs (5), plain-language report (6).

## Next (0.3.x / 0.4)
1. **In-game test of 0.3.0** on 1.11.240 and 1.10.163 (test matrix in MAINTAINING.md), then release on GitHub.
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
8. Test on 1.10.984 (NG) and VR if available.
9. Nexus release. Owner's decision, after the in-game test and a GitHub release.
10. Translations of the report messages (all player-facing text is in `Doctor.cpp` and the rules).

## Not planned
* Fixing things automatically (disabling plugins, editing load orders). Vitals stays read-only; it tells you what to do.
* Replacing crash loggers. Buffout 4 / Addictol / CLASSIC do that well; Vitals covers what they cannot see.
