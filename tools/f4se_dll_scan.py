#!/usr/bin/env python3
"""Scan F4SE plugin DLLs and predict which F4SE era will load them.

Reads each DLL's PE export table and, when present, the exported
F4SEPlugin_Version data (F4SEPluginVersionData), then applies the loader
rules from F4SE source:

  * F4SE 0.6.23 (Old-Gen 1.10.163) loads only DLLs exporting F4SEPlugin_Query
    and lets that function decide at runtime.
  * F4SE 0.7.x (Next-Gen 1.10.980/984, Anniversary 1.11.x) never calls Query;
    it needs the F4SEPlugin_Version data export and checks its flags or its
    compatibleVersions list.

Read-only, standard library only. Heuristic: confirm with f4se.log.

Usage:
  python3 f4se_dll_scan.py PATH [PATH ...]   (files or folders, e.g. Data/F4SE/Plugins)
  python3 f4se_dll_scan.py --json PATH
"""
import argparse
import json
import os
import struct
import sys

# F4SEPluginVersionData flag bits (f4se/PluginAPI.h)
ADDR_SIGNATURES = 1 << 0
ADDR_AL_1_10_980 = 1 << 1
ADDR_AL_1_11_137 = 1 << 2
STRUCT_NO_STRUCTS = 1 << 0
STRUCT_1_10_980 = 1 << 1
STRUCT_1_11_137 = 1 << 2

# Runtimes to evaluate: label -> (major, minor, build, era)
RUNTIMES = [
    ("1.10.163", (1, 10, 163), "OG"),
    ("1.10.984", (1, 10, 984), "NG"),
    ("1.11.240", (1, 11, 240), "AE"),
]

# Strings that hint at how a plugin resolves addresses
HINTS = [
    (b"version-%d-%d-%d-%d.bin", "Address Library (format string)"),
    (b"version-{}-{}-{}-{}.bin", "Address Library (format string)"),
    (b"f4rd-runtime.bin", "CommonLibF4RD runtime database"),
    (b"Unsupported Fallout 4 runtime", "Dear-Modding CommonLibF4 whitelist: terminates on unlisted builds"),
    (b"Failed to find offset for Address Library ID", "Address Library lookups"),
]


def make_version(major, minor, build, sub=0):
    return ((major & 0xFF) << 24) | ((minor & 0xFF) << 16) | ((build & 0xFFF) << 4) | (sub & 0xF)


def unpack_version(v):
    return f"{(v >> 24) & 0xFF}.{(v >> 16) & 0xFF}.{(v >> 4) & 0xFFF}"


class PEError(Exception):
    pass


def parse_pe(data):
    if data[:2] != b"MZ":
        raise PEError("not a PE file (no MZ header)")
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_off:pe_off + 4] != b"PE\0\0":
        raise PEError("bad PE signature")
    machine, nsec = struct.unpack_from("<HH", data, pe_off + 4)
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt_off = pe_off + 24
    magic = struct.unpack_from("<H", data, opt_off)[0]
    if magic != 0x20B:
        raise PEError("32-bit image (F4SE 0.7.x rejects these)")
    export_rva, export_size = struct.unpack_from("<II", data, opt_off + 112)
    sections = []
    sec_off = opt_off + opt_size
    for i in range(nsec):
        o = sec_off + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, va, max(vsize, rsize), raw, rsize))
    return machine, export_rva, export_size, sections


def rva_to_off(rva, sections):
    for _, va, size, raw, rsize in sections:
        if va <= rva < va + size:
            delta = rva - va
            if delta >= rsize:
                return None  # in uninitialised data
            return raw + delta
    return None


def read_cstr(data, off, limit=512):
    end = data.find(b"\0", off, off + limit)
    if end < 0:
        end = off + limit
    return data[off:end].decode("utf-8", "replace")


def exports(data, export_rva, sections):
    if not export_rva:
        return {}
    off = rva_to_off(export_rva, sections)
    if off is None:
        return {}
    (n_funcs, n_names, funcs_rva, names_rva, ords_rva) = struct.unpack_from("<5I", data, off + 20)
    funcs_off = rva_to_off(funcs_rva, sections)
    names_off = rva_to_off(names_rva, sections)
    ords_off = rva_to_off(ords_rva, sections)
    result = {}
    if None in (funcs_off, names_off, ords_off):
        return result
    for i in range(n_names):
        name_rva = struct.unpack_from("<I", data, names_off + 4 * i)[0]
        ordinal = struct.unpack_from("<H", data, ords_off + 2 * i)[0]
        if ordinal >= n_funcs:
            continue
        func_rva = struct.unpack_from("<I", data, funcs_off + 4 * ordinal)[0]
        noff = rva_to_off(name_rva, sections)
        if noff is not None:
            result[read_cstr(data, noff, 256)] = func_rva
    return result


