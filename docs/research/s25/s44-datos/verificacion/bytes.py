import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
for a in sys.argv[1:]:
    r, n = a.split(':')
    r = int(r, 16); n = int(n)
    b = pe.rd(r, n)
    print(f"{r:#x} ({n}): " + ', '.join(f"0x{x:02X}" for x in b))
    for i in pe.dis(r, r + n):
        print(f"      {i.address - pe.BASE:#x} {i.mnemonic} {i.op_str}")
