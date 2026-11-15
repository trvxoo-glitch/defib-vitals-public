#!/usr/bin/env python3
"""Build the Vitals release folder (FOMOD layout) and check it.

  python tools/make_package.py build --dll build/windows/x64/release/Vitals.dll [--out package]
  python tools/make_package.py check [package]

Layout of the release archive (the zip CI publishes is this folder's contents):
  fomod/info.xml, fomod/ModuleConfig.xml      installer read by Vortex, MO2 and other managers
  00 Core/F4SE/Plugins/Vitals.dll             always installed
  00 Core/F4SE/Plugins/Vitals/addresses/      address lists
  00 Core/F4SE/Plugins/Vitals/knowledge/      plugin doctor rules
  10 Settings - Player/F4SE/Plugins/Vitals.ini       default settings (dist/.../Vitals.ini as is)
  11 Settings - Developer/F4SE/Plugins/Vitals.ini    same file with the developer options on
  20 Share - Yes/F4SE/Plugins/Vitals/share-optin.txt  share pack marker, "share=1" (installer page 2, not the default)
  21 Share - No/F4SE/Plugins/Vitals/share-optin.txt   the same file with "share=0" (the default), so a reinstall that
                                                       picks No replaces an earlier Yes even when files are merged
  Docs/                                        README, user guide, troubleshooting, changelog, licences
Manual install: copy the contents of "00 Core" and of ONE settings folder into the game's Data folder.

`check` fails when a folder or file named in ModuleConfig.xml is missing, when the two info.xml versions
disagree with xmake.lua, when a settings folder has no Vitals.ini, or when the share-pack marker is wrong: "share=1"
anywhere but "20 Share - Yes", a marker anywhere but the two share folders, or "No thanks" not the pre-selected
option (sharing must never be on without the player choosing it). Standard library only.
"""
import argparse
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUG = os.path.join("F4SE", "Plugins")
DEVELOPER = {"bPapyrusNatives": "1", "bLayoutProbes": "1"}
SHARE_FOLDER = "20 Share - Yes"
SHARE_NO_FOLDER = "21 Share - No"
SHARE_MARKER = "share-optin.txt"
# Vitals reads the share= line: only "share=1" turns the pack on (src/Share.cpp SharePackEnabled).
SHARE_TEXT = """share=1
Vitals share pack: ON. You chose this in the installer ("Help improve Vitals").
Vitals writes Vitals-Share.zip next to Vitals-Report.txt (Documents\\My Games\\Fallout4\\F4SE\\Vitals) each
session, with personal details scrubbed out. Vitals never sends it: you decide whether to share it.
To turn it off, change the first line to share=0, delete this file, or set bSharePack=0 in Vitals.ini.
Vitals then deletes the pack at the next launch.
"""
SHARE_NO_TEXT = """share=0
Vitals share pack: OFF. You chose "No thanks" in the installer. Vitals keeps everything on your PC and makes no pack.
"""
DOCS = ["README.md", "CHANGELOG.md", "AI_DISCLOSURE.md", "LICENSE", "EXCEPTIONS",
        "docs/USER_GUIDE.md", "docs/TROUBLESHOOTING.md", "docs/FAQ.md", "docs/PRIVACY_AND_SAFETY.md", "docs/CONTRIBUTING.md",
        "docs/FILE_FORMATS.md", "docs/ARCHITECTURE.md", "docs/BUILDING.md", "docs/MAINTAINING.md", "docs/HISTORY.md", "docs/ROADMAP.md"]


def version():
    m = re.search(r'plugin_version\s*=\s*"([\d.]+)"', open(os.path.join(ROOT, "xmake.lua"), encoding="utf-8").read())
    return m.group(1)


