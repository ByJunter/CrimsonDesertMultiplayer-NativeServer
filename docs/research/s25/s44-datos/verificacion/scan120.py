# Find every .rsrc function that walks a stage's actor list (+0x120 ptr / +0x128 count, 0x28-byte entries) and
# list its byte reads at +0xC (the binding type). Read only.
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_IMM
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
rs = [s for s in pe.SECS if s[0] == '.rsrc'][0]
lo, hi = rs[1], rs[1] + rs[2]
funcs = [f for f in pe.FUNCS if lo <= f[0] < hi and f[1] - f[0] < 0x8000]
# quick byte prefilter: disp32 0x128 or disp8 can't be 0x128 (> 0x7F) -> needs 28 01 00 00 in the bytes
pat = b'\x28\x01\x00\x00'
pat2 = b'\x20\x01\x00\x00'
out = []
for b, e in funcs:
    code = pe._d[pe.off(b):pe.off(b) + (e - b)]
    if pat not in code or pat2 not in code:
        continue
    has128 = has120 = has5 = False
    reads = []
    for i in md.disasm(code, pe.BASE + b):
        for op in i.operands:
            if op.type == CS_OP_MEM:
                if op.mem.disp == 0x128: has128 = True
                if op.mem.disp == 0x120: has120 = True
                if op.mem.disp == 0xC and op.size == 1:
                    reads.append(f"{i.address - pe.BASE:#x} {i.mnemonic} {i.op_str}")
        if i.mnemonic == 'lea' and '*4]' in i.op_str:
            has5 = True
    if has128 and has120 and has5:
        out.append((b, e, reads))
for b, e, reads in out:
    print(f"== {b:#x}-{e:#x} byte+0xC reads: {len(reads)}")
    for r in reads[:20]:
        print('    ', r)
print('functions', len(out))
