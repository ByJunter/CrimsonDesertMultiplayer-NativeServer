import sys, struct, re
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe
pe = cdpe.PE(); B = cdpe.BASE
# registration funcs ranges
regs = [(0x21FA720, 0x2209C20), (0x2209C20, 0x2220000)]
def s(r):
    b = pe.read(r, 200)
    if b is None: return None
    if len(b)>1 and b[1]==0:
        return b.decode('utf-16le','replace').split('\0')[0]
    return b.split(b'\0')[0].decode('latin-1')
def name(var):
    out=[]
    for lo,hi in regs:
        code = pe.read(lo, hi-lo)
        for m in re.finditer(rb"\x48\x8d\x0d", code):
            o = m.start(); src = lo+o
            d = struct.unpack_from("<i", code, o+3)[0]
            if src+7+d != var: continue
            ins = list(pe.md.disasm(pe.read(src, 60), B+src))[:6]
            strs=[]
            for i in ins:
                mm = re.search(r"\[rip \+ 0x([0-9a-f]+)\]", i.op_str)
                if i.mnemonic=='lea' and mm and i.op_str[:2] in ('r8','rd'):
                    t = i.address+i.size+int(mm.group(1),16)-B
                    strs.append(s(t))
            out.append(strs)
    return out
for a in sys.argv[1:]:
    v=int(a,16); print(hex(v), name(v))
