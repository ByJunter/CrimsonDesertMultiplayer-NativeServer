import sys, os, struct, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
tg=int(sys.argv[1],16)
D=pe._d
for n,va,vs,ro,rs in pe.SECS:
    if n!='.rsrc': continue
    seg=D[ro:ro+rs]
    for m in re.finditer(rb'[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]', seg):
        p=m.start(); v=struct.unpack_from('<i',seg,p+3)[0]; t=va+p+7+v
        if t==tg:
            a=va+p; f=pe.func_of(a); print(hex(a), f and hex(f[0]))
