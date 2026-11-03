import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
# functions (pdata) in .rsrc that contain both a +0x98 load loop (mov r,[r+0x98]; test; jne back) and a +0x1d8 access
d=pe._d
hits=[]
for (b,e) in pe.FUNCS:
    if pe.sec(b)!='.rsrc' or e-b>0x3000: continue
    code=pe.rd(b,e-b)
    if b'\x98\x00\x00\x00' not in code or b'\xd8\x01\x00\x00' not in code: continue
    ins=list(pe.md.disasm(code,pe.BASE+b))
    for i,x in enumerate(ins):
        if x.mnemonic=='mov' and '+ 0x98]' in x.op_str:
            # look ahead 40 instructions for 0x1d8
            for y in ins[i+1:i+40]:
                if '0x1d8]' in y.op_str:
                    hits.append((b, x.address-pe.BASE, y.address-pe.BASE, x.op_str, y.mnemonic+' '+y.op_str)); break
seen=set()
for h in hits:
    if h[0] in seen: continue
    seen.add(h[0]); print(hex(h[0]), hex(h[1]), h[3], '|', hex(h[2]), h[4])
