#!/usr/bin/env python3
"""Read-only source-byte signature candidates for a new Crimson Desert build.

The report is evidence, not an address patch. A unique prologue is not proof of
function identity or unchanged ABI. Only relative instruction operands are
wildcarded; structure offsets, registers and ordinary immediates remain exact.
Uses the PE exception directory for function ranges (not section names).
"""
from __future__ import annotations

import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

import capstone
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "scripts"))
from rtti_dump import Pe

CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
CS.detail = True
ARRAY = re.compile(r"\b(?:uint8_t|unsigned\s+char|BYTE)\s+(\w+)\s*\[\s*(?:\d+)?\s*\]\s*=\s*\{([^}]+)\}", re.S)
CONSTANT = re.compile(r"\b(?:constexpr|const)\s+(?:uintptr_t|uint64_t|uint32_t|size_t)\s+(\w+)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*;")
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)


def strip_comments(text):
    return COMMENT.sub(lambda m: "\n" * m[0].count("\n"), text)


def source_signatures(root=ROOT):
    """Yield literal byte arrays and conservative same-file name associations."""
    for path in sorted((root / "src" / "mod").rglob("*")):
        if path.suffix not in (".h", ".cpp"):
            continue
        raw = path.read_text(encoding="utf-8-sig")
        text = strip_comments(raw)
        declarations = defaultdict(set)
        for declaration in CONSTANT.finditer(text):
            declarations[declaration[1]].add(int(declaration[2], 0))
        # Names can repeat in different local/namespace scopes. Do not silently
        # bind a byte array to whichever declaration happened to appear last.
        constants = {name:next(iter(values)) for name,values in declarations.items() if len(values) == 1}
        for m in ARRAY.finditer(text):
            tokens = [x.strip() for x in m[2].split(",") if x.strip()]
            if not tokens or not all(re.fullmatch(r"(?:0[xX][0-9a-fA-F]+|[0-9]+)", t) for t in tokens):
                continue
            values = [int(t, 0) for t in tokens]
            if not all(0 <= v <= 255 for v in values):
                continue
            base_name = re.sub(r"(?:Head|Bytes|Aob|AOB|Sig|Code)$", "", m[1])
            old = constants.get(base_name)
            if old is not None and not 0x10000 <= old < 0x20000000:
                old = None
            yield {
                "file": path.relative_to(root).as_posix(),
                "line": text.count("\n", 0, m.start()) + 1,
                "name": m[1], "symbol": base_name if old else None,
                "old_rva": old, "data": bytes(values),
            }


def relative_mask(data, address=0):
    """Mask RIP-relative memory displacements and relative call/jump operands."""
    mask = bytearray(b"\xff" * len(data))
    records = []
    decoded = 0
    for insn in CS.disasm(data, address):
        at = insn.address - address
        decoded = at + insn.size
        spans = []
        if any(op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP for op in insn.operands):
            spans.append((insn.disp_offset, insn.disp_size, "rip_relative"))
        if (insn.group(capstone.CS_GRP_JUMP) or insn.group(capstone.CS_GRP_CALL)) and insn.imm_size:
            spans.append((insn.imm_offset, insn.imm_size, "relative_branch"))
        for offset, size, kind in spans:
            mask[at + offset:at + offset + size] = b"\0" * size
            records.append({"offset": at + offset, "size": size, "kind": kind})
    return bytes(mask), records, decoded


def make_pattern(data, mask):
    return re.compile(b"".join(re.escape(bytes([v])) if keep else b"." for v, keep in zip(data, mask)), re.S)


def exception_ranges(pe):
    nt = struct.unpack_from("<I", pe.data, 0x3c)[0]
    rva, size = struct.unpack_from("<II", pe.data, nt + 24 + 112 + 3 * 8)
    off = pe.rva_to_off(rva)
    if off is None:
        return []
    result = []
    for at in range(off, off + size - 11, 12):
        begin, end, unwind = struct.unpack_from("<III", pe.data, at)
        if begin < end and pe.is_exec_rva(begin):
            result.append((begin, end, unwind))
    return sorted(result)


