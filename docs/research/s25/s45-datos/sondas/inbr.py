# Branches (E8/E9 rel32, 0F 80-8F rel32, EB/7x rel8) and 8-byte absolute pointers whose target falls inside the
# patched bytes (start excluded), over every section of the exe. Raw byte scan (over-approximates: data bytes too).
import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
ranges=[]
for a in sys.argv[1:]:
    s,e=a.split(':'); ranges.append((int(s,16),int(e,16)))
lo=min(s for s,e in ranges); hi=max(e for s,e in ranges)
D=pe._d
hits=0
for n,va,vs,ro,rs in pe.SECS:
    seg=D[ro:ro+rs]
    for m in re.finditer(rb'[\xE8\xE9]|\x0F[\x80-\x8F]', seg):
        f=m.end()
        if f+4>len(seg): continue
        v=struct.unpack_from('<i',seg,f)[0]
        t=va+f+4+v
        if lo<t<hi:
            for s,e in ranges:
                if s<t<e:
                    print(f"{n} rel32 at {va+m.start():#x} {seg[m.start():f].hex()} -> {t:#x}"); hits+=1
    for s,e in ranges:
        if va<=s<va+rs:
            for p in range(max(0,s-140-va), min(rs-1,e+140-va)):
                if seg[p]==0xEB or 0x70<=seg[p]<=0x7F:
                    t=va+p+2+struct.unpack_from('<b',seg,p+1)[0]
                    if s<t<e: print(f"{n} rel8 at {va+p:#x} -> {t:#x}"); hits+=1
            for t in range(s+1,e):
                pat=struct.pack('<Q',pe.BASE+t)
                j=D.find(pat)
                while j!=-1:
                    print(f"abs ptr at file {j:#x} -> {t:#x}"); hits+=1; j=D.find(pat,j+1)
print('hits',hits)
