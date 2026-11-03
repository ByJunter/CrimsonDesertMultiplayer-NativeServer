import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
tg = [int(x, 16) for x in sys.argv[1:]]
d = pe._d
for n, va, vs, ro, rs in pe.SECS:
    if n not in ('.rsrc', '.xtls', '.text'): continue
    seg = d[ro:ro+rs]
    for i in range(len(seg) - 7):
        if seg[i] in (0x48, 0x4C) and seg[i+1] == 0x8D and (seg[i+2] & 0xC7) == 0x05:
            t = va + i + 7 + struct.unpack_from('<i', seg, i+3)[0]
            if t in tg:
                f = pe.func_of(va + i)
                print(f"{t:#x} <- lea at {va+i:#x} func {f and hex(f[0])}")
