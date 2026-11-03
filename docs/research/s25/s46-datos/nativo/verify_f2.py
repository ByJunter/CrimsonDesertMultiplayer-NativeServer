# Run with `python -I` (re.py next to it shadows the standard module otherwise).
# S46 phase 2: offline check (build 2976, read only on the exe) of what R0 and U2-c rely on.
#  1. every context block of shop_trade_probe.cpp (kCtx*) byte for byte, and that none overlaps a patch;
#  2. P2 0x2369862: the registers after the point up to the erase call and after it (rax / rdx dead), the lock
#     taken before and released after, no branch into [0x2369845, 0x236987C) but from the function's own entry
#     (the je at 0x23695AE to 0x2369845), the callee of 0x21245B0;
#  3. U2-c 0x21F28A1: every use of rbx / [rsp+0x38] / [rsp+0x28] / [rsp+0x30] after the point;
#  4. the native erase decompile's move (plain copies of the 8-byte handles).
import sys, re
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe, capstone
from capstone import x86
pe = cdpe.PE(); B = cdpe.BASE
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64); md.detail = True
src = open(sys.argv[1], encoding="utf-8").read()

def arr(name):
    m = re.search(r"constexpr uint8_t " + name + r"\[\] = \{([^}]*)\}", src)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))
ok = True
ctx = re.findall(r"\{(0x[0-9A-F]+), (k[A-Za-z0-9]+)\}", src)
patches = [(0x21F2C45, 5), (0x21F2917, 7), (0x21F28A1, 7), (0x2369862, 5), (0x2369695, 7), (0x2B36FB3, 7), (0x2B3708C, 6)]
seen = set()
for rva_s, name in ctx:
    if (rva_s, name) in seen: continue
    seen.add((rva_s, name))
    rva = int(rva_s, 16); b = arr(name)
    good = pe.read(rva, len(b)) == b
    over = [hex(p) for p, n in patches if rva < p + n and p < rva + len(b)]
    print(f"ctx {name:14s} {rva:#x} {len(b):3d} B: {'OK' if good else 'MISMATCH'}{'  OVERLAPS patch ' + str(over) if over else ''}")
    ok &= good and not over

def dis(a, n):
    out = []
    for ins in md.disasm(pe.read(a, n * 16), B + a):
        out.append(ins)
        if len(out) >= n: break
    return out

print("\n== P2 region 0x2369845 .. 0x23698C6")
for ins in dis(0x2369845, 30):
    regs_w = [ins.reg_name(r) for r in ins.regs_access()[1]]
    regs_r = [ins.reg_name(r) for r in ins.regs_access()[0]]
    print(f"  {ins.address-B:#x}: {ins.mnemonic:6s} {ins.op_str:36s} reads {regs_r} writes {regs_w}")
    if ins.address - B >= 0x23698BC: break

print("\n== branches of 0x2369360 into [0x2369846, 0x236987C)")
f0, f1 = 0x2369360, 0x23698E7
for ins in md.disasm(pe.read(f0, f1 - f0), B + f0):
    if ins.group(capstone.CS_GRP_JUMP) and ins.operands and ins.operands[0].type == x86.X86_OP_IMM:
        t = ins.operands[0].imm - B
        if 0x2369845 <= t < 0x236987C:
            print(f"  {ins.address-B:#x} {ins.mnemonic} -> {t:#x}")

print("\n== 0x21245B0:", [f"{i.mnemonic} {i.op_str}" for i in dis(0x21245B0, 1)])

print("\n== U2-c: uses of rbx, [rsp+0x38], [rsp+0x28], [rsp+0x30] in 0x21F28A1 .. 0x21F2F2E")
f0, f1 = 0x21F28A1, 0x21F2F2E
for ins in md.disasm(pe.read(f0, f1 - f0), B + f0):
    t = ins.op_str
    if "rbx" in t or "rsp + 0x38]" in t or "rsp + 0x28]" in t or "rsp + 0x30]" in t:
        print(f"  {ins.address-B:#x}: {ins.mnemonic} {t}")
print("\nALL CONTEXT OK" if ok else "\nCONTEXT PROBLEM")
