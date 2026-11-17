#!/usr/bin/env python3
"""Defib log analyzer: turn one play session's logs into one short report.

Reads, when present:
  * F4SE\\f4se.log                     F4SE's own load results
  * F4SE\\Vitals\\Vitals.jsonl          Vitals' environment, address, native and probe records
  * F4SE\\Vitals\\breadcrumbs.bin       which port hook ran last (and breadcrumbs.prev.bin)
  * F4SE\\crash-*.log                   AddictolCrashLogger / Buffout crash logs (newest one)
  * Logs\\Script\\Papyrus.0.log        Papyrus errors (needs Papyrus logging turned on)
  * Fallout4Custom.ini                 whether Papyrus logging is on

Usage:
  python3 defib_logs.py "C:\\Users\\<you>\\Documents\\My Games\\Fallout4" [--out report.md]
  python3 defib_logs.py "...\\Fallout4" --compare          (this run vs the run before: what a mod changed)
         [--plugins "C:\\...\\Fallout 4\\Data\\F4SE\\Plugins"]   (adds f4se_dll_scan predictions)

Read-only. Standard library only. The Windows user name is removed from the report.
"""
import argparse
import configparser
import glob
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))

F4SE_HEX = re.compile(r"\bf4se\s+([0-9A-Fa-f]{8})\b", re.I)
F4SE_HEADER = re.compile(r"version\s*=\s*(\d+\.\d+\.\d+)\s+([0-9A-Fa-f]{8})\b")
RUNTIME = re.compile(r"runtime[^\n]*?(\d+\.\d+\.\d+)", re.I)
DLL_NAMED = re.compile(r"plugin\s+(?:[A-Za-z]:)?[^\n]*?([^\\/\n]+?\.dll)", re.I)  # "plugin X Y.dll (..."
DLL = re.compile(r"([A-Za-z0-9_\-\.\+]+\.dll)", re.I)
GOOD = re.compile(r"loaded correctly|loaded successfully|\bloaded\b(?!.*(fail|not|couldn))", re.I)
BAD = re.compile(
    r"couldn'?t load|could not load|failed|incompatible|disabled|not compatible|"
    r"bad dll|unsupported|version mismatch|error|missing|address library", re.I)


# ---------------------------------------------------------------- helpers
def redact(text):
    for var in ("USERPROFILE", "HOME"):
        v = os.environ.get(var)
        if v and len(v) > 3:
            text = re.sub(re.escape(v), "%USERPROFILE%", text, flags=re.I)
    text = re.sub(r"([A-Za-z]:[\\/]+Users[\\/]+)[^\\/\n\"]+", r"\1<user>", text, flags=re.I)
    return text


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def dll_key(name):
    return os.path.basename(name.strip()).lower()


# ---------------------------------------------------------------- f4se.log
def parse_f4se_log(path):
    text = read_text(path)
    out = {"path": path, "runtime": None, "f4se": None, "plugins": {}, "errors": []}
    hd = F4SE_HEADER.search(text)
    if hd:  # F4SE 0.7.x: "initialize (version = 0.7.9 010B0F00 <timestamp>, ..."
        out["f4se"] = hd.group(1)
        v = int(hd.group(2), 16)
        out["runtime"] = f"{v >> 24}.{(v >> 16) & 0xFF}.{(v >> 4) & 0xFFF}"
    else:
        m = RUNTIME.search(text)
        if m:
            out["runtime"] = m.group(1)
        hx = F4SE_HEX.search(text)
        if hx:
            v = int(hx.group(1), 16)
            out["f4se"] = f"{v >> 24}.{(v >> 16) & 0xFF}.{(v >> 4) & 0xFFF}"
    for ln in text.splitlines():
        d = DLL_NAMED.search(ln) or DLL.search(ln)
        name = dll_key(d.group(1)) if d else None
        if name and re.match(r"f4se_\d", name):
            continue
        if BAD.search(ln):
            if name:
                out["plugins"][name] = ("failed", ln.strip())
            else:
                out["errors"].append(ln.strip())
        elif name and GOOD.search(ln):
            out["plugins"][name] = ("loaded", ln.strip())
    return out


# ---------------------------------------------------------------- Vitals.jsonl
def parse_vitals(path):
    recs = defaultdict(list)
    bad = 0
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            try:
                r = json.loads(ln)
            except json.JSONDecodeError:
                bad += 1  # a cut-off last line after a crash is expected
                continue
            kind = r.get("kind", "?")
            if kind in ("fn", "ptr") and "result" in r:  # Vitals 0.1.0 wrote the entry kind over "kind"
                kind = "address"
            recs[kind].append(r)
    recs["_bad_lines"] = bad
    return recs


