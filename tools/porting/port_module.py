#!/usr/bin/env python3
"""Per-module help for the 3527/3549 -> 3576 port (docs/research/port-3576.md). Read-only on the installed exe.

For one source file: every byte array with a 3527/3549 address (trailing `// 0x...` comment or a same-named constant,
as in port_near.py) is placed at old + shift, where shift comes from the file's own unique near matches
(data/port_near_3576.json) — most modules moved as whole functions, so one shift (or a few, one per function) covers
them. At that address the array must match with relative operands masked (signature_map.relative_mask); the report
says whether the bytes are identical and, when only relative fields differ, prints the 3576 bytes as a C++ array.

  python tools/porting/port_module.py src/mod/session2/guest_seq_route.cpp [--shift 0x420] [--only 3527]
"""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from rtti_dump import Pe
from signature_map import relative_mask
from port_near import arrays


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("file")
    ap.add_argument("--exe", type=Path, default=ROOT.parent / "bin64" / "CrimsonDesert.exe")
    ap.add_argument("--shift", type=lambda s: int(s, 0), action="append")
    ap.add_argument("--only", default="", help="only arrays whose name contains this text (e.g. 3527)")
    args = ap.parse_args()
    data = args.exe.read_bytes()
    pe = Pe(data)
    rel = Path(args.file).as_posix()
    near = json.loads((Path(__file__).parent / "data" / "port_near_3576.json").read_text(encoding="utf-8"))
    shifts = args.shift
    if not shifts:
        c = Counter(int(r["near"][0], 16) - int(r["old"], 16) for r in near["results"]
                    if r["file"] == rel and r["status"] == "unique")
        shifts = [s for s, _ in c.most_common()]
        print(f"shifts from the file's unique matches: {[(hex(s), n) for s, n in c.most_common()]}")
    for it in arrays():
        if it["file"] != rel or it["old"] is None or args.only not in it["name"]:
            continue
        mask, _, _ = relative_mask(it["data"], it["old"])
        placed = None
        for s in shifts:
            off = pe.rva_to_off(it["old"] + s)
            cur = data[off:off + len(it["data"])] if off is not None else b""
            if len(cur) == len(it["data"]) and all(not m or a == b for a, b, m in zip(it["data"], cur, mask)):
                placed = (s, cur)
                break
        if not placed:
            print(f"  {it['name']:28} old {it['old']:#x}  NOT at any shift")
            continue
        s, cur = placed
        same = cur == it["data"]
        line = f"  {it['name']:28} old {it['old']:#x} -> {it['old'] + s:#x} (+{s:#x}) {'same bytes' if same else 'RELATIVE FIELDS DIFFER'}"
        print(line)
        if not same:
            print("      3576: {" + ", ".join(f"0x{b:02X}" for b in cur) + "}")


if __name__ == "__main__":
    main()
