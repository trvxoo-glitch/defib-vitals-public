#!/usr/bin/env python3
"""Build Vitals' address list from the pinned CommonLibF4's RE/IDs.h.

Usage: python3 tools/gen_addresses_from_commonlib.py lib/commonlibf4/include/RE/IDs.h [--keep OLD_LIST] > new_list.txt

--keep: the current dist/Data/F4SE/Plugins/Vitals/addresses/commonlibf4.txt. For every name still in the library,
its hand-set kind ("data") and its expected-byte columns are carried over, so regenerating after a library update
never wipes the shipped signatures. Always use it when updating the shipped list (docs/MAINTAINING.md).

Every entry is an ID the library already uses, so Vitals only checks library-provided addresses.
Missing runtime slots are filled the way REL::VariantID fills them (repeat the last value).
Entries whose value is 0 (marked inlined) or an offset rather than an ID are skipped.
Without --keep, expected bytes are "-" (Vitals then has nothing to compare for that build until a local baseline exists).
"""
import re
import sys

NS = re.compile(r'^\s*namespace\s+(\w+)')
ENTRY = re.compile(r'inline\s+constexpr\s+REL::(?:Variant)?ID\s+(\w+)\s*\{\s*([0-9,\s]+)\}')


def read_keep(path):
    keep = {}
    if path:
        for ln in open(path, encoding="utf-8"):
            f = [x.strip() for x in ln.rstrip("\n").split("|")]
            if len(f) >= 8 and not ln.startswith("#"):
                keep[f[0]] = f
    return keep


def main(path, keep_path=None):
    keep = read_keep(keep_path)
    stack = []  # (name, depth at its opening brace)
    depth, pending = 0, []
    out = ["# Generated from CommonLibF4 RE/IDs.h. name | kind | og | ng | ae | og_bytes | ng_bytes | ae_bytes"]
    for line in open(path, encoding="utf-8", errors="replace"):
        code = line.split("//", 1)[0]
        pending += re.findall(r"\bnamespace\s+([\w:]+)", code)
        e = ENTRY.search(code)
        if e:
            ids = [int(x) for x in e.group(2).replace(" ", "").split(",") if x]
            while len(ids) < 3:
                ids.append(ids[-1])
            if 0 not in ids[:3]:
                names = [p for n, _ in stack for p in n.split("::") if p not in ("RE", "ID")]
                name = "::".join(names + [e.group(1)])
                kind = "ptr" if e.group(1).lower().endswith("singleton") else "fn"
                old = keep.get(name)
                if old and old[1] == "data":
                    kind = "data"  # set by hand: a global, not code
                b = old[5:8] if old and old[2:5] == [str(i) for i in ids[:3]] else ["-", "-", "-"]  # bytes only if the IDs are unchanged
                out.append(f"{name}|{kind}|{ids[0]}|{ids[1]}|{ids[2]}|{b[0]}|{b[1]}|{b[2]}")
        for ch in code:
            if ch == "{":
                depth += 1
                if pending:
                    stack.append((pending.pop(0), depth))
            elif ch == "}":
                if stack and stack[-1][1] == depth:
                    stack.pop()
                depth -= 1
    print("\n".join(out))


if __name__ == "__main__":
    args = sys.argv[1:]
    keep_path = None
    if "--keep" in args:
        i = args.index("--keep")
        keep_path = args[i + 1] if i + 1 < len(args) else sys.exit(__doc__)
        del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    main(args[0], keep_path)
