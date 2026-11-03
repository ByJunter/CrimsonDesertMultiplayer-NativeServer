# S46 phase 1: offline check of the probe points (build 2976). For each point: the .pdata function that contains it,
# whole instructions over the patch, relative operands inside the patch, a 40-byte signature at the point and its
# number of matches in the whole file and in .rsrc, and any branch (rel32 / rel8) or absolute pointer whose target
# falls strictly inside the patch (start excluded), over every section (raw byte scan: over-approximates).
import sys, struct, re
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe, capstone
from capstone import x86
pe = cdpe.PE(); B = cdpe.BASE; D = pe.data
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
PTS = [("P1", 0x21F2C45, 5), ("P1b", 0x21F2917, 7), ("U2-c", 0x21F28A1, 7), ("P2", 0x2369862, 5),
       ("P2a", 0x2369695, 7), ("P5", 0x2B36FB3, 7), ("P6", 0x2B3708C, 6)]
cdpe.func_of(pe, 0x1000)
rsrc = [s for s in pe.sections if s[0] == ".rsrc"][0]
def count(sig, lo=0, hi=None):
    n = 0; j = D.find(sig, lo)
    while j != -1 and (hi is None or j < hi):
        n += 1; j = D.find(sig, j + 1)
    return n
for name, rva, patch in PTS:
    f = cdpe.func_of(pe, rva)
    end = [e for b, e, u in cdpe._pd if b == f]
    print(f"== {name} {rva:#x} patch {patch} | section {pe.secname(rva)} | pdata func {f:#x}-{end[0] if end else 0:#x}")
    off = 0; rel = []
    for ins in md.disasm(pe.read(rva, 32), B + rva):
        if off >= patch: break
        isrel = any(op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_RIP for op in ins.operands) or \
                ((ins.group(capstone.CS_GRP_JUMP) or ins.group(capstone.CS_GRP_CALL)) and
                 any(op.type == x86.X86_OP_IMM for op in ins.operands))
        print(f"   {ins.address-B:#x} {ins.bytes.hex(' '):24s} {ins.mnemonic} {ins.op_str}{'   <- RELATIVE' if isrel else ''}")
        off += ins.size
    print(f"   whole instructions: {off == patch}")
    sig = pe.read(rva, 40)
    o = pe.off(rva)
    print(f"   sig40 {sig.hex(' ')}\n   matches: file {count(sig)}, .rsrc {count(sig, rsrc[3], rsrc[3] + rsrc[4])}")
    hits = 0
    s, e = rva, rva + patch
    for n, va, vs, ro, rs, fl in pe.sections:
        seg = D[ro:ro + rs]
        for m in re.finditer(rb'[\xE8\xE9]|\x0F[\x80-\x8F]', seg):
            q = m.end()
            if q + 4 > len(seg): continue
            t = va + q + 4 + struct.unpack_from('<i', seg, q)[0]
            if s < t < e: print(f"   {n} rel32 at {va+m.start():#x} -> {t:#x}"); hits += 1
        if va <= s < va + rs:
            for p in range(max(0, s - 140 - va), min(rs - 1, e + 140 - va)):
                if seg[p] == 0xEB or 0x70 <= seg[p] <= 0x7F:
                    t = va + p + 2 + struct.unpack_from('<b', seg, p + 1)[0]
                    if s < t < e: print(f"   {n} rel8 at {va+p:#x} -> {t:#x}"); hits += 1
    for t in range(s + 1, e):
        pat = struct.pack('<Q', B + t); j = D.find(pat)
        while j != -1: print(f"   abs ptr at file {j:#x} -> {t:#x}"); hits += 1; j = D.find(pat, j + 1)
    # branches of the containing function landing anywhere in [s, e] (decoded)
    fb = f; fe = end[0] if end else rva + 0x400
    into = [(i.address - B, int(i.op_str, 16) - B) for i in md.disasm(pe.read(fb, fe - fb), B + fb)
            if (i.group(capstone.CS_GRP_JUMP)) and i.op_str.startswith('0x') and s <= int(i.op_str, 16) - B <= e]
    print(f"   raw hits strictly inside: {hits} | decoded jumps of the function to [{s:#x},{e:#x}]: {[(hex(a), hex(b)) for a, b in into]}")
