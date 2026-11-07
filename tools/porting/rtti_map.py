#!/usr/bin/env python3
"""Map named RTTI and static Troc handlers between documented game builds.

Reads the game executable without launching or modifying it. Requires an RTTI
dump from tools/scripts/rtti_dump.py and the old named Troc CSV. Output contains
evidence only; it does not change any runtime addresses or accept a new build.
"""
from __future__ import annotations

import argparse
from array import array
import csv
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from rtti_dump import Pe


def hx(value):
    return None if value is None else f"0x{value:X}"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT.parent / "bin64" / "CrimsonDesert.exe")
    ap.add_argument("--rtti", type=Path, default=Path(__file__).parent / "data" / "rtti_3527.json")
    ap.add_argument("--old-trocs", type=Path, default=ROOT / "docs" / "research" / "data" / "troc_messages_2976.csv")
    ap.add_argument("--out", type=Path, default=Path(__file__).parent / "data" / "rtti_mapping_2976_3527.json")
    args = ap.parse_args()
    pe = Pe(args.exe.read_bytes())
    rtti = json.loads(args.rtti.read_text(encoding="utf-8"))
    if int(rtti["timestamp"], 16) != pe.timestamp:
        raise ValueError("RTTI dump and executable timestamp mismatch")
    old = {row["name"]: row for row in csv.DictReader(args.old_trocs.open(encoding="utf-8-sig"))}
    tables = {}
    named_tables = {}
    for mangled, cls in rtti["classes"].items():
        named_tables[cls["name"]] = cls["vtables"]
        if not mangled.startswith(".?AVTrocTr"):
            continue
        name = mangled.removeprefix(".?AVTrocTr").split("@")[0]
        for vt in cls["vtables"]:
            if vt["rva"] and vt["offset"] == 0:
                tables[int(vt["rva"], 16)] = (name, vt)
    table_vas = {pe.image_base + address: address for address in tables}
    instances = {}
    for section in pe.sections:
        if section.exec or not section.flags & 0x80000000:
            continue
        words = array("Q")
        words.frombytes(pe.data[section.raw_ptr:section.raw_ptr + section.raw_size // 8 * 8])
        if sys.byteorder != "little":
            words.byteswap()
        for index, value in enumerate(words):
            if value not in table_vas:
                continue
            rva = section.va + index * 8
            offset = pe.rva_to_off(rva)
            if offset is None or offset + 0x90 > len(pe.data):
                continue
            kind, opcode = struct.unpack_from("<IH", pe.data, offset + 8)
            min_size, max_size = struct.unpack_from("<HH", pe.data, offset + 0x18)
            instances.setdefault(table_vas[value], []).append({
                "rva": hx(rva), "kind": kind, "opcode": opcode,
                "min_size": min_size, "max_size": max_size,
                "evidence": "on-disk aligned vtable pointer and instance fields",
            })
    messages = []
    mappings = []
    for address, (name, vt) in sorted(tables.items(), key=lambda kv: kv[1][0]):
        slots = [pe.u64(address + 8 * index) for index in range(min(vt["length"], 3))]
        funcs = [v - pe.image_base if v is not None else None for v in slots]
        previous = old.get(name)
        row = {
            "name": name, "vtable": hx(address), "vtable_length": vt["length"],
            "functions": [hx(v) for v in funcs], "instances": instances.get(address, []),
            "old": previous,
        }
        messages.append(row)
        if previous:
            mappings.append({"old": previous["vtable"], "new": hx(address), "kind": "vtable", "name": name, "evidence": "same RTTI class, primary vtable"})
            for index, key in [(1, "on_receive_slot1"), (2, "handler_slot2")]:
                if len(funcs) > index:
                    mappings.append({"old": previous[key], "new": hx(funcs[index]), "kind": "function", "name": name, "slot": index, "evidence": "same RTTI class and virtual slot; ABI/body requires independent verification"})
    out = {
        "source_build": "1.0.0.2976", "target_build": "1.0.0.3527",
        "exe": str(args.exe.resolve()), "sha256": hashlib.sha256(pe.data).hexdigest(),
        "timestamp": hx(pe.timestamp), "image_base": hx(pe.image_base),
        "stats": {"old_trocs": len(old), "new_trocs": len(messages), "matched_trocs": sum(m["old"] is not None for m in messages), "instances": sum(len(v) for v in instances.values())},
        "messages": messages, "address_candidates": mappings,
        "missing_old_names": sorted(set(old) - {m["name"] for m in messages}),
        "new_names": sorted({m["name"] for m in messages} - set(old)),
        "named_vtables": named_tables,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2), encoding="utf-8")
    print(json.dumps(out["stats"], indent=2))
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