# ---------------------------------------------------------------- breadcrumbs.bin
def parse_breadcrumbs(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < 64 or data[:4] != b"VTLB":
        return None
    _ver, cap, esize, next_seq = struct.unpack_from("<IIIQ", data, 4)
    # The same layout check as Share::BreadcrumbsText (include/Vitals/Share.h): 128-byte entries, at most 65536 of them, all
    # present. A damaged or newer file is "no breadcrumbs", not a loop over a made-up capacity or entries read at wrong offsets.
    if esize != 128 or not 0 < cap <= 65536 or len(data) < 64 + cap * esize:
        return None
    entries = []
    for i in range(cap):
        off = 64 + i * esize
        if off + esize > len(data):
            break
        seq, qpc, tid, _pad = struct.unpack_from("<QqII", data, off)
        if not seq:
            continue
        port = data[off + 24: off + 64].split(b"\0", 1)[0].decode("utf-8", "replace")
        where = data[off + 64: off + 128].split(b"\0", 1)[0].decode("utf-8", "replace")
        entries.append((seq, tid, port, where))
    entries.sort()
    return {"next_seq": next_seq, "entries": entries}


# ---------------------------------------------------------------- crash log
def parse_crash(path):
    text = read_text(path)
    out = {"path": path, "exception": None, "modules": []}
    m = re.search(r"Unhandled exception[^\n]*", text, re.I)
    if m:
        out["exception"] = m.group(0).strip()
    stack = re.split(r"\n\s*(?:PROBABLE CALL STACK|STACK):?\s*\n", text, maxsplit=1, flags=re.I)
    region = stack[1][:6000] if len(stack) > 1 else text[:6000]
    mods = re.findall(r"([A-Za-z0-9_\-\.]+\.(?:dll|exe))\+[0-9A-Fa-f]+", region, re.I)
    out["modules"] = [m for m, _ in Counter(mods).most_common(8)]
    return out


# ---------------------------------------------------------------- Papyrus
def papyrus_settings(mygames):
    ini = os.path.join(mygames, "Fallout4Custom.ini")
    want = {"benablelogging": "1", "benabletrace": "1", "bloaddebuginformation": "1"}
    if not os.path.exists(ini):
        return None, list(want)
    cp = configparser.ConfigParser(strict=False, interpolation=None)
    try:
        cp.read_string(read_text(ini))
    except configparser.Error:
        return ini, list(want)
    sec = next((s for s in cp.sections() if s.lower() == "papyrus"), None)
    missing = [k for k, v in want.items() if not sec or cp.get(sec, k, fallback="0").strip() != v]
    return ini, missing


def parse_papyrus(path):
    errs = Counter()
    for ln in read_text(path).splitlines():
        if re.search(r"\berror\b", ln, re.I):
            errs[re.sub(r"^\[[^\]]*\]\s*", "", ln).strip()[:200]] += 1
    return errs


# ---------------------------------------------------------------- report
def build_report(mygames, plugins_dir=None):
    f4 = os.path.join(mygames, "F4SE")
    vit = os.path.join(f4, "Vitals")
    L = ["# Defib session report", ""]
    issues = []

    flog_path = os.path.join(f4, "f4se.log")
    flog = parse_f4se_log(flog_path) if os.path.exists(flog_path) else None
    vpath = os.path.join(vit, "Vitals.jsonl")
    v = parse_vitals(vpath) if os.path.exists(vpath) else None

    # --- environment
    L.append("## Environment")
    env = v["environment"][0] if v and v["environment"] else None
    if env:
        L.append(f"- Game {env.get('game_version')} ({env.get('runtime')}), F4SE {env.get('f4se_version')}, "
                 f"Vitals {env.get('vitals_version')}")
        L.append(f"- Address Library `{env.get('address_library_file')}`: "
                 f"{'present' if env.get('address_library_present') else 'MISSING'}")
        if not env.get("address_library_present"):
            issues.append("Address Library file for this game build is missing.")
    elif v is not None:
        L.append("- Vitals ran but wrote no environment record (it may have stopped early).")
        issues.append("Vitals environment record missing.")
    else:
        L.append("- No Vitals output found (`F4SE\\Vitals\\Vitals.jsonl`). Is Vitals.dll installed and loading?")
    if flog:
        L.append(f"- f4se.log: runtime {flog['runtime'] or '?'}, F4SE {flog['f4se'] or '?'}")
        if not flog["plugins"]:
            issues.append("f4se.log lists no plugins at all: was the game started outside the mod manager (MO2/Vortex)?")
        if os.path.exists(vpath) and os.path.getmtime(flog_path) - os.path.getmtime(vpath) > 120 and not any("vitals" in k for k in flog["plugins"]):
            issues.append("f4se.log is from a later launch than the Vitals report, and Vitals did not load in it.")
    else:
        L.append("- No f4se.log found.")
    L.append("")

    # --- load results and cross-check
    L.append("## Plugin load results")
    if flog:
        loaded = sorted(k for k, (s, _) in flog["plugins"].items() if s == "loaded")
        failed = sorted(k for k, (s, _) in flog["plugins"].items() if s == "failed")
        L.append(f"- f4se.log: {len(loaded)} loaded, {len(failed)} failed")
        for k in failed:
            L.append(f"  - FAILED `{k}`: {flog['plugins'][k][1][:160]}")
    if v and v["plugin"]:
        in_proc = {dll_key(p["file"]) for p in v["plugin"] if p.get("in_process")}
        on_disk = {dll_key(p["file"]) for p in v["plugin"]}
        L.append(f"- Vitals: {len(on_disk)} DLLs in the plugin folder, {len(in_proc)} in the game process")
        if flog:
            f_loaded = {k for k, (s, _) in flog["plugins"].items() if s == "loaded"}
            only_v = sorted((in_proc & on_disk) - f_loaded)
            only_f = sorted((f_loaded & on_disk) - in_proc)
            if not only_v and not only_f:
                L.append("- Cross-check: Vitals and f4se.log AGREE on which plugins loaded.")
            else:
                issues.append("Vitals and f4se.log disagree on loaded plugins (see list).")
                for k in only_v:
                    L.append(f"  - in process per Vitals, not 'loaded' in f4se.log: `{k}`")
                for k in only_f:
                    L.append(f"  - 'loaded' in f4se.log, not in process per Vitals: `{k}`")
        unregistered = [p["file"] for p in v["plugin"] if p.get("in_process") and p.get("f4se_registered") is False]
        for f in unregistered:
            L.append(f"  - `{f}` is in the process but F4SE has no plugin record for its name")
    L.append("")

    # --- plugin doctor (Vitals 0.3.0+)
    if v and (v["doctor_issue"] or v["doctor"]):
        L.append("## Plugin doctor")
        sev_order = {"problem": 0, "warning": 1, "info": 2}
        for d in sorted(v["doctor_issue"], key=lambda r: (sev_order.get(r.get("severity"), 3), r.get("category", ""), r.get("subject", ""))):
            sev = d.get("severity", "?")
            L.append(f"- {sev.upper()} [{d.get('category')}] {d.get('message')}")
            if d.get("advice"):
                L.append(f"  - what to do: {d.get('advice')}")
            if sev == "problem":
                issues.append(f"Doctor: {d.get('message')}")
        if not v["doctor_issue"]:
            L.append("- No problems found.")
        L.append("")

    # --- plugin inventory
    if v and v["plugin"]:
        L.append("## Plugin inventory")
        for pl in sorted(v["plugin"], key=lambda x: x["file"].lower()):
            L.append(f"- `{pl['file']}` {pl.get('file_version') or '-'} sha256 `{(pl.get('sha256') or '-')[:16]}…`"
                     f" {'in process' if pl.get('in_process') else 'NOT loaded'}")
        L.append("")

    # --- address check
    if v and v["address_check_unavailable"]:
        u = v["address_check_unavailable"][0]
        L.append("## Address check")
        L.append(f"- SKIPPED: Address Library `{u.get('address_library_file')}` is not usable ({u.get('problem')}).")
        L.append("")
        issues.append(f"Address Library not usable: {u.get('problem')}.")
    if v and v["address"]:
        c = Counter(a.get("result") for a in v["address"])
        L.append("## Address check")
        L.append("- " + ", ".join(f"{k}: {n}" for k, n in sorted(c.items())))
        hooked = [a for a in v["address"] if a.get("result") == "hooked"]
        if hooked:
            by = Counter(a.get("hooked_by", "?") for a in hooked if a.get("owner_category", "plugin") not in ("game", "system"))
            L.append("- functions detoured by another plugin: " + ", ".join(f"`{m}` x{n}" for m, n in by.most_common()))
            for a in hooked[:15]:
                L.append(f"  - `{a.get('name')}` -> `{a.get('hooked_by')}`")
        for a in v["address"]:
            if a.get("result") in ("MISMATCH", "differs", "patched_in_place", "unresolved", "unreadable", "bad_id_text", "bad_expected_value"):
                stub = f", starts with `{a['stub']}`" if a.get("stub") else ""
                L.append(f"  - {a['result']} `{a.get('name')}` id {a.get('id', '-')} ({a.get('source')}{stub})")
        if c.get("MISMATCH") or c.get("unreadable"):
            issues.append("Address check found mismatched or unreadable addresses.")
        if c.get("bad_expected_value"):
            issues.append(f"{c['bad_expected_value']} address list line(s) have an expected value that is not hex: fix the list.")
        pc = Counter(a.get("ptr_class") for a in v["address"] if a.get("ptr_class"))
        if pc:
            L.append("- singleton entries by what is really at the address: " + ", ".join(f"{k}: {n}" for k, n in sorted(pc.items())))
        nlocal = sum(1 for a in v["address"] if a.get("expected_from_local_baseline"))
        if nlocal:
            L.append(f"- {nlocal} expected values came from the local baseline (captured in memory on this build)")
        if c.get("patched_in_place"):
            L.append(f"- {c['patched_in_place']} function(s) switched off in place (start with a return stub): a fix mod disabled them."
                     " The bytes do not say which mod.")
        if c.get("differs"):
            L.append(f"- {c['differs']} function(s) differ from the expected bytes: patched in place by another plugin (check who) or a wrong address."
                     " Informational unless a port uses that ID.")
        L.append("")

    # --- timeline
    if v and v["phase"]:
        L.append("## Timeline (ms since Vitals loaded)")
        L.append("- " + " -> ".join(f"{p['event']} {p['ms_since_vitals_load']}" for p in v["phase"]))
        if v["auto_exit"]:
            L.append(f"- Test-rig auto-exit fired ({v['auto_exit'][0].get('reason')}); the game was ended without saving on purpose.")
        L.append("")

    # --- natives
    if v and v["papyrus_native"]:
        per = Counter(n["module"] for n in v["papyrus_native"])
        L.append("## Papyrus natives by DLL")
        for mod, n in per.most_common():
            L.append(f"- `{mod}`: {n}")
        L.append("")

    # --- probes
    if v and v["layout_probe"]:
        L.append("## Layout probes")
        for p in v["layout_probe"]:
            verdict = p.get("verdict") or ("OK" if p.get("agrees_with_library") else "CHECK")
            slots = p.get("advancing_ms_slots") or p.get("ms_counter_slots") or p.get("timestamp_slots")
            found = ", ".join(slots) if slots else p.get("best_candidate")
            how = "slots advancing like a ms counter" if "advancing_ms_slots" in p else "counter-like slots (older one-shot method)"
            L.append(f"- {p['probe']} on {p.get('runtime')} ({p.get('when', '?')}): {how} {found or 'none'}, "
                     f"library {p.get('library_offset')} -> {verdict}")
            if verdict == "CHECK":
                issues.append(f"Layout probe {p['probe']} ({p.get('when')}) disagrees with the library.")
        L.append("")

    # --- breadcrumbs
    for name in ("breadcrumbs.bin", "breadcrumbs.prev.bin"):
        bp = os.path.join(vit, name)
        if os.path.exists(bp):
            bc = parse_breadcrumbs(bp)
            if bc and bc["entries"]:
                L.append(f"## Last breadcrumbs ({name})")
                for seq, tid, port, where in bc["entries"][-8:]:
                    L.append(f"- #{seq} thread {tid}: {port} / {where}")
                L.append("")

    # --- crash
    crashes = sorted(glob.glob(os.path.join(f4, "crash-*.log")), key=os.path.getmtime)
    if crashes:
        cr = parse_crash(crashes[-1])
        L.append("## Newest crash log")
        L.append(f"- `{os.path.basename(cr['path'])}`: {cr['exception'] or 'exception line not found'}")
        if cr["modules"]:
            L.append("- Modules near the top of the stack: " + ", ".join(f"`{m}`" for m in cr["modules"]))
        L.append(f"- Run CLASSIC on it for a full scan. {len(crashes)} crash log(s) in the folder.")
        L.append("")

    # --- papyrus
    ini, missing = papyrus_settings(mygames)
    plog = os.path.join(mygames, "Logs", "Script", "Papyrus.0.log")
    L.append("## Papyrus")
    if missing:
        L.append(f"- Papyrus logging not fully on (missing in [Papyrus] of Fallout4Custom.ini: {', '.join(missing)})")
    if os.path.exists(plog):
        errs = parse_papyrus(plog)
        L.append(f"- Papyrus.0.log: {sum(errs.values())} error lines, {len(errs)} distinct")
        for e, n in errs.most_common(10):
            L.append(f"  - x{n} {e}")
    else:
        L.append("- No Papyrus.0.log")
    L.append("")

    # --- DLL scan
    if plugins_dir and os.path.isdir(plugins_dir):
        sys.path.insert(0, HERE)
        import subprocess
        scan = subprocess.run([sys.executable, os.path.join(HERE, "f4se_dll_scan.py"), plugins_dir],
                              capture_output=True, text=True)
        L.append("## DLL scan (predicted load per build)")
        L.append("```")
        L.append(scan.stdout.strip()[:20000])
        L.append("```")
        L.append("")

    # --- Stage 0 gate (per build)
    gate = stage0_gate(flog, v)
    if gate:
        L.append("## Stage 0 gate on this build")
        for label, ok, note in gate:
            L.append(f"- {'PASS' if ok else 'FAIL'}: {label}{f' ({note})' if note else ''}")
        L.append("")

    head = ["## Summary"]
    head += [f"- {i}" for i in issues] or ["- No problems found by the analyzer."]
    head.append("")
    return redact("\n".join(L[:2] + head + L[2:]))


def stage0_gate(flog, v):
    """The Stage 0 'done when' items that one session can show. Returns [(label, passed, note)]."""
    if not v:
        return []
    out = []
    env = v["environment"][0] if v["environment"] else {}
    out.append(("Vitals loaded and wrote its report", bool(env), env.get("vitals_version", "")))
    if flog and v["plugin"]:
        in_proc = {dll_key(p["file"]) for p in v["plugin"] if p.get("in_process")}
        on_disk = {dll_key(p["file"]) for p in v["plugin"]}
        f_loaded = {k for k, (s, _) in flog["plugins"].items() if s == "loaded"}
        agree = not ((in_proc & on_disk) ^ (f_loaded & on_disk))
        out.append(("environment report matches f4se.log", agree, ""))
    adr = v["address"]
    if adr:
        c = Counter(a.get("result") for a in adr)
        bad = c.get("unresolved", 0) + c.get("unreadable", 0) + c.get("bad_id_text", 0) + c.get("MISMATCH", 0)
        out.append(("known IDs resolve on this build", bad == 0, f"{len(adr) - bad} of {len(adr)}"))
        out.append(("known IDs match their expected bytes", c.get("match", 0) > 0,
                    f"{c.get('match', 0)} match, {c.get('hooked', 0)} hooked, {c.get('patched_in_place', 0)} patched in place, {c.get('differs', 0)} differ, "
                    f"{c.get('resolved_no_baseline', 0)} without a baseline"))
    out.append(("log analyzer produced one report", True, ""))
    return out


def _index(v):
    adr = {a.get("name"): a for a in v["address"]} if v else {}
    plg = {dll_key(p["file"]): p for p in v["plugin"]} if v else {}
    nat = Counter(n["module"] for n in v["papyrus_native"]) if v else Counter()
    env = v["environment"][0] if v and v["environment"] else {}
    return adr, plg, nat, env


def compare_sessions(cur_path, prev_path):
    """What changed between two Vitals sessions: plugins, hooks and natives.
    The census use: run once without a mod and once with it; the difference is what that mod does to the engine."""
    cur, prev = parse_vitals(cur_path), parse_vitals(prev_path)
    a1, p1, n1, e1 = _index(prev)
    a2, p2, n2, e2 = _index(cur)
    L = ["# Defib session comparison", "",
         f"- before: `{os.path.basename(prev_path)}` game {e1.get('game_version', '?')}, Vitals {e1.get('vitals_version', '?')}",
         f"- after:  `{os.path.basename(cur_path)}` game {e2.get('game_version', '?')}, Vitals {e2.get('vitals_version', '?')}", ""]
    if e1.get("game_version") != e2.get("game_version"):
        L += ["**Different game builds: address results are not comparable, only the plugin list is.**", ""]
    L.append("## Plugins")
    added = sorted(set(p2) - set(p1))
    removed = sorted(set(p1) - set(p2))
    changed = sorted(k for k in set(p1) & set(p2) if (p1[k].get("sha256") != p2[k].get("sha256")) or (bool(p1[k].get("in_process")) != bool(p2[k].get("in_process"))))
    for k in added:
        L.append(f"- added `{p2[k]['file']}` {'(loaded)' if p2[k].get('in_process') else '(NOT loaded)'}")
    for k in removed:
        L.append(f"- removed `{p1[k]['file']}`")
    for k in changed:
        L.append(f"- changed `{p2[k]['file']}`: file or load state differs")
    if not (added or removed or changed):
        L.append("- no change")
    L.append("")
    if e1.get("game_version") == e2.get("game_version"):
        L.append("## Engine functions")
        def is_code(r):
            return (r.get("checked_as") or r.get("entry_kind")) == "fn"

        def state(r):
            # detoured / patched / clean, from what was in memory (works with or without a baseline)
            if r.get("jump_target") and r.get("owner_category") not in ("game", "system"):
                return "detoured", r.get("hooked_by") or "?"
            if r.get("result") in ("differs", "patched_in_place"):
                return "patched in place", ""
            return "clean", ""

        moved = []
        for name in sorted(set(a1) & set(a2)):
            r1, r2 = a1[name], a2[name]
            if not (is_code(r1) and is_code(r2)):
                continue  # data changes at run time; only code is compared
            s1, s2 = state(r1), state(r2)
            if s1 != s2:
                moved.append((name, s1, s2))
        by = Counter((s2[1] if s2[1] not in ("", "?") else ("a trampoline (owner not a DLL)" if s2[0] == "detoured" else "in place"))
                     for _, s1, s2 in moved if s2[0] != "clean")
        gone = sum(1 for _, s1, s2 in moved if s1[0] != "clean" and s2[0] == "clean")
        if gone:
            L.append(f"- {gone} function(s) no longer hooked or patched")
        if by:
            L.append("- newly hooked or patched, by plugin: " + ", ".join(f"`{m}` x{n}" for m, n in by.most_common()))
        for name, s1, s2 in moved[:60]:
            L.append(f"  - `{name}`: {s1[0]}{' by ' + s1[1] if s1[1] else ''} -> {s2[0]}{' by ' + s2[1] if s2[1] else ''}")
        if len(moved) > 60:
            L.append(f"  - ... and {len(moved) - 60} more")
        if not moved:
            L.append("- no change")
        L.append("")
    if n1 or n2:
        L.append("## Papyrus natives by DLL")
        for m in sorted(set(n1) | set(n2)):
            if n1[m] != n2[m]:
                L.append(f"- `{m}`: {n1[m]} -> {n2[m]}")
        if n1 == n2:
            L.append("- no change")
        L.append("")
    return redact("\n".join(L))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mygames", help=r"Documents\My Games\Fallout4 folder")
    ap.add_argument("--plugins", help=r"game Data\F4SE\Plugins folder (optional)")
    ap.add_argument("--out", help="write the report here instead of printing it")
    ap.add_argument("--compare", nargs="?", const="prev", metavar="OTHER.jsonl",
                    help="compare this session with another Vitals.jsonl (default: Vitals.prev.jsonl, the run before; "
                         "or N to use the Nth newest file in Vitals\\history, 1 = newest)")
    ap.add_argument("--history", action="store_true", help="list the sessions kept in Vitals\\history")
    a = ap.parse_args(argv)
    vit = os.path.join(a.mygames, "F4SE", "Vitals")
    hist = sorted(glob.glob(os.path.join(vit, "history", "Vitals-*.jsonl")), reverse=True)
    if a.history:
        print("\n".join(f"{i + 1}: {os.path.basename(h)}" for i, h in enumerate(hist)) or "no history kept")
        return 0
    if a.compare:
        if a.compare == "prev":
            other = os.path.join(vit, "Vitals.prev.jsonl")
        elif a.compare.isdigit() and 1 <= int(a.compare) <= len(hist):
            other = hist[int(a.compare) - 1]
        else:
            other = a.compare
        rep = compare_sessions(os.path.join(vit, "Vitals.jsonl"), other)
    else:
        rep = build_report(a.mygames, a.plugins)
    if a.out:
        with open(a.out, "w", encoding="utf-8") as f:
            f.write(rep)
        print(f"wrote {a.out}")
    else:
        print(rep)
    return 0


if __name__ == "__main__":
    sys.exit(main())
