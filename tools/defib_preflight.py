#!/usr/bin/env python3
"""Defib preflight: check a Fallout 4 install WITHOUT starting the game.

The offline twin of Vitals' in-game plugin doctor, for the census and for anyone who wants an answer before
launching. Standard-library Python 3.9+, read-only.

Checks:
  * F4SE DLLs in Data\\F4SE\\Plugins: will F4SE load them on this game build, and if not, why (same rules as
    Vitals and tools/f4se_dll_scan.py).
  * Load order (plugins.txt): masters missing, installed but inactive, or loading after the plugin that needs them;
    active plugins whose file is gone; full/light counts.
  * Loose files in Data\\Interface: which mod deployed each one (Vortex's vortex.deployment.json) and its date.
    Old menu files were the cause of a main-menu crash on 1.11.240.
  * The knowledge rules shipped with Vitals (Data\\F4SE\\Plugins\\Vitals\\knowledge\\*.txt, or --rules).

Usage:
  python defib_preflight.py "C:\\...\\steamapps\\common\\Fallout 4" [--plugins-txt PATH] [--runtime 1.11.240] [--json] [--out report.md]
plugins.txt defaults to %LOCALAPPDATA%\\Fallout4\\plugins.txt. For MO2 pass the profile's plugins.txt.
"""
import argparse
import datetime as dt
import glob
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import f4se_dll_scan  # noqa: E402

BASE_MASTERS = ["Fallout4.esm", "DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCworkshop02.esm",
                "DLCworkshop03.esm", "DLCNukaWorld.esm", "DLCUltraHighResolution.esm"]
RUNTIMES = {"1.10.163": ((1, 10, 163), "OG"), "1.10.984": ((1, 10, 984), "NG"), "1.11.240": ((1, 11, 240), "AE")}


def issue(sev, cat, subject, message, advice=""):
    return {"severity": sev, "category": cat, "subject": subject, "message": message, "advice": advice}


def find_ci(folder, name):
    """Case-insensitive file lookup (the game folder is case-insensitive on Windows)."""
    p = os.path.join(folder, name)
    if os.path.exists(p):
        return p
    low = name.lower()
    try:
        for f in os.listdir(folder):
            if f.lower() == low:
                return os.path.join(folder, f)
    except OSError:
        pass
    return None


def game_runtime(game):
    """Best guess at the game build: the Address Library file present, else None."""
    plugins = os.path.join(game, "Data", "F4SE", "Plugins")
    for label in ("1.11.240", "1.10.984", "1.10.163"):
        if find_ci(plugins, f"version-{label.replace('.', '-')}-0.bin"):
            return label
    return None


# ------------------------------------------------------------------ plugins and masters
def read_header(path):
    with open(path, "rb") as f:
        d = f.read(1 << 20)
    if len(d) < 24 or d[:4] != b"TES4":
        return None
    size, flags = struct.unpack_from("<II", d, 4)
    o, end, masters = 24, min(len(d), 24 + size), []
    while o + 6 <= end:
        typ = d[o:o + 4]
        ln = struct.unpack_from("<H", d, o + 4)[0]
        if typ == b"MAST":
            masters.append(plugin_name(d[o + 6:o + 6 + ln].split(b"\0")[0]))
        o += 6 + ln
    ext = os.path.splitext(path)[1].lower()
    return {"masters": masters, "light": bool(flags & 0x200) or ext == ".esl", "master": bool(flags & 1) or ext in (".esm", ".esl")}


def plugin_name(raw):
    """A plugin name from a file: valid UTF-8 is kept, anything else is Windows-1252 (Rules::PluginNameToUtf8)."""
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1252", "replace")


def actual_load_order(n_implicit, is_master):
    """Indices in the order the game really loads plugins (Rules::ActualLoadOrder): the implicit files first, then
    master-flagged plugins (ESM flag, .esm, .esl) in plugins.txt order, then every other plugin."""
    rest = range(n_implicit, len(is_master))
    return list(range(min(n_implicit, len(is_master)))) + [i for i in rest if is_master[i]] + [i for i in rest if not is_master[i]]


def read_names(path):
    """Lines of plugins.txt / Fallout4.ccc. Valid UTF-8 is kept; anything else is Windows-1252 (as the game writes it)."""
    with open(path, "rb") as f:
        raw = f.read()
    try:
        text = raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        text = raw.decode("cp1252", "replace")
    return [ln.strip() for ln in text.splitlines()]


