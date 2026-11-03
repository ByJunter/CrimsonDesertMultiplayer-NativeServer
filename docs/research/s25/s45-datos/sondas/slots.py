import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
for vt in [0x558B4A8,0x55F71B8,0x562E0B0,0x562E420,0x5629BF0,0x5630920]:
    print(hex(vt), [hex(pe.u64(vt+8*k)-pe.BASE) for k in (99,100,101)])
for a,n in [(0x34CFC20,16),(0xB27880,48),(0xC38F30,32),(0xC3A440,48),(0xC4D6B0,48),(0xC45740,32)]:
    print(hex(a), pe.rd(a,n).hex())
    pe.pr(a,a+n)
