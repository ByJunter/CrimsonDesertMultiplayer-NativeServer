import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
for (b,e) in pe.FUNCS:
    if pe.sec(b)!='.rsrc' or e-b>0x8000: continue
    if not (0x5c0000<=b<0x640000 or 0xa80000<=b<0xab0000 or 0x2040000<=b<0x2070000 or 0x34c0000<=b<0x34f0000): continue
    code=pe.rd(b,e-b)
    if b'\x98\x02\x00\x00' not in code and b'\xd8\x01\x00\x00' not in code: continue
    for x in pe.md.disasm(code,pe.BASE+b):
        s=x.op_str
        if ('+ 0x298]' in s and s.startswith('dword ptr [') and x.mnemonic=='mov') or ('0x1d8]' in s and 'dword' in s and 0x34c0000<=b<0x34f0000):
            print(hex(b), hex(x.address-pe.BASE), x.mnemonic, s)