def developer_ini(text):
    for key, val in DEVELOPER.items():
        text, n = re.subn(rf"^{key}=.*$", f"{key}={val}", text, flags=re.M)
        if n != 1:
            raise SystemExit(f"Vitals.ini has no single {key}= line")
    return text.replace("; Vitals settings.", "; Vitals settings (DEVELOPER preset: Papyrus natives and layout probes on).", 1)


def safe_output_dir(out):
    """The package folder is deleted and rebuilt, so refuse anything that is not a package folder: the repo, a parent
    of it, a drive root or home, or an existing folder that does not look like an earlier package."""
    real = os.path.realpath(out)
    home = os.path.realpath(os.path.expanduser("~"))
    root = os.path.realpath(ROOT)
    if real in (os.path.dirname(real), home) or root == real or root.startswith(real + os.sep):
        raise SystemExit(f"FOMOD ERROR: refusing to use {out} as the package folder (it would delete {real})")
    if os.path.isdir(real) and os.listdir(real) and not os.path.isdir(os.path.join(real, "fomod")):
        raise SystemExit(f"FOMOD ERROR: {out} exists and is not an earlier package (no fomod folder); pick an empty or new folder")
    return real


def build(dll, out):
    out = safe_output_dir(out)
    if os.path.exists(out):
        shutil.rmtree(out)
    src = os.path.join(ROOT, "dist", "Data", PLUG)
    core = os.path.join(out, "00 Core", PLUG)
    shutil.copytree(os.path.join(src, "Vitals"), os.path.join(core, "Vitals"))
    shutil.copy2(dll, core)
    ini = open(os.path.join(src, "Vitals.ini"), encoding="utf-8").read()
    for folder, text in (("10 Settings - Player", ini), ("11 Settings - Developer", developer_ini(ini))):
        d = os.path.join(out, folder, PLUG)
        os.makedirs(d)
        with open(os.path.join(d, "Vitals.ini"), "w", encoding="utf-8", newline="\r\n") as f:
            f.write(text)
    for folder, text in ((SHARE_FOLDER, SHARE_TEXT), (SHARE_NO_FOLDER, SHARE_NO_TEXT)):
        share = os.path.join(out, folder, PLUG, "Vitals")
        os.makedirs(share)
        with open(os.path.join(share, SHARE_MARKER), "w", encoding="utf-8", newline="\r\n") as f:
            f.write(text)
    os.makedirs(os.path.join(out, "fomod"))
    v = version()
    info = open(os.path.join(ROOT, "fomod", "info.xml"), encoding="utf-8").read()
    info = re.sub(r'<Version MachineVersion="[^"]*">[^<]*</Version>', f'<Version MachineVersion="{v}">{v}</Version>', info)
    open(os.path.join(out, "fomod", "info.xml"), "w", encoding="utf-8").write(info)
    shutil.copy2(os.path.join(ROOT, "fomod", "ModuleConfig.xml"), os.path.join(out, "fomod"))
    os.makedirs(os.path.join(out, "Docs"))
    for d in DOCS:
        p = os.path.join(ROOT, d)
        if not os.path.exists(p):
            raise SystemExit(f"FOMOD ERROR: documentation file {d} is missing (listed in DOCS)")
        dest = os.path.join(out, "Docs", *d.split("/"))  # keep docs/ so the README's relative links work
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        shutil.copy2(p, dest)
    return check(out)


def check(pkg):
    errors = []
    cfg = os.path.join(pkg, "fomod", "ModuleConfig.xml")
    info = os.path.join(pkg, "fomod", "info.xml")
    for p in (cfg, info):
        if not os.path.exists(p):
            errors.append(f"missing {p}")
    if errors:
        return report(errors)
    tree = ET.parse(cfg)
    sources = [(e.tag, e.get("source")) for e in tree.iter() if e.tag in ("file", "folder")]
    for tag, s in sources:
        p = os.path.join(pkg, *s.replace("\\", "/").split("/"))
        if not (os.path.isdir(p) if tag == "folder" else os.path.isfile(p)):
            errors.append(f"ModuleConfig.xml names {tag} '{s}', which is not in the package")
        elif s.startswith("1") and not os.path.isfile(os.path.join(p, PLUG, "Vitals.ini")):
            errors.append(f"settings folder '{s}' has no {PLUG}\\Vitals.ini")
    if not os.path.isfile(os.path.join(pkg, "00 Core", PLUG, "Vitals.dll")):
        errors.append("00 Core has no Vitals.dll")
    errors += check_share(pkg, tree)
    m = ET.parse(info).getroot().find("Version")
    v = version()
    if m is None or m.text != v or m.get("MachineVersion") != v:
        errors.append(f"fomod/info.xml version is not {v} (xmake.lua)")
    return report(errors, len(sources))