class ImageScanner:
    def __init__(self, pe, cap=1000):
        self.pe, self.cap = pe, cap
        # Code classified by PE flags. RWX virtualized/packed sections need
        # separate live evidence and are deliberately excluded from this scan.
        self.regions = [(s, pe.data[s.raw_ptr:s.raw_ptr+s.raw_size]) for s in pe.sections
                        if s.exec and not s.flags & 0x80000000]
        self.functions = exception_ranges(pe)
        self.starts = [f[0] for f in self.functions]
        self.cache = {}

    def function(self, rva):
        i = bisect_right(self.starts, rva) - 1
        if i >= 0 and self.functions[i][0] <= rva < self.functions[i][1]:
            a, b, unwind = self.functions[i]
            return {"begin": a, "end": b, "unwind": unwind, "offset": rva-a}
        return None

    def scan(self, data, mask=None):
        key = (data, mask)
        if key in self.cache:
            return self.cache[key]
        results = []
        pattern = make_pattern(data, mask) if mask is not None else None
        for section, region in self.regions:
            if pattern:
                def pattern_finds():
                    pos = 0
                    while match := pattern.search(region, pos):
                        yield match.start()
                        pos = match.start() + 1
                positions = pattern_finds()
            else:
                def finds():
                    at = region.find(data)
                    while at >= 0:
                        yield at
                        at = region.find(data, at+1)
                positions = finds()
            for at in positions:
                rva = section.va + at
                results.append({"rva": rva, "section": section.name, "function": self.function(rva)})
                if len(results) >= self.cap:
                    self.cache[key] = results, True
                    return results, True
        self.cache[key] = results, False
        return results, False


def run(exe, out, cap=1000):
    data = exe.read_bytes()
    pe = Pe(data)
    scanner = ImageScanner(pe, cap)
    records = []
    for index, entry in enumerate(source_signatures()):
        source = entry.pop("data")
        mask, fields, decoded = relative_mask(source, entry["old_rva"] or 0)
        entry.update(length=len(source), bytes=source.hex(" "), relative_fields=fields,
                     decoded_length=decoded, retained_bytes=sum(bool(x) for x in mask))
        if len(source) < 8:
            entry.update(status="too_short", matches=[], truncated=False, method=None)
            records.append(entry)
            continue
        matches, truncated = scanner.scan(source)
        method = "exact"
        if not matches and fields and entry["retained_bytes"] >= 8:
            matches, truncated = scanner.scan(source, mask)
            method = "relative_masked"
        entry.update(method=method, matches=matches, truncated=truncated)
        if len(matches) == 1:
            entry["status"] = "unique_candidate"
            entry["evidence_strength"] = "long" if entry["retained_bytes"] >= 24 else "short"
        elif matches:
            entry["status"] = "ambiguous"
        else:
            entry["status"] = "not_found"
        records.append(entry)
        if index % 100 == 0:
            print(f"Scanned {index+1} arrays", flush=True)

    # Consolidate signatures that independently name the same old address.
    # A full signature can resolve a repeated shortened prologue, but capped
    # candidate lists are not treated as exhaustive negative evidence.
    by_old = defaultdict(list)
    for r in records:
        if r["old_rva"] and r["matches"] and not r["truncated"]:
            by_old[r["old_rva"]].append(r)
    mappings = []
    for old, evidence in sorted(by_old.items()):
        common = set.intersection(*(set(m["rva"] for m in r["matches"]) for r in evidence))
        if len(common) == 1:
            new = next(iter(common))
            mappings.append({"old_rva": old, "new_rva": new, "verified": False,
                             "reason": "signature_candidate_requires_semantic_and_ABI_validation",
                             "function": scanner.function(new),
                             "evidence": [{"file": r["file"], "name": r["name"], "method": r["method"],
                                           "length": r["length"], "retained_bytes": r["retained_bytes"]}
                                          for r in evidence]})
    result = {
        "exe": str(exe.resolve()), "sha256": hashlib.sha256(data).hexdigest(),
        "timestamp": pe.timestamp, "image_base": pe.image_base,
        "policy": "Read-only candidates. No ABI/identity claim; no automatic runtime patches. RWX sections excluded.",
        "function_count": len(scanner.functions),
        "summary": dict(Counter(r["status"] for r in records)),
        "candidate_mapping_count": len(mappings), "mappings": mappings, "signatures": records,
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"summary": result["summary"], "mappings": len(mappings), "output": str(out)}, indent=2))
    return result


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT.parent / "bin64" / "CrimsonDesert.exe")
    ap.add_argument("--out", type=Path, default=Path(__file__).parent / "data" / "signature_mapping_2976_3527.json")
    ap.add_argument("--cap", type=int, default=1000)
    args = ap.parse_args()
    run(args.exe, args.out, args.cap)
