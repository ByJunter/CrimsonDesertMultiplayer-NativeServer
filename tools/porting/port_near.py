#!/usr/bin/env python3
"""Anchors and near-search for porting 3527/3549 code addresses to a later build (1.0.0.3576 first).

Read-only on the installed exe. Evidence, not a port: each new address still needs its function read with capstone.

1. Anchors: every byte array in src/mod whose address is written in the source as a trailing comment on the line
   where the array ends (`... };  // 0xBB09E8 ...`) or as an `= 0x...;` constant named like the array, searched in the
   new exe with relative operands masked (signature_map.relative_mask). A unique match with a small positive shift
   (0 <= new - old < 0x20000) is an anchor (`old`, `new`).
2. The shift is piecewise: `shift(old)` = the shift of the nearest anchors below and above (reported with both).
3. `near(old, bytes)`: the matches of the bytes within +-window of old + shift(old).

  python tools/porting/port_near.py [--exe PATH] [--out FILE]          # anchors + every array's near result
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent))
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from rtti_dump import Pe
from signature_map import ImageScanner, relative_mask, make_pattern

ARRAY = re.compile(r"\b(?:uint8_t|unsigned\s+char|BYTE)\s+(\w+)\s*\[\s*(?:\d+)?\s*\]\s*=\s*\{([^}]+)\}\s*;([^\n]*)", re.S)
CONST = re.compile(r"\b(?:constexpr|const)\s+(?:uintptr_t|uint64_t|uint32_t|size_t)\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+)\s*[;,]")
HEX = re.compile(r"0x([0-9A-Fa-f]{5,8})\b")
CODE_LO, CODE_HI = 0x1000, 0x5600000


def arrays(root=ROOT):
    for path in sorted((root / "src" / "mod").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        text = path.read_text(encoding="utf-8-sig")
        consts = {}
        # every `name = 0x...` of a constexpr declaration, including the later names of a multi-name one
        for line in text.splitlines():
            if re.match(r"\s*(?:inline\s+)?(?:static\s+)?(?:constexpr|const)\s+(?:uintptr_t|uint64_t|uint32_t|size_t)\b", line):
                for c in re.finditer(r"(\w+)\s*=\s*(0x[0-9a-fA-F]+)\b", line):
                    consts.setdefault(c[1], int(c[2], 16))
        for m in ARRAY.finditer(text):
            tokens = [t.strip() for t in m[2].split(",") if t.strip()]
            if not tokens or not all(re.fullmatch(r"0[xX][0-9a-fA-F]+|[0-9]+", t) for t in tokens):
                continue
            data = bytes(int(t, 0) for t in tokens)
            old, how = None, None
            tail = m[3]
            h = HEX.search(tail) if "//" in tail else None
            if h and CODE_LO <= int(h[1], 16) < CODE_HI:
                old, how = int(h[1], 16), "comment"
            if old is None:
                base = re.sub(r"(?:Head|Bytes|Aob|AOB|Sig|Code|B)(?=(_?3527|_?3549)?$)", "", m[1])
                for name in (base, base.replace("Bytes", "")):
                    if name in consts and CODE_LO <= consts[name] < CODE_HI:
                        old, how = consts[name], "constant"
                        break
            yield {"file": path.relative_to(root).as_posix(), "line": text.count("\n", 0, m.start()) + 1,
                   "name": m[1], "data": data, "old": old, "how": how,
                   "tagged_3527": "3527" in m[1] or "3549" in m[1] or "3527" in tail or "3549" in tail}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT.parent / "bin64" / "CrimsonDesert.exe")
    ap.add_argument("--out", type=Path, default=Path(__file__).parent / "data" / "port_near_3576.json")
    ap.add_argument("--window", type=lambda s: int(s, 0), default=0x2000)
    args = ap.parse_args()
    data = args.exe.read_bytes()
    pe = Pe(data)
    scan = ImageScanner(pe, cap=50)
    items = list(arrays())
    anchors = []
    for it in items:
        if it["old"] is None or len(it["data"]) < 10:
            continue
        mask, _, _ = relative_mask(it["data"], it["old"])
        if sum(bool(x) for x in mask) < 10:
            continue
        matches, truncated = scan.scan(it["data"], mask)
        if len(matches) == 1 and not truncated:
            d = matches[0]["rva"] - it["old"]
            if 0 <= d < 0x20000:
                anchors.append((it["old"], matches[0]["rva"], it["file"], it["name"]))
    anchors.sort()
    olds = [a[0] for a in anchors]

    def shift(old):
        i = bisect_right(olds, old)
        lo = anchors[i - 1] if i > 0 else None
        hi = anchors[i] if i < len(anchors) else None
        return (lo[1] - lo[0] if lo else None), (hi[1] - hi[0] if hi else None)

    results = []
    for it in items:
        if it["old"] is None:
            continue
        lo, hi = shift(it["old"])
        # 3527 -> 3576: every anchor shifted by +0x2C0 .. +0xD50 (equal within a function, not monotonic across the
        # image), so the window is fixed around old + 0x1000 (old - 0x1000 .. old + 0x3000 with the default window).
        guesses = [0x1000]
        mask, _, _ = relative_mask(it["data"], it["old"])
        pattern = make_pattern(it["data"], mask)
        hits = set()
        for d in guesses:
            center = it["old"] + d
            off = pe.rva_to_off(max(0, center - args.window))
            end = pe.rva_to_off(center + args.window)
            if off is None or end is None:
                continue
            for m in pattern.finditer(data, off, end):
                rva = center - args.window + (m.start() - off)
                hits.add(rva)
        results.append({"file": it["file"], "line": it["line"], "name": it["name"], "old": hex(it["old"]),
                        "how": it["how"], "len": len(it["data"]), "shift_below": hex(lo) if lo is not None else None,
                        "shift_above": hex(hi) if hi is not None else None,
                        "near": [hex(h) for h in sorted(hits)],
                        "status": "unique" if len(hits) == 1 else ("none" if not hits else "several")})
    out = {"exe": str(args.exe), "timestamp": hex(pe.timestamp), "anchor_count": len(anchors),
           "anchors": [{"old": hex(a[0]), "new": hex(a[1]), "shift": hex(a[1] - a[0]), "file": a[2], "name": a[3]}
                       for a in anchors],
           "results": results}
    args.out.write_text(json.dumps(out, indent=1), encoding="utf-8")
    from collections import Counter
    print(json.dumps({"anchors": len(anchors), "results": dict(Counter(r["status"] for r in results)),
                      "out": str(args.out)}, indent=1))


if __name__ == "__main__":
    main()
