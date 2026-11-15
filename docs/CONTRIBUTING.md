# Contributing

Vitals is GPL-3.0 with the modding and linking exceptions (`EXCEPTIONS`). Changes made with AI help are welcome and must say so (see `AI_DISCLOSURE.md`).

## The most useful contribution: a knowledge rule
A rule teaches the doctor to recognise something that breaks setups. Format and special names: [FILE_FORMATS.md](FILE_FORMATS.md#knowledge-rules-dataf4sepluginsvitalsknowledgetxt). To be added to `defib-known.txt` a rule must have:
1. **A confirmed finding**: the problem was seen in game, with the game build, F4SE version and mods involved.
2. **A specific match**: a file name or a date, not a broad pattern. A false problem in a player's start-up box costs trust.
3. **A dated comment** above it saying what was seen (see the existing rules).
4. **A plain message** that says what is wrong and what to do.

Put experimental rules in your own `Vitals\knowledge\*.txt`; Vitals reads all of them and updates only replace `defib-known.txt`.

## Bug reports
Include `Vitals-Report.txt` (safe to share) and, if asked, `Vitals.jsonl`; the game build, F4SE version and mod manager; and what you expected. For a wrong message, include the line.

## Code changes
- Read [ARCHITECTURE.md](ARCHITECTURE.md) first, especially the design rules (read only, never stop the game, library-provided addresses only, exact build list).
- Build: [BUILDING.md](BUILDING.md). Tests: `python -m unittest discover -s tests` must pass; CI also builds the DLL and checks the FOMOD package.
- Keep player-facing text in plain language with a "what to do".
- Update [FILE_FORMATS.md](FILE_FORMATS.md), the user guide and `CHANGELOG.md` with the change. The DLL's F4SE rules (`include/Vitals/Rules.h`) and `tools/f4se_dll_scan.py` change together, with a line in `tests/loader_cases.txt`.
- Never add files that belong to someone else (other mods' DLLs, `f4rd-runtime.bin`, game executables, baselines, Address Library files).
