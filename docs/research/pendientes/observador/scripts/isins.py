import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
md = Cs(CS_ARCH_X86, CS_MODE_64)
# Is each given rva a real instruction start inside its .pdata function? Print the instruction.
for a in sys.argv[1:]:
    r = int(a, 16)
    f = D.func_of(r)
    if not f:
        print(f"{r:08X}: no .pdata function (data or padding?)")
        continue
    code = D.rd(f[0], f[1] - f[0])
    hit = None
    for ins in md.disasm(code, 0x140000000 + f[0]):
        if ins.address - 0x140000000 == r:
            hit = ins
            break
        if ins.address - 0x140000000 > r: break
    print(f"{r:08X} func {f[0]:08X}-{f[1]:08X}:", f"{hit.mnemonic} {hit.op_str}" if hit else "NOT an instruction start (false rel32)")
