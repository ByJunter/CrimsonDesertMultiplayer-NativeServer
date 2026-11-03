import sys,os,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read
HC=sys.argv[1]
want={0xB010006D,0xB0100071,0xB0100077,0xB010007E,0xB0100082,0xB010015A,0xB0100213,0xA0100001}
first2005={}
for n,t,d,op,size,data,sess,tid in read(HC):
    p=data[5:]
    if d!=2: continue
    if op==2661:
        actor,owner=struct.unpack_from('<II',p,1); seq=struct.unpack_from('<Q',p,9)[0]
        nb=(len(p)-91)//11
        bs=[(struct.unpack_from('<II',p,91+11*i),p[91+11*i+8:91+11*i+11].hex()) for i in range(nb)]
        print(f"#{n} t={t:.3f} 2661 seq {seq:#x} play[13:91]={p[17:91].hex()}")
        for (nd,ac),ty in bs: print(f"      node {nd:08X} actor {ac:08X} u8s {ty}")
    elif op==2005:
        for k in want:
            kb=struct.pack('<I',k)
            if k not in first2005 and kb in p[:16]:
                first2005[k]=t; print(f"#{n} t={t:.3f} 2005 first for {k:08X} (size {size})")
    elif op==2981:
        print(f"#{n} t={t:.3f} 2981 size {size} {p.hex()}")
    if t>56: break
