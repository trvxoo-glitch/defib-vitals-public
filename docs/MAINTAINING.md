# Maintaining Vitals

How to keep Vitals working when the game, F4SE or the library changes, and how to release. Written so that
someone new (or a future session) can do it without the original authors.

## When Bethesda patches the game

Vitals lists exact builds, so on a new build F4SE skips it ("disabled, incompatible") and nothing crashes.

1. Wait for F4SE and the Address Library for the new build.
2. Check whether Dear-Modding-FO4/commonlibf4 supports the build (its `REL/Version` list and the plugin template).
3. Update the pin (below), add the build to `res/vitals/commonlibf4-plugin.cpp.in` (compatible versions) and to
   `include/Vitals/Rules.h` (`Loader::Check`) and `tools/f4se_dll_scan.py` if the loader rules changed.
4. Regenerate the address list with `--keep` so the shipped signatures and hand-set kinds survive:
   `python tools/gen_addresses_from_commonlib.py lib/commonlibf4/include/RE/IDs.h --keep
   dist/Data/F4SE/Plugins/Vitals/addresses/commonlibf4.txt > new.txt`, check the diff, replace the file, then fill expected bytes for
   the new build with the Chart's `addrcheck.py --fill` against your own exe. **Never commit the exe or bytes
   taken from an encrypted exe**; bytes in the list are short signatures, baselines stay local.
5. Run the test matrix. Release.

## When F4SE changes how it loads plugins

The rules live in two places that must agree: `Loader::Check` in `include/Vitals/Rules.h` and `verdict()` in
`tools/f4se_dll_scan.py`. Read the new F4SE's `PluginManager.cpp` (`CheckPluginCompatibility`; it ships with source),
update both, and add lines to `tests/loader_cases.txt`: CI runs every line through both and fails if either disagrees.
If there is a new reason, give it a sentence in `Explain` (`src/Doctor.cpp`).

## Updating the library pin

`lib/COMMONLIBF4_COMMIT` holds the commit. Change it on purpose only:
1. Read the library's commits since the pin for changes to `REL::ID`, `IDDB`, the plugin template, REX::W32. Vitals
   mirrors two IDDB details that must stay in step: the Address Library file name and format (`Formats::AddressLib`)
   and the list of blocked files (`BlockedHash` in `src/Common.cpp`, from `IDDB::validate_file`).
2. Update the commit, build in CI, run the full test matrix in game on 1.11.240 and 1.10.163.
3. Note the new pin in `CHANGELOG.md`.

## Adding doctor rules

Census findings are the main source. They live in the defib-census repo (`rules/*.txt`) and reach Vitals as a
generated file: run `python scripts/census.py export` there and copy the result over
`dist/Data/F4SE/Plugins/Vitals/knowledge/defib-census.txt` (never edit that file by hand; the export writes a
`# Generated:` date that decides which file wins when two hold the same rule). Tag each rule with the game versions it was
confirmed on or is true for (`dll@AE`, `dll@NG,AE`, `oldfile@NG,AE`), in the census rules file: a rule with no tag shows
on every version, including ones where its advice is wrong. Anything that is not a census finding goes in `defib-known.txt`. Either way: only something confirmed in game, with a comment giving the
date, the build and what was seen. Format: [FILE_FORMATS.md](FILE_FORMATS.md). Prefer specific rules (a file name,
a date) over broad ones. Use `problem` only for what may crash the game: it always opens the start-up box and
"Don't show" cannot silence it, so a false problem costs trust. `tests/test_tools.py` checks that every shipped
rule parses.

## Test matrix (before every release)

| Test | 1.11.240 (AE) | 1.10.163 (OG, MO2) | 1.10.984 (NG) |
| --- | --- | --- | --- |
| Loads; report written; matches f4se.log | required | required | when available |
| Doctor: a known-refused DLL explained correctly | required | required | |
| Doctor: an inactive master reported (temporarily untick a master) | required | | |
| Message box opens on every launch while something is listed (`iPopupMode=1`); after Don't show it lists only what is still not silenced; with nothing to list there is no box and the report says ALL CLEAR | required | | |
| Message box buttons: Continue / Don't show (lists plugins in popup_disabled.txt; a crash risk still shows) / Copy report (puts the report on the clipboard, the box stays open) / Stop game (ends the game) | required | | |
| INI warning: appears with loose files and no INI settings, absent on a correct setup | required | | |
| Address check: no unexpected `differs` on a clean F4SE + Vitals launch | required | required | |
| Save load: pointer re-check, no crash | required | required | |
| Report top: the "Last launch" recap and "CHANGED SINCE LAST LAUNCH" match the previous launch (replace or remove one DLL between two launches and check it is listed) | required | | |
| Share pack (Yes in the installer): `Vitals-Share.zip` is started right after the report is written, before the box, and is finished while the box is open (a box left open for a while can still be racing the write), written again at the main menu, nothing personal in it (open it and search for your user name, PC name, save names); No thanks writes no pack, and removes an old one | required | | |
| Mod manager evidence: Vortex (`Installed by`, newer-on-Nexus line) and MO2 (instance chosen, winner from `modlist.txt`, Overwrite advice) | required (Vortex) | required (MO2) | |
| Unit tests (`python -m unittest discover -s tests`, includes the C++ rules test) | CI | | |
| FOMOD installs in Vortex and MO2, both options | required | | |

Keep the results (and what was **not** tested) in your own notes or the release thread. Do not write "tested" or "not tested" lines in `CHANGELOG.md`: they go stale and contradict each other.

## Releasing

1. Version in `xmake.lua` (`plugin_version`). `fomod/info.xml` is set from it at packaging time.
2. `CHANGELOG.md`: a `## <version>` section; the release notes are cut from it.
3. Merge to `main`, then run the `build` workflow manually with `release_version` (or push tag `v<version>`
   from a PC). CI builds, checks the FOMOD, and publishes `Vitals-<version>-AI-Assisted.zip` (the main file) and
   `Vitals-<version>-Extra-Docs.zip` (the optional file: the guides and changelog that are not in the main zip).
4. Nexus: upload the first zip as the main file and the second as an optional file named "Vitals Extra Docs"
   (category Miscellaneous or Documentation), keep the "AI Assisted" tag and the disclosure on the page, and paste the
   changelog section into the file's changelog. The Nexus description is kept outside the repository.

## Rules that keep the project healthy

* Never upload or redistribute other authors' files (DLLs, `f4rd-runtime.bin`, game executables).
* Baselines and anything copied from the game are local only.
* Secrets (Nexus API key) live only in the environment, never in the repo or chat.
* Every release is tagged AI Assisted.

## Share packs you collected

A pack is deleted 90 days after it arrived, or sooner when the sender asks (the privacy page promises this). Nothing in the
release process does it for you, so on each release: list the files in your `inbox` folder, delete every pack older than
90 days, and delete the derived files you made from them that still hold a sender's data. Only the counts (five or more
installs) are ever published. A person opens a pack when one has to be read; an AI assistant is given only `summary.md`
(counts and file names), never a raw pack.
