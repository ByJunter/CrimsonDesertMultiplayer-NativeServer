import sys,struct,mmap
from capstone import *
PE=r'D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe'
f=open(PE,'rb'); mm=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
pe=struct.unpack_from('<I',mm,0x3c)[0]
nsec=struct.unpack_from('<H',mm,pe+6)[0]; opt=struct.unpack_from('<H',mm,pe+20)[0]
secs=[]
for i in range(nsec):
    o=pe+24+opt+i*40
    name=mm[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',mm,o+8)
    secs.append((name,va,vs,ro,rs))
def sec(rva):
    for s in secs:
        if s[1]<=rva<s[1]+max(s[2],s[4]): return s
def rd(rva,n):
    s=sec(rva); o=s[3]+rva-s[1]; return mm[o:o+n]
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=False
def dis(rva,n,stop_ret=True,maxins=100000):
    code=rd(rva,n); out=[]
    for ins in md.disasm(code,0x140000000+rva):
        r=ins.address-0x140000000
        s=f"{r:08X} {ins.bytes.hex(' '):<32} {ins.mnemonic} {ins.op_str}"
        if ins.mnemonic in('call','jmp') and ins.op_str.startswith('0x'):
            t=int(ins.op_str,16)-0x140000000; sc=sec(t); s+=f"   ; {sc[0] if sc else '?'}"
        if 'rip +' in ins.op_str:
            import re
            m=re.search(r'rip \+ (0x[0-9a-f]+)',ins.op_str)
            if m: s+=f"   ; -> {r+ins.size+int(m.group(1),16):#x}"
        out.append(s)
        if stop_ret and ins.mnemonic=='int3': break
        if len(out)>=maxins: break
    return out
if __name__=='__main__':
    rva=int(sys.argv[1],16); n=int(sys.argv[2],16) if len(sys.argv)>2 else 0x400
    for l in dis(rva,n,stop_ret=(len(sys.argv)<4)): print(l)

# .pdata function table
import bisect
_pd=None
def pdata():
    global _pd
    if _pd is None:
        opt_off=pe+24
        magic=struct.unpack_from('<H',mm,opt_off)[0]
        dd=opt_off+(112 if magic==0x20b else 96)
        exc_rva,exc_size=struct.unpack_from('<II',mm,dd+3*8)
        raw=rd(exc_rva,exc_size)
        ents=[struct.unpack_from('<III',raw,i) for i in range(0,len(raw)-11,12)]
        ents.sort(); _pd=ents
    return _pd
def func_of(rva):
    ents=pdata(); i=bisect.bisect_right(ents,(rva,0xFFFFFFFF,0xFFFFFFFF))-1
    if i>=0 and ents[i][0]<=rva<ents[i][1]: return ents[i]
    return None

# Whole function: the .pdata entry of rva merged with the contiguous neighbours that are chained
# (UNW_FLAG_CHAININFO in the later entry's unwind info), as MSVC splits one function into chunks.
def _chained(ent):
    info = rd(ent[2], 1)[0]
    return bool((info >> 3) & 4)
def func_whole(rva):
    ents = pdata(); i = bisect.bisect_right(ents, (rva, 0xFFFFFFFF, 0xFFFFFFFF)) - 1
    if i < 0 or not (ents[i][0] <= rva < ents[i][1]): return None
    lo = i
    while lo > 0 and ents[lo - 1][1] == ents[lo][0] and _chained(ents[lo]): lo -= 1
    hi = i
    while hi + 1 < len(ents) and ents[hi][1] == ents[hi + 1][0] and _chained(ents[hi + 1]): hi += 1
    return (ents[lo][0], ents[hi][1], ents[lo][2])
