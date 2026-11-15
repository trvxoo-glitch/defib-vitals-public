#!/usr/bin/env python3
"""Vitals share-pack inbox: download packs players sent, and turn many packs into one summary of candidate rules.

  python tools/share_inbox.py pull --out inbox          download new packs from the upload server
        (needs VITALS_SHARE_URL and VITALS_READ_KEY in the environment; see server/README.md)
  python tools/share_inbox.py summary inbox [--out summary.md] [--min-installs 5]

`summary` reads every *.zip in the folder (packs from the server, or ones players attached to a GitHub issue) and
counts, per distinct install id (a random id, one per Vitals install; it says nothing about the player):
  * F4SE DLLs that are not running, with Vitals' loader reason, game build and the mod folder that installed them
  * crash locations (module+offset of the unhandled exception) and the modules on the probable call stack
  * Papyrus unbound native functions (which F4SE plugin a script wanted)
  * doctor findings by category
Candidates are DLLs not covered by a 'dll' rule in Vitals' knowledge files, seen in at least --min-installs installs.
A candidate is not a rule: a person checks it (mod page, an in-game test) before it goes into the knowledge files,
as docs/CONTRIBUTING.md says.

Raw packs stay private. Never commit, post or forward them; the summary holds counts and file names only.
Standard library only.
"""
import argparse
import io
import json
import os
import re
import sys
import urllib.request
import zipfile
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KNOWLEDGE = os.path.join(ROOT, "dist", "Data", "F4SE", "Plugins", "Vitals", "knowledge")
CODE = re.compile(r"^VTL-[A-Z2-9]{4}-[A-Z2-9]{4}$")


# ---------------------------------------------------------------- pull
def _get(url, key):
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {key}", "User-Agent": "vitals-share-inbox"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def pull(out, base=None, key=None):
    base = (base or os.environ.get("VITALS_SHARE_URL", "")).rstrip("/")
    key = key or os.environ.get("VITALS_READ_KEY", "")
    if not base or not key:
        raise SystemExit("Set VITALS_SHARE_URL and VITALS_READ_KEY (see server/README.md).")
    os.makedirs(out, exist_ok=True)
    cursor, new = None, 0
    while True:
        listing = json.loads(_get(f"{base}/v1/admin/packs" + (f"?cursor={cursor}" if cursor else ""), key))
        for p in listing["packs"]:
            code = p["code"]
            dest = os.path.join(out, f"{code}.zip")
            if not CODE.match(code) or os.path.exists(dest):
                continue
            with open(dest, "wb") as f:
                f.write(_get(f"{base}/v1/admin/pack/{code}", key))
            new += 1
        cursor = listing.get("cursor")
        if not cursor:
            break
    print(f"{new} new pack(s) in {out}")
    return new


# ---------------------------------------------------------------- reading one pack
def _text(z, name):
    try:
        return z.read(name).decode("utf-8", "replace")
    except KeyError:
        return ""


def read_pack(path):
    """One pack as plain data; None when the file is not a Vitals share pack."""
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile:
        return None
    with z:
        try:
            manifest = json.loads(_text(z, "manifest.json") or "{}")
        except json.JSONDecodeError:
            return None
        if manifest.get("format") != "vitals-share 1":
            return None
        pack = {"file": os.path.basename(path), "install": manifest.get("install_id") or os.path.basename(path),
                "runtime": manifest.get("runtime", "?"), "game": manifest.get("game_version", "?"),
                "vitals": manifest.get("vitals_version", "?"), "not_running": {}, "issues": [], "crashes": [],
                "unbound": [], "sources": {}}
        # this session's Vitals.jsonl, or the previous one when this launch stopped early
        for name in ("Vitals.jsonl", "Vitals.prev.jsonl"):
            lines = _text(z, name).splitlines()
            if not lines:
                continue
            for ln in lines:
                try:
                    rec = json.loads(ln)
                except json.JSONDecodeError:
                    continue  # a cut-off last line after a crash
                if rec.get("kind") == "doctor_issue":
                    pack["issues"].append((rec.get("severity", "?"), rec.get("category", "?")))
                    if rec.get("category") == "plugin_dll":
                        pack["not_running"][rec.get("subject", "?")] = rec.get("reason", "?")
            break
        for name in z.namelist():
            if name.startswith("crash/"):
                pack["crashes"].append(parse_crash(_text(z, name)))
        for ln in _text(z, "papyrus-summary.txt").splitlines():
            m = re.match(r"\s+(\d+)x\s+Unbound native function \"([^\"]+)\" in object \"([^\"]+)\"", ln)
            if m:
                pack["unbound"].append(f"{m.group(3)}.{m.group(2)}")
        for ln in _text(z, "dll-sources.txt").splitlines()[2:]:
            dll, _, src = ln.partition(" | ")
            if dll and src:
                pack["sources"][dll.strip()] = src.strip()
        return pack


def parse_crash(text):
    out = {"where": None, "modules": []}
    m = re.search(r"Unhandled exception[^\n]*?\b([A-Za-z0-9_\-.]+\.(?:dll|exe))\+([0-9A-Fa-f]+)", text, re.I)
    if m:
        out["where"] = f"{m.group(1)}+{m.group(2).upper()}"
    stack = re.split(r"\n\s*PROBABLE CALL STACK:?\s*\n", text, maxsplit=1, flags=re.I)
    region = stack[1][:6000] if len(stack) > 1 else ""
    mods = re.findall(r"([A-Za-z0-9_\-.]+\.(?:dll|exe))\+[0-9A-Fa-f]+", region, re.I)
    out["modules"] = [mod for mod, _ in Counter(mods).most_common(5)]
    return out


