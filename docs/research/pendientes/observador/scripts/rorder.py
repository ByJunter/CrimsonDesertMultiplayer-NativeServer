"""rorder.py handler_rva [len]: the stream reads of a Troc handler in address order: every call (direct
0x141F270 or indirect call r9/[rax+8]) preceded by `mov r8d, N` and `lea rdx, [...]`, plus calls to other functions
(sub-deserializers) so loops and nested structs are visible."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
md = Cs(CS_ARCH_X86, CS_MODE_64)
rva = int(sys.argv[1], 16)
f = D.func_of(rva)
end = f[1] if len(sys.argv) < 3 else rva + int(sys.argv[2], 16)
last_lea = None; last_r8 = None
for ins in md.disasm(D.rd(rva, end - rva), 0x140000000 + rva):
    a = ins.address - 0x140000000
    if ins.mnemonic == 'lea' and ins.op_str.startswith('rdx,'): last_lea = (a, ins.op_str[5:])
    if ins.mnemonic == 'mov' and ins.op_str.startswith('r8d,'): last_r8 = ins.op_str[5:]
    if ins.mnemonic == 'call':
        t = ins.op_str
        if t in ('r9', 'qword ptr [rax + 8]') or t == '0x14141f270':
            print(f"{a:08X} READ {last_r8} -> {last_lea[1] if last_lea else '?'}")
        elif t.startswith('0x'):
            print(f"{a:08X} call {int(t,16)-0x140000000:#x} (rdx {last_lea[1] if last_lea else '?'})")
        else:
            print(f"{a:08X} call {t}")
    if ins.mnemonic in ('jb', 'jl', 'jne') and ins.op_str.startswith('0x') and int(ins.op_str, 16) - 0x140000000 < a:
        print(f"{a:08X} LOOP back to {int(ins.op_str,16)-0x140000000:#x}")