def share_on(path):
    """What Vitals reads: on only with a "share=1" line (src/Share.cpp SharePackEnabled)."""
    for ln in open(path, encoding="utf-8", errors="replace").read().splitlines():
        if ln.strip().lower().replace(" ", "") == "share=1":
            return True
    return False


def check_share(pkg, tree):
    """Sharing must never be on unless the player picked Yes: the share=1 marker only in the Yes folder, the share=0
    marker in the No folder, No pre-selected and listed first, Yes never pre-selected."""
    errors = []
    allowed = {f"{SHARE_FOLDER}/F4SE/Plugins/Vitals/{SHARE_MARKER}", f"{SHARE_NO_FOLDER}/F4SE/Plugins/Vitals/{SHARE_MARKER}"}
    for dirpath, _dirs, files in os.walk(pkg):
        for f in files:
            if f.lower() == SHARE_MARKER:
                rel = os.path.relpath(os.path.join(dirpath, f), pkg).replace(os.sep, "/")
                if rel not in allowed:
                    errors.append(f"share-pack marker found outside the two share folders: {rel}")
    yes = os.path.join(pkg, SHARE_FOLDER, PLUG, "Vitals", SHARE_MARKER)
    no = os.path.join(pkg, SHARE_NO_FOLDER, PLUG, "Vitals", SHARE_MARKER)
    if not os.path.isfile(yes) or not share_on(yes):
        errors.append(f"{SHARE_FOLDER} has no {SHARE_MARKER} with share=1")
    if not os.path.isfile(no) or share_on(no):
        errors.append(f"{SHARE_NO_FOLDER} has no {SHARE_MARKER}, or it says share=1")
    group = None
    for g in tree.iter("group"):
        if any(fd.get("source") in (SHARE_FOLDER, SHARE_NO_FOLDER) for fd in g.iter("folder")):
            group = g
    if group is None:
        return errors + ["ModuleConfig.xml has no share-pack group"]
    plugins = list(group.iter("plugin"))

    def kind(p):
        t = p.find("typeDescriptor/type")
        return t.get("name") if t is not None else "unset"

    def installs(p, folder):
        return any(fd.get("source") == folder for fd in p.iter("folder"))

    if not plugins or not installs(plugins[0], SHARE_NO_FOLDER) or kind(plugins[0]) != "Recommended":
        errors.append(f"the first share-pack option must install '{SHARE_NO_FOLDER}' and be Recommended (pre-selected)")
    for p in plugins:
        if installs(p, SHARE_FOLDER) and kind(p) != "Optional":
            errors.append(f"the share-pack Yes option must be Optional (never pre-selected), not {kind(p)}")
    return errors


def report(errors, n=0):
    for e in errors:
        print("FOMOD ERROR:", e)
    if not errors:
        print(f"FOMOD OK: {n} source paths present, version {version()}")
    return 1 if errors else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("--dll", required=True)
    b.add_argument("--out", default="package")
    c = sub.add_parser("check")
    c.add_argument("package", nargs="?", default="package")
    a = ap.parse_args(argv)
    return build(a.dll, a.out) if a.cmd == "build" else check(a.package)


if __name__ == "__main__":
    sys.exit(main())
