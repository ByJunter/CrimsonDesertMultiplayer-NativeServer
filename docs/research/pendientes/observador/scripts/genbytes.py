"""genbytes.py name:rva:len ... -> C++ constexpr arrays + the disassembly of each range (whole instructions check)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
md = Cs(CS_ARCH_X86, CS_MODE_64)
for a in sys.argv[1:]:
    name, rva, n = a.split(':'); rva = int(rva, 16); n = int(n)
    b = D.rd(rva, n)
    ins = list(md.disasm(b, 0x140000000 + rva))
    covered = sum(i.size for i in ins)
    asm = ' ; '.join(f"{i.mnemonic} {i.op_str}" for i in ins)
    print(f"// 0x{rva:X} {asm}{'' if covered == n else f'  (+{n - covered} bytes of the next instruction)'}")
    print(f"constexpr uintptr_t k{name} = 0x{rva:X};")
    print(f"constexpr uint8_t k{name}Bytes[] = {{" + ', '.join(f"0x{x:02X}" for x in b) + "};")