def load_order(game, plugins_txt, with_implicit_count=False):
    data = os.path.join(game, "Data")
    order = [m for m in BASE_MASTERS if find_ci(data, m)]
    ccc = find_ci(game, "Fallout4.ccc")
    if ccc:
        for ln in read_names(ccc):
            if ln and find_ci(data, ln):
                order.append(ln)
    implicit = {o.lower() for o in order}
    n_implicit = len(order)
    for ln in read_names(plugins_txt):
        if ln.startswith("*") and ln[1:].lower() not in implicit:
            order.append(ln[1:])
    return (order, n_implicit) if with_implicit_count else order


FULL_SLOTS, LIGHT_SLOTS = 254, 4096  # full plugins: load indexes 00-FD (base game, DLCs and CC included); FE: light


def check_load_order(game, plugins_txt):
    out, info = [], []
    if not plugins_txt or not os.path.exists(plugins_txt):
        return out, ["plugins.txt not found; load-order checks skipped"]
    data = os.path.join(game, "Data")
    order, n_implicit = load_order(game, plugins_txt, with_implicit_count=True)
    headers, is_master, missing = [], [], 0
    for name in order:
        path = find_ci(data, name)
        h = read_header(path) if path else None
        if not path:
            missing += 1
            out.append(issue("warning", "load_order", name, f"{name} is active in plugins.txt but the file is not in Data.",
                             "Re-deploy your mod manager, or deactivate it."))
        elif not h:
            out.append(issue("warning", "load_order", name, f"{name} does not start with a valid plugin header (TES4)."))
        headers.append((path, h))
        is_master.append(h["master"] if h else os.path.splitext(name)[1].lower() in (".esm", ".esl"))
    actual = actual_load_order(n_implicit, is_master)
    pos = {order[i].lower(): p for p, i in enumerate(actual) if headers[i][0]}
    full = light = 0
    for p, i in enumerate(actual):
        name, (path, h) = order[i], headers[i]
        if not h:
            continue
        if h["light"]:
            light += 1
        else:
            full += 1
        for m in h["masters"]:
            j = pos.get(m.lower())
            if j is None:
                present = find_ci(data, m) is not None
                out.append(issue("problem", "load_order", name,
                                 f"{name} needs the master {m}, which is {'installed but NOT active' if present else 'NOT installed'}.",
                                 f"{'Activate' if present else 'Install'} {m}, or deactivate {name}."))
            elif j > p:
                mi = actual[j]
                if is_master[i] and not is_master[mi] and mi < i:
                    msg = (f"{name} loads before its master {m}: {name} has the master (ESM) flag, so the game loads it before"
                           f" every normal plugin, including {m}.")
                    adv = f"A master-flagged plugin cannot depend on a normal plugin. Deactivate {name}, or get a version that does not need {m}."
                else:
                    msg, adv = f"{name} loads before its master {m}.", "Sort the load order (LOOT) so masters load first."
                out.append(issue("problem", "load_order", name, msg, adv + " Like an inactive master, this typically crashes the game while plugins load."))
    info.append(f"Load order: {len(order) - missing} active plugins ({full} full, {light} light)")
    if full > FULL_SLOTS:
        out.append(issue("problem", "load_order", "plugin count",
                         f"{full} full (non-light) plugins are active, counting Fallout4.esm, the DLCs and Creation Club files;"
                         f" the game has {FULL_SLOTS} full-plugin slots."))
    if light > LIGHT_SLOTS:
        out.append(issue("problem", "load_order", "light plugin count", f"{light} light plugins are active; the limit is {LIGHT_SLOTS}."))
    return out, info


# ------------------------------------------------------------------ DLLs
NEXT_GEN = dt.date(2024, 4, 25)  # the update that introduced F4SEPlugin_Version


def age_note(path):
    """Second check for 'outdated': when was the DLL built (COFF timestamp), else the file date, vs the Next-Gen update."""
    try:
        with open(path, "rb") as f:
            d = f.read(4096)
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        ts = struct.unpack_from("<I", d, pe + 8)[0]
    except (OSError, struct.error):
        return ""
    weak = False
    built = None
    if ts:
        try:
            built = dt.datetime.fromtimestamp(ts, dt.timezone.utc).date()
        except (OverflowError, OSError, ValueError):
            built = None
    if not built or built < dt.date(2010, 1, 1) or built > dt.date.today() + dt.timedelta(days=1):
        built = dt.date.fromtimestamp(os.path.getmtime(path))
        weak = True
    what = "Its file is dated" if weak else "Built"
    if built < NEXT_GEN:
        return f" {what} {built}, before the April 2024 Next-Gen update, so it {'was probably' if weak else 'was'} made for Old-Gen 1.10.163 or earlier and is outdated."
    return f" {what} {built}, after the Next-Gen update, so it may still be maintained and only lack the new version data: check the mod page for a newer file."