# ---------------------------------------------------------------- many packs
def known_dll_rules(folder=KNOWLEDGE):
    known = set()
    if os.path.isdir(folder):
        for f in sorted(os.listdir(folder)):
            if f.endswith(".txt"):
                with open(os.path.join(folder, f), encoding="utf-8", errors="replace") as fh:
                    for ln in fh:
                        parts = [p.strip() for p in ln.split("|")]
                        if len(parts) >= 5 and parts[0].split("@")[0].lower() == "dll":
                            known.add(parts[1].lower())
    return known


def summarize(folder, min_installs=5, knowledge=KNOWLEDGE):
    packs = []
    for f in sorted(os.listdir(folder)):
        if f.lower().endswith(".zip"):
            p = read_pack(os.path.join(folder, f))
            if p:
                packs.append(p)
    # every count is a set of install ids: the same player sending twice counts once
    by_install = {}
    for p in packs:
        by_install[p["install"]] = p  # latest pack per install, for the game build line
    dlls = defaultdict(lambda: {"installs": set(), "reasons": Counter(), "builds": Counter(), "sources": Counter()})
    crashes = defaultdict(set)
    crash_mods = defaultdict(set)
    unbound = defaultdict(set)
    issues = Counter()
    runtimes = Counter()
    for p in by_install.values():
        runtimes[f"{p['runtime']} {p['game']}"] += 1
    for p in packs:
        inst = p["install"]
        for dll, why in p["not_running"].items():
            d = dlls[dll.lower()]
            if inst in d["installs"]:
                continue
            d["installs"].add(inst)
            d["reasons"][why] += 1
            d["builds"][p["game"]] += 1
            if dll in p["sources"]:
                d["sources"][p["sources"][dll]] += 1
            d.setdefault("name", dll)
        for c in p["crashes"]:
            if c["where"]:
                crashes[c["where"]].add(inst)
            for mod in c["modules"]:
                crash_mods[mod].add(inst)
        for fn in set(p["unbound"]):
            unbound[fn].add(inst)
        for sev, cat in p["issues"]:
            issues[f"{sev} {cat}"] += 1
    known = known_dll_rules(knowledge)
    top = lambda c: ", ".join(f"{k} ({n})" for k, n in c.most_common(3)) or "-"
    L = [f"# Vitals share-pack summary", "",
         f"{len(packs)} pack(s) from {len(by_install)} install(s). Game builds: {top(runtimes)}.",
         "Counts are installs, not packs. Nothing here is a rule until a person confirms it in game.", ""]
    rows = sorted(dlls.values(), key=lambda d: -len(d["installs"]))
    cands = [d for d in rows if len(d["installs"]) >= min_installs and d["name"].lower() not in known]
    L += [f"## Candidate rules: not-running DLLs in {min_installs}+ installs with no knowledge rule", ""]
    L += ["| DLL | Installs | Loader reason | Game builds | Installed by (Vortex) |", "| --- | --- | --- | --- | --- |"]
    L += [f"| {d['name']} | {len(d['installs'])} | {top(d['reasons'])} | {top(d['builds'])} | {top(d['sources'])} |" for d in cands] or ["| (none yet) | | | | |"]
    L += ["", "## All not-running DLLs", "", "| DLL | Installs | Has a rule |", "| --- | --- | --- |"]
    L += [f"| {d['name']} | {len(d['installs'])} | {'yes' if d['name'].lower() in known else 'no'} |" for d in rows[:200]] or ["| (none) | | |"]
    L += ["", "## Crash locations", "", "| Where | Installs |", "| --- | --- |"]
    L += [f"| {w} | {len(s)} |" for w, s in sorted(crashes.items(), key=lambda kv: -len(kv[1]))[:50]] or ["| (none) | |"]
    L += ["", "## Modules on crash call stacks", "", "| Module | Installs |", "| --- | --- |"]
    L += [f"| {m} | {len(s)} |" for m, s in sorted(crash_mods.items(), key=lambda kv: -len(kv[1]))[:50]] or ["| (none) | |"]
    L += ["", "## Unbound Papyrus native functions (a script wants an F4SE plugin that is not running)", "",
          "| Script.Function | Installs |", "| --- | --- |"]
    L += [f"| {f} | {len(s)} |" for f, s in sorted(unbound.items(), key=lambda kv: -len(kv[1]))[:100]] or ["| (none) | |"]
    L += ["", "## Doctor findings by kind (all packs)", ""]
    L += [f"- {k}: {n}" for k, n in issues.most_common()] or ["- (none)"]
    return "\n".join(L) + "\n", {"packs": len(packs), "installs": len(by_install), "candidates": [d["name"] for d in cands]}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pull")
    p.add_argument("--out", default="inbox")
    s = sub.add_parser("summary")
    s.add_argument("folder")
    s.add_argument("--out")
    s.add_argument("--min-installs", type=int, default=5)
    a = ap.parse_args(argv)
    if a.cmd == "pull":
        pull(a.out)
        return 0
    text, _ = summarize(a.folder, a.min_installs)
    if a.out:
        open(a.out, "w", encoding="utf-8").write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
