import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
# find 'call qword ptr [reg + disp32]' with given disps over .rsrc by raw byte scan: FF /2 with modrm mod=10
disps = [int(x, 16) for x in sys.argv[1:]]
d = pe._d
for n, va, vs, ro, rs in pe.SECS:
    if n != '.rsrc': continue
    seg = d[ro:ro+rs]
    for disp in disps:
        pat = struct.pack('<i', disp)
        i = seg.find(pat)
        while i != -1:
            # check preceding bytes FF modrm (mod=10, reg=010)
            for back in (2, 3):
                j = i - back
                if j < 0: continue
                if seg[j] == 0xFF and (seg[j+1] & 0xF8) in (0x90,) and back == 2:
                    a = va + j
                    f = pe.func_of(a)
                    print(f"{disp:#x} call [r{seg[j+1]&7}+disp] at {a:#x} func {f and hex(f[0])}")
                if back == 3 and seg[j] in (0x41,) and seg[j+1] == 0xFF and (seg[j+2] & 0xF8) == 0x90:
                    a = va + j
                    f = pe.func_of(a)
                    print(f"{disp:#x} call [r{8+(seg[j+2]&7)}+disp] at {a:#x} func {f and hex(f[0])}")
            i = seg.find(pat, i + 1)