ADVICE = {
    "og_no_query": "This plugin is for Next-Gen/Anniversary only. Look for an Old-Gen (1.10.163) version of the mod.",
    "needs_newer_f4se": "Update F4SE to the version this plugin asks for.",
    "address_library_missing": "Install the Address Library file for your exact game version (Nexus 47327).",
    "ng_flags_only": "Look for an Anniversary (1.11.x) build of this mod, or a port.",
    "other_builds": "Look for a version of this mod for your game version.",
}
DEFAULT_ADVICE = "Look for a version of this mod for your game version, or a replacement. Until then its features (and mods that need it) will not work."
POST_NG_ADVICE = "Check the mod page for a newer file: the author may have released one with Next-Gen/Anniversary support."


def check_dlls(game, runtime):
    """F4SE DLLs that will not load. These are warnings: a plugin that does not load is a mod that does not run,
    not a crash. Crash risks (masters, Address Library) are separate problems."""
    out, info = [], []
    plugins = os.path.join(game, "Data", "F4SE", "Plugins")
    if not os.path.isdir(plugins):
        return out, ["No Data\\F4SE\\Plugins folder"]
    rt, era = RUNTIMES[runtime]
    al_present = os.path.exists(os.path.join(plugins, f"version-{runtime.replace('.', '-')}-0.bin"))
    dlls = sorted(glob.glob(os.path.join(plugins, "*.dll")) + glob.glob(os.path.join(plugins, "*.DLL")))
    bad = 0
    for path in dict.fromkeys(dlls):
        name = os.path.basename(path)
        try:
            r = f4se_dll_scan.scan_file(path)
        except Exception:  # truncated or not a PE file at all
            out.append(issue("warning", "plugin_dll", name, f"{name} could not be read as a Windows DLL. It is damaged or not a DLL.",
                             "Reinstall the mod that provides it, or remove it."))
            continue
        if "error" in r:
            out.append(issue("info", "plugin_dll", name, f"{name}: {r['error']}"))
            continue
        status, reason, why = f4se_dll_scan.verdict(rt, era, "F4SEPlugin_Query" in r["f4seExports"], r.get("versionData"), al_present=al_present)
        if status != "no":
            continue
        bad += 1
        advice = ADVICE.get(why, DEFAULT_ADVICE)
        if why in ("no_version_data", "no_version_data_query_only"):
            note = age_note(path)
            reason += "." + note.rstrip(".")
            if "after the Next-Gen update" in note:
                advice = POST_NG_ADVICE
        out.append(issue("warning", "plugin_dll", name, f"{name} will not load on {runtime}: {reason}.", advice))
    info.append(f"F4SE plugins: {len(set(dlls))} DLLs, {bad} predicted not to load on {runtime}")
    return out, info


# ------------------------------------------------------------------ loose interface files
def vortex_owners(game):
    path = os.path.join(game, "Data", "vortex.deployment.json")
    owners = {}
    if os.path.exists(path):
        try:
            d = json.load(open(path, encoding="utf-8"))
            for f in d.get("files", []):
                owners[f["relPath"].replace("/", "\\").lower()] = f.get("source", "?")
        except (OSError, ValueError, KeyError):
            pass
    return owners


def check_interface(game):
    out = []
    folder = os.path.join(game, "Data", "Interface")
    if not os.path.isdir(folder):
        return out
    owners = vortex_owners(game)
    for f in sorted(os.listdir(folder)):
        if not f.lower().endswith((".swf", ".txt")) or not os.path.isfile(os.path.join(folder, f)):
            continue
        when = dt.datetime.fromtimestamp(os.path.getmtime(os.path.join(folder, f))).date()
        owner = owners.get(f"interface\\{f}".lower(), "not deployed by Vortex (manual install or another manager)")
        out.append(issue("info", "loose_file", f"Data\\Interface\\{f}", f"Data\\Interface\\{f} ({when}) from: {owner}"))
    return out