def parse_version_data(data, rva, sections):
    off = rva_to_off(rva, sections)
    if off is None or off + 0x25C > len(data):
        return None
    data_version, plugin_version = struct.unpack_from("<II", data, off)
    name = read_cstr(data, off + 8, 256)
    author = read_cstr(data, off + 8 + 256, 256)
    addr, struc = struct.unpack_from("<II", data, off + 0x208)
    compat = []
    for i in range(16):
        v = struct.unpack_from("<I", data, off + 0x210 + 4 * i)[0]
        if v == 0:
            break
        compat.append(v)
    se_required, reserved_nb, reserved_b = struct.unpack_from("<III", data, off + 0x250)
    return {
        "dataVersion": data_version,
        "pluginVersion": plugin_version,
        "name": name,
        "author": author,
        "addressIndependence": addr,
        "structureIndependence": struc,
        "compatibleVersions": compat,
        "seVersionRequired": se_required,
        "reservedBreaking": reserved_b,
    }


def flag_names(value, table):
    names = [n for bit, n in table if value & bit]
    return names or ["none"]


ADDR_TABLE = [(ADDR_SIGNATURES, "Signatures"), (ADDR_AL_1_10_980, "AddressLibrary_1_10_980"), (ADDR_AL_1_11_137, "AddressLibrary_1_11_137")]
STRUCT_TABLE = [(STRUCT_NO_STRUCTS, "NoStructs"), (STRUCT_1_10_980, "1_10_980Layout"), (STRUCT_1_11_137, "1_11_137Layout")]


def verdict(runtime, era, has_query, vd, f4se=None, al_present=True, has_any=True):
    """Return (status, reason, why) for one runtime. status: yes / maybe / no.

    Same rules, in the same order, as F4SE (0.6.x Query; 0.7.x PluginManager::CheckPluginCompatibility) and
    Vitals' include/Vitals/Rules.h Loader::Check. `why` is the shared reason code; tests/loader_cases.txt feeds
    the same cases to both. f4se: running F4SE version as a tuple, None = unknown (that check is skipped).
    al_present: whether the Address Library file for this build exists (F4SE checks it for the current flag).
    """
    if not has_any:
        return "no", "no F4SEPlugin_* exports (not an F4SE plugin, or a helper DLL)", "not_plugin"
    if era == "OG":
        if has_query:
            return "maybe", "exports F4SEPlugin_Query; the plugin's own code accepts or rejects 1.10.163 at runtime", "og_query_decides"
        return "no", "no F4SEPlugin_Query export (F4SE 0.6.23 loads plugins only through Query)", "og_no_query"
    if vd is None:
        if has_query:
            return "no", "pre-Next-Gen plugin: Query only, no F4SEPlugin_Version data (F4SE 0.7.x never calls Query)", "no_version_data_query_only"
        return "no", "no F4SEPlugin_Version export", "no_version_data"
    if not vd.get("dataVersion", 1):
        return "no", "bad version data (dataVersion 0)", "bad_version_data"
    if not vd.get("name", "x"):
        return "no", "no plugin name in the version data", "no_name"
    ng = era == "NG"
    current_al = ADDR_AL_1_10_980 if ng else ADDR_AL_1_11_137
    addr = vd["addressIndependence"]
    addr_ok = addr & (ADDR_SIGNATURES | current_al)
    struct_ok = vd["structureIndependence"] & (STRUCT_NO_STRUCTS | (STRUCT_1_10_980 if ng else STRUCT_1_11_137))
    independent = bool(addr_ok and struct_ok and not vd["reservedBreaking"])
    if addr & current_al and not al_present:
        return "no", "uses the Address Library and the file for this game build is missing", "address_library_missing"
    if not independent:
        if make_version(*runtime) not in vd["compatibleVersions"]:
            if not ng and addr & ADDR_AL_1_10_980 and not addr & ADDR_AL_1_11_137:
                return "no", "Next-Gen-era flags only; F4SE 0.7.5+ requires the 1_11_137 flags (rebuild needed)", "ng_flags_only"
            if vd["compatibleVersions"]:
                return "no", "lists other game builds only: " + ", ".join(unpack_version(v) for v in vd["compatibleVersions"]), "other_builds"
            return "no", "not version-independent for this era and lists no game builds", "not_declared"
    need = vd["seVersionRequired"]
    if f4se is not None and need > make_version(*f4se):
        return "no", f"needs F4SE {unpack_version(need)} or newer", "needs_newer_f4se"
    if independent:
        needs_al = (addr & (ADDR_AL_1_10_980 | ADDR_AL_1_11_137)) and not (addr & ADDR_SIGNATURES)
        return "yes", "declares version independence for this era" + (" (needs the matching Address Library file)" if needs_al else ""), "independent"
    return "yes", "lists this exact runtime in compatibleVersions", "listed"


