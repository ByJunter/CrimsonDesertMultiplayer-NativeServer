#!/usr/bin/env python3
"""Recover the reader's globals for build 3527 from documented code references.

This records static evidence only. It never writes to or starts the game and
does not claim that unchanged member offsets have been validated at runtime.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/scripts"))
from aob_scan_file import compile_pattern
from rtti_dump import Pe


def hx(n):
    return f"0x{n:X}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT.parent / "bin64/CrimsonDesert.exe")
    parser.add_argument("--out", type=Path, default=Path(__file__).parent / "data/core_globals_3527.json")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    pe = Pe(data)
    # 3527 (0x6AEA07A6, the original target) and 3576 (0x6AED2E85, 2026-11-06): the same patterns; "old" is 3527's.
    if pe.timestamp not in (0x6AEA07A6, 0x6AED2E85):
        raise ValueError("This evidence extractor targets timestamps 0x6AEA07A6 (3527) and 0x6AED2E85 (3576)")
    patterns = {}
    doc = ROOT / "docs/research/cheat-tables-versions.md"
    inside = False
    for line in doc.read_text(encoding="utf-8").splitlines():
        if line.strip() == "```sigs":
            inside = True
        elif line.strip() == "```":
            inside = False
        elif inside and "|" in line:
            name, pattern, *_ = (item.strip() for item in line.split("|"))
            patterns[name] = pattern
    selected = {
        "kWorldSystemSlot": (0x6D69190, [("world_system_ref", 3, 7), ("world_system_ref_p3", 3, 7)]),
        "kStaticPlayerPos": (0x6D77858, [("player_static_pos_write", 4, 8)]),
        "kStaticPlayerPosZ": (0x6D77860, [("player_static_pos_write", 11, 15)]),
        "kChunkOrigin": (0x6D56500, [("world_offset_sub", 25, 29)]),
        "kCameraSlot": (0x6C8CF30, [("camera_global_ref", 3, 7)]),
    }

    def unique(name):
        matches = []
        for sec in pe.sections:
            # Ordinary native code; exclude writable executable protection code.
            if not sec.exec or sec.flags & 0x80000000:
                continue
            pattern = compile_pattern(patterns[name])
            for match in pattern.finditer(data, sec.raw_ptr, sec.raw_ptr + sec.raw_size):
                matches.append((sec.va + match.start() - sec.raw_ptr, match.start()))
        if len(matches) != 1:
            raise ValueError(f"{name}: expected a unique native-code match, got {len(matches)}")
        return matches[0]

    rows = []
    for symbol, (old, references) in selected.items():
        evidence = []
        for name, disp, end in references:
            site, off = unique(name)
            target = site + end + struct.unpack_from("<i", data, off + disp)[0]
            section = pe.section_of(target)
            if not section or section.exec or not section.flags & 0x80000000:
                raise ValueError(f"{symbol}: target {hx(target)} is not writable data")
            evidence.append({"pattern": name, "site": hx(site), "target": hx(target),
                             "disp_offset": disp, "instruction_end": end,
                             "bytes": data[off:off + end].hex(" ")})
        if len({item["target"] for item in evidence}) != 1:
            raise ValueError(f"{symbol}: independent references disagree")
        rows.append({"symbol": symbol, "old": hx(old), "new": evidence[0]["target"],
                     "evidence": evidence, "runtime_validated": False})

    slots = [unique(name)[0] for name in ("camera_vtable_slot1", "camera_vtable_slot2")]
    needle = struct.pack("<QQ", *(pe.image_base + slot for slot in slots))
    camera_vtables = []
    for sec in pe.sections:
        if sec.exec or sec.flags & 0x80000000:
            continue
        start, end = sec.raw_ptr, sec.raw_ptr + sec.raw_size
        at = data.find(needle, start, end)
        while at >= 0:
            table = sec.va + at - sec.raw_ptr - 8
            if table % 8 == 0:
                camera_vtables.append(table)
            at = data.find(needle, at + 1, end)
    if len(camera_vtables) != 1:
        raise ValueError(f"Camera vtable: {len(camera_vtables)} candidates")
    rows.append({"symbol": "kVtCamera", "old": hx(0x5D20718), "new": hx(camera_vtables[0]),
                 "evidence": {"unique_virtual_slots_1_2": [hx(slot) for slot in slots]},
                 "runtime_validated": False})

    rtti = json.loads((Path(__file__).parent / ("data/rtti_3576.json" if pe.timestamp == 0x6AED2E85 else "data/rtti_3527.json")).read_text(encoding="utf-8"))
    names = {
        "kVtClientActorManager": (0x55B62C8, "ClientActorManager"),
        "kVtControlledActor": (0x5585C40, "ClientChildOnlyInGameActor"),
        "kVtCharacterControl": (0x55B5460, "ClientCharacterControlActorComponent"),
        "kVtStatusComponent": (0x558D868, "ClientStatusActorComponent"),
    }
    unresolved = []
    for symbol, (old, name) in names.items():
        classes = [(m, c) for m, c in rtti["classes"].items()
                   if m == f".?AV{name}@pa@@" or c["name"] in (name, "pa::" + name)]
        candidates = [(m, v) for m, c in classes for v in c["vtables"] if v["offset"] == 0 and v["rva"]]
        if len(candidates) != 1:
            unresolved.append({"symbol": symbol, "class": name, "candidates": candidates})
            continue
        mangled, vtable = candidates[0]
        rows.append({"symbol": symbol, "old": hx(old), "new": vtable["rva"],
                     "evidence": {"primary_rtti_class": mangled}, "runtime_validated": False})
    out = {"timestamp": hx(pe.timestamp), "sha256": hashlib.sha256(data).hexdigest(),
           "status": "static evidence; object member offsets still require runtime verification",
           "mappings": rows, "unresolved": unresolved}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps({"mappings": [{k: row[k] for k in ("symbol", "old", "new")} for row in rows],
                      "unresolved": unresolved}, indent=2))


if __name__ == "__main__":
    main()