def ini_value(path, section, key):
    """Value of key under [section] in an INI file (case-insensitive), or None."""
    try:
        raw = open(path, "rb").read()
    except OSError:
        return None
    # The game's own INI files are UTF-16 (with a BOM); others are ANSI or UTF-8. GetPrivateProfileString, which the
    # game uses, reads all of them.
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = raw.decode("utf-16", "replace")
    else:
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("cp1252", "replace")
    inside = False
    for ln in text.splitlines():
        t = ln.strip()
        if not t or t[0] in ";#":
            continue
        if t.startswith("["):
            inside = t.lower() == f"[{section}]".lower()
        elif inside and "=" in t:
            k, v = t.split("=", 1)
            if k.strip().lower() == key.lower():
                return v.strip()
    return None


def ini_int(value):
    """The leading integer of an INI value, as the game reads it ("1 ; set by BethINI" is 1), or None."""
    m = re.match(r"\s*([+-]?\d+)", value or "")
    return int(m.group(1)) if m else None


LOOSE_ASSET_DIRS = ("Meshes", "Textures", "Materials", "Sound", "Music", "Interface", "Vis", "LODSettings")


def loose_assets_installed(data):
    """At least one real file in an asset folder (empty folders a mod manager left do not count). Scripts are left
    out: the game reads loose .pex files either way. Same as the plugin's LooseAssetsInstalled."""
    visited = 0
    for d in LOOSE_ASSET_DIRS:
        root = find_ci(data, d)
        if not root or not os.path.isdir(root):
            continue
        for _dirpath, _dirs, files in os.walk(root):
            visited += 1 + len(files)
            if files or visited > 50000:
                return True
    return False


def check_loose_setup(game, my_games):
    """Loose meshes/textures/interface files need bInvalidateOlderFiles=1 and an empty sResourceDataDirsFinal."""
    data = os.path.join(game, "Data")
    if not loose_assets_installed(data) or not my_games:
        return []

    def effective(key):
        for name in ("Fallout4Custom.ini", "Fallout4Prefs.ini", "Fallout4.ini"):  # Custom wins, then Prefs
            v = ini_value(os.path.join(my_games, name), "Archive", key)
            if v is not None:
                return v
        return None

    missing = []
    if ini_int(effective("bInvalidateOlderFiles")) != 1:
        missing.append("bInvalidateOlderFiles=1")
    dirs = effective("sResourceDataDirsFinal")
    if dirs is None or dirs.split(";", 1)[0].strip() != "":
        missing.append("an empty sResourceDataDirsFinal=")
    if not missing:
        return []
    return [issue("warning", "ini_setup", "Fallout4Custom.ini",
                  "Loose mod files are installed, but the game's INI settings do not turn on loose files: missing "
                  + " and ".join(missing) + " under [Archive] in Fallout4Custom.ini. The game will silently ignore those files."
                  " (Mod Organizer 2 profiles with their own INI files: check the profile's Fallout4Custom.ini instead.)",
                  "Add [Archive] bInvalidateOlderFiles=1 and sResourceDataDirsFinal= to Fallout4Custom.ini.")]


# ------------------------------------------------------------------ knowledge rules
def load_rules(paths):
    """Rules from every file; the same rule (kind, match, extra) in two files is used once, as in game."""
    rules, seen = [], set()
    for p in sorted(paths):
        for ln in open(p, encoding="utf-8", errors="replace"):
            ln = ln.strip()
            if not ln or ln[0] in "#;":
                continue
            f = [x.strip() for x in ln.split("|")]
            key = (f[0].lower(), f[1].lower(), f[2].lower()) if len(f) >= 5 else None
            if key and key not in seen:
                seen.add(key)
                rules.append({"kind": f[0].lower(), "match": f[1], "extra": f[2], "severity": f[3].lower(),
                              "message": " | ".join(f[4:])})
    return rules


def enb_installed(game):
    return any(find_ci(game, n) for n in ("enbseries.ini", "enblocal.ini")) and any(find_ci(game, n) for n in ("d3d11.dll", "dxgi.dll"))


def exists(game, rel, runtime=None):
    if rel == "{ENB}":
        return enb_installed(game)
    if rel == "{ADDRESS_LIBRARY}":
        return runtime is not None and os.path.exists(os.path.join(game, "Data", "F4SE", "Plugins", f"version-{runtime.replace('.', '-')}-0.bin"))
    return os.path.exists(os.path.join(game, *rel.split("\\")))


