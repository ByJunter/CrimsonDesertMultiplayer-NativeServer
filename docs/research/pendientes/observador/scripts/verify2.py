"""verify2.py rva:patch[:nbytes] ...
For each hook point: section, .pdata function, the whole instructions in the patch (from the function start),
instruction boundary at rva and rva+patch, branches of the function into (rva, rva+patch), RIP-relative operands,
and every rel32 (E8/E9/0F8x) of .rsrc/.xtls/.text into it, each source re-checked as a real instruction start
(capstone over its .pdata function; .xtls has no unwind data for most code: reported as UNVERIFIED)."""
import sys, os, re, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
from capstone.x86 import *
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
md0 = Cs(CS_ARCH_X86, CS_MODE_64)
pts = []
for a in sys.argv[1:]:
    p = a.split(':')
    pts.append((int(p[0], 16), int(p[1]), int(p[2]) if len(p) > 2 else 16))
rel = {}
for name, va, vs, ro, rs in D.secs:
    if name not in ('.rsrc', '.xtls', '.text'): continue
    data = D.mm[ro:ro + min(rs, vs if vs else rs)]
    for m in re.finditer(b'\xE8|\xE9|\x0F[\x80-\x8F]', data):
        o = m.start(); L = len(m.group(0))
        if o + L + 4 > len(data): continue
        t = va + o + L + 4 + struct.unpack_from('<i', data, o + L)[0]
        rel.setdefault(t, []).append((name, va + o))
def real_ins(src):
    f = D.func_whole(src)
    if not f: return None
    for ins in md0.disasm(D.rd(f[0], f[1] - f[0]), 0x140000000 + f[0]):
        a = ins.address - 0x140000000
        if a == src: return f"{ins.mnemonic} {ins.op_str}"
        if a > src: return False
    return False
ok_all = True
for rva, patch, n in pts:
    s = D.sec(rva); f = D.func_whole(rva)
    print(f"== {rva:08X} patch {patch} section {s[0]} func {f[0]:08X}-{f[1]:08X}")
    bounds = set(); tg = []; riprel = False
    for ins in md.disasm(D.rd(f[0], f[1] - f[0]), 0x140000000 + f[0]):
        a = ins.address - 0x140000000; bounds.add(a)
        if ins.group(X86_GRP_JUMP) or ins.group(X86_GRP_CALL):
            for op in ins.operands:
                if op.type == X86_OP_IMM: tg.append((a, op.imm - 0x140000000))
        if rva <= a < rva + patch:
            rr = 'rip' in ins.op_str
            riprel |= rr
            print(f"   {a:08X} {ins.bytes.hex(' '):<30} {ins.mnemonic} {ins.op_str}" + ("  RIP-REL!" if rr else ''))
    b1, b2 = rva in bounds, (rva + patch) in bounds
    bad = [(hex(a), hex(t)) for a, t in tg if rva < t < rva + patch]
    real = []
    for t, v in rel.items():
        if rva < t < rva + patch:
            for name, src in v:
                r = real_ins(src) if name == '.rsrc' else 'UNVERIFIED (.xtls/.text)'
                if r: real.append((hex(src), name, r))
    print(f"   boundary rva {b1} rva+patch {b2} | branches into {bad} | real rel32 into {real} | rip-rel {riprel}")
    good = s[0] == '.rsrc' and b1 and b2 and not bad and not real and not riprel and patch >= 5
    ok_all &= good
    print("   VERDICT", "OK" if good else "REFUSE", "| expected:", D.rd(rva, n).hex(' ').upper())
print("ALL OK" if ok_all else "SOME REFUSED")
