import sys,os,struct,json,re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
vt=json.load(open('vtmap.json'))
slot=int(sys.argv[1]); target=int(sys.argv[2],16)
found={}
for k,(n,o,l) in vt.items():
    if l<=slot or o!=0: continue
    p=struct.unpack('<Q',D.rd(int(k,16)+8*slot,8))[0]-0x140000000
    f=D.func_of(p)
    if not f: continue
    code=D.rd(f[0],f[1]-f[0])
    hit=False
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
        o2=m.start(); disp=struct.unpack_from('<i',code,o2+3)[0]
        if f[0]+o2+7+disp==target: hit=True;break
    if hit: found.setdefault(p,[]).append(n)
for p,ns in found.items(): print(hex(p),ns[:6],len(ns))