def check_rules(game, rules, runtime, dll_issues):
    out = []
    plugins = os.path.join(game, "Data", "F4SE", "Plugins")
    refused = {i["subject"].lower() for i in dll_issues if i["category"] == "plugin_dll"}  # will not run, for any reason
    blobs = {}
    for r in rules:
        k = r["kind"]
        if k == "needs":
            users = []
            for p in glob.glob(os.path.join(plugins, "*.dll")):
                if p not in blobs:
                    blobs[p] = open(p, "rb").read().lower()
                if r["match"].lower().encode() in blobs[p]:
                    users.append(os.path.basename(p))
            if users and not exists(game, r["extra"], runtime):
                out.append(issue(r["severity"], "data_file", r["extra"], f"{r['extra']} is missing, and {', '.join(users)} need it. {r['message']}"))
        elif k == "dll":
            p = find_ci(plugins, r["match"])
            if p and (r["extra"] != "refused" or r["match"].lower() in refused):
                out.append(issue(r["severity"], "known_issue", r["match"], r["message"]))
        elif k == "conflict":
            if exists(game, r["match"], runtime) and exists(game, r["extra"], runtime):
                out.append(issue(r["severity"], "conflict", f"{r['match']} + {r['extra']}", r["message"]))
        elif k == "oldfile":
            p = os.path.join(game, *r["match"].split("\\"))
            try:
                cutoff = dt.date.fromisoformat(r["extra"])
            except ValueError:
                continue  # a malformed date: skip the rule, as Vitals does in game
            if os.path.exists(p):
                when = dt.date.fromtimestamp(os.path.getmtime(p))
                if when < cutoff:
                    out.append(issue(r["severity"], "loose_file", r["match"], f"{r['match']} is dated {when}, older than {r['extra']}. {r['message']}"))
        elif k == "file":
            if exists(game, r["match"], runtime):
                out.append(issue(r["severity"], "known_issue", r["match"], r["message"]))
    return out


def run(game, plugins_txt=None, runtime=None, rules_paths=None, my_games=None):
    runtime = runtime or game_runtime(game) or "1.11.240"
    if plugins_txt is None and os.environ.get("LOCALAPPDATA"):
        plugins_txt = os.path.join(os.environ["LOCALAPPDATA"], "Fallout4", "plugins.txt")
    if my_games is None and os.environ.get("USERPROFILE"):
        my_games = os.path.join(os.environ["USERPROFILE"], "Documents", "My Games", "Fallout4")
    if rules_paths is None:
        rules_paths = glob.glob(os.path.join(game, "Data", "F4SE", "Plugins", "Vitals", "knowledge", "*.txt"))
        if not rules_paths:
            here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
            rules_paths = glob.glob(os.path.join(here, "dist", "Data", "F4SE", "Plugins", "Vitals", "knowledge", "*.txt"))
    dll_issues, info1 = check_dlls(game, runtime)
    lo_issues, info2 = check_load_order(game, plugins_txt)
    issues = dll_issues + lo_issues + check_rules(game, load_rules(rules_paths), runtime, dll_issues) + check_interface(game) + check_loose_setup(game, my_games)
    return {"runtime": runtime, "info": info1 + info2, "issues": issues}


def to_markdown(res):
    L = [f"# Defib preflight ({res['runtime']})", ""]
    L += [f"- {i}" for i in res["info"]]
    for sev, title in (("problem", "Problems"), ("warning", "Warnings"), ("info", "Notes")):
        items = [i for i in res["issues"] if i["severity"] == sev]
        if items:
            L += ["", f"## {title}"]
            for i in items:
                L.append(f"- {i['message']}")
                if i["advice"]:
                    L.append(f"  - what to do: {i['advice']}")
    return "\n".join(L) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game", help="game folder (the one with Fallout4.exe)")
    ap.add_argument("--plugins-txt", help="plugins.txt to use (default %%LOCALAPPDATA%%\\Fallout4\\plugins.txt)")
    ap.add_argument("--runtime", choices=sorted(RUNTIMES), help="game build to check against (default: from the Address Library file)")
    ap.add_argument("--my-games", help="Documents\\My Games\\Fallout4 folder (default: from USERPROFILE) for the INI check")
    ap.add_argument("--rules", nargs="*", help="knowledge rule files (default: the ones installed with Vitals)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of Markdown")
    ap.add_argument("--out", help="write the report here")
    a = ap.parse_args(argv)
    res = run(a.game, a.plugins_txt, a.runtime, a.rules, a.my_games)
    text = json.dumps(res, indent=1) if a.json else to_markdown(res)
    if a.out:
        open(a.out, "w", encoding="utf-8").write(text)
        print(f"wrote {a.out}")
    else:
        print(text)
    return 1 if any(i["severity"] == "problem" for i in res["issues"]) else 0


if __name__ == "__main__":
    sys.exit(main())