def scan_file(path):
    with open(path, "rb") as fh:
        data = fh.read()
    report = {"file": path}
    try:
        _, export_rva, _, sections = parse_pe(data)
        exp = exports(data, export_rva, sections)
    except PEError as exc:
        report["error"] = str(exc)
        return report
    except (struct.error, IndexError, ValueError):  # truncated or damaged: report it, keep scanning the others
        report["error"] = "damaged or truncated DLL (its headers could not be read)"
        return report
    f4se_exports = sorted(n for n in exp if n.startswith("F4SEPlugin_"))
    report["f4seExports"] = f4se_exports
    if not f4se_exports:
        report["error"] = "no F4SEPlugin_* exports (not an F4SE plugin, or a helper DLL)"
        return report
    vd = None
    if "F4SEPlugin_Version" in exp:
        try:
            vd = parse_version_data(data, exp["F4SEPlugin_Version"], sections)
        except (struct.error, IndexError, ValueError):
            vd = None
    has_query = "F4SEPlugin_Query" in exp
    if vd:
        report["versionData"] = dict(
            vd,
            addressIndependenceFlags=flag_names(vd["addressIndependence"], ADDR_TABLE),
            structureIndependenceFlags=flag_names(vd["structureIndependence"], STRUCT_TABLE),
            compatibleVersionsText=[unpack_version(v) for v in vd["compatibleVersions"]],
            seVersionRequiredText=unpack_version(vd["seVersionRequired"]) if vd["seVersionRequired"] else "any",
        )
    report["hints"] = sorted({label for needle, label in HINTS if needle in data})
    report["verdicts"] = {}
    for label, runtime, era in RUNTIMES:
        status, reason, why = verdict(runtime, era, has_query, vd)
        report["verdicts"][f"{era} {label}"] = {"status": status, "reason": reason, "why": why}
    if has_query and vd:
        report["style"] = "multi-era (exports both Query and Version)"
    elif vd:
        report["style"] = "Next-Gen/Anniversary era (Version data only)"
    else:
        report["style"] = "Old-Gen era (Query only)"
    return report


def iter_paths(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                for f in sorted(files):
                    if f.lower().endswith(".dll"):
                        yield os.path.join(root, f)
        else:
            yield p


def print_report(r):
    print(f"== {r['file']}")
    if "error" in r:
        print(f"   {r['error']}")
        return
    print(f"   Exports: {', '.join(r['f4seExports'])}")
    print(f"   Style:   {r['style']}")
    vd = r.get("versionData")
    if vd:
        print(f"   Name/author: {vd['name'] or '?'} / {vd['author'] or '?'} (plugin version {vd['pluginVersion']})")
        print(f"   addressIndependence:   {', '.join(vd['addressIndependenceFlags'])}")
        print(f"   structureIndependence: {', '.join(vd['structureIndependenceFlags'])}")
        print(f"   compatibleVersions:    {', '.join(vd['compatibleVersionsText']) or 'none'}")
        print(f"   minimum F4SE:          {vd['seVersionRequiredText']}")
    for h in r["hints"]:
        print(f"   Hint: {h}")
    for rt, v in r["verdicts"].items():
        print(f"   {rt:<12} {v['status']:<5} {v['reason']}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", help="DLL files or folders to scan")
    ap.add_argument("--json", action="store_true", help="print JSON instead of text")
    args = ap.parse_args()
    reports = [scan_file(p) for p in iter_paths(args.paths)]
    if not reports:
        print("No DLLs found.", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(reports, indent=2))
    else:
        for r in reports:
            print_report(r)
        print("\nVerdicts are predictions from the loader rules. 'yes' means F4SE should accept the DLL, not that its")
        print("addresses or struct layouts are right. In-DLL runtime checks can still refuse or terminate. Confirm in f4se.log.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
