"""regw.py from_rva to_rva reg[,reg...]: every instruction in [from, to) of the .pdata function that WRITES one
of the registers (capstone regs_write, sub-registers included), and every branch into (from, to] from outside
that range (a path that could skip the defining instruction)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
from capstone.x86 import *
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
a0, a1 = int(sys.argv[1], 16), int(sys.argv[2], 16)
fam = {'rdi': {'rdi', 'edi', 'di', 'dil'}, 'rsi': {'rsi', 'esi', 'si', 'sil'}, 'rbx': {'rbx', 'ebx', 'bx', 'bl'},
       'rbp': {'rbp', 'ebp', 'bp', 'bpl'}, 'r12': {'r12', 'r12d', 'r12w', 'r12b'}, 'r13': {'r13', 'r13d', 'r13w', 'r13b'},
       'r14': {'r14', 'r14d', 'r14w', 'r14b'}, 'r15': {'r15', 'r15d', 'r15w', 'r15b'}}
regs = set()
for r in sys.argv[3].split(','): regs |= fam[r]
f = D.func_whole(a0)
ins_all = list(md.disasm(D.rd(f[0], f[1] - f[0]), 0x140000000 + f[0]))
for ins in ins_all:
    a = ins.address - 0x140000000
    if a0 <= a < a1:
        _, w = ins.regs_access()
        names = {ins.reg_name(x) for x in w}
        if names & regs: print(f"WRITE {a:08X} {ins.mnemonic} {ins.op_str}")
    if ins.group(X86_GRP_JUMP):
        for op in ins.operands:
            if op.type == X86_OP_IMM:
                t = op.imm - 0x140000000
                if a0 < t <= a1 and not (a0 <= a < a1): print(f"BRANCH INTO from {a:08X} -> {t:08X}")
print("done", f"{f[0]:08X}-{f[1]:08X}")
