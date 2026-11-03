import sys,os,re,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
# RIP-relative lea/mov references (REX.W 8D/8B with modrm mod=00 rm=101) to data RVAs, in .rsrc
targets={int(a,16) for a in sys.argv[1:]}
pat=re.compile(rb'(?=[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d])',re.S)
for name,va,vs,ro,rs in D.secs:
    if name!='.rsrc': continue
    data=D.mm[ro:ro+rs]
    for m in pat.finditer(data):
        o=m.start()
        disp=struct.unpack_from('<i',data,o+3)[0]
        t=va+o+7+disp
        if t in targets:
            f=D.func_of(va+o)
            print(f"{va+o:08X} {'lea' if data[o+1]==0x8d else 'mov'} -> {t:X} func {f[0]:08X}" if f else f"{va+o:08X} -> {t:X} nofunc")
