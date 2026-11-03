import sys, os, struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
tg = [int(x,16) for x in sys.argv[1:]]
d = pe._d
for n, va, vs, ro, rs in pe.SECS:
    if n not in ('.rsrc', '.xtls', '.text'): continue
    seg = d[ro:ro+rs]
    for opc in (b'\xe8', b'\xe9'):
        i = seg.find(opc)
        while i != -1 and i < len(seg) - 5:
            t = va + i + 5 + struct.unpack_from('<i', seg, i+1)[0]
            if t in tg:
                f = pe.func_of(va+i)
                print(f"{t:#x} <- {opc.hex()} {va+i:#x} {n} func {f and hex(f[0])}")
            i = seg.find(opc, i+1)
# absolute pointers
for t in tg:
    b = struct.pack('<Q', pe.BASE + t); i = d.find(b)
    while i != -1: print(f"{t:#x} ptr at fileoff {i:#x}"); i = d.find(b, i+1)
