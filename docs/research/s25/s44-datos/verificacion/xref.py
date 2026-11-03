# Direct calls / jumps (E8/E9 rel32) to the targets in .rsrc, and 8-byte pointers to them anywhere (vtables). Read only.
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
d = pe._d
targets = [int(a, 16) for a in sys.argv[1:]]
rs = [s for s in pe.SECS if s[0] == '.rsrc'][0]
name, va, vs, ro, rsz = rs
code = d[ro:ro + rsz]
import re
for t in targets:
    hits = []
    for op in (0xE8, 0xE9):
        start = 0
        b = bytes([op])
        while True:
            i = code.find(b, start)
            if i < 0 or i + 5 > len(code):
                break
            rel = struct.unpack_from('<i', code, i + 1)[0]
            src = va + i
            if src + 5 + rel == t:
                f = pe.func_of(src)
                hits.append((src, op, f[0] if f else 0))
            start = i + 1
    ptr = struct.pack('<Q', pe.BASE + t)
    ptrs = []
    s = 0
    while True:
        i = d.find(ptr, s)
        if i < 0:
            break
        # file offset -> rva
        for n, sva, svs, sro, srs in pe.SECS:
            if sro <= i < sro + srs:
                ptrs.append((n, sva + i - sro))
        s = i + 1
    print(f"== {t:#x}: {len(hits)} direct")
    for src, op, f in hits[:40]:
        print(f"   {'call' if op == 0xE8 else 'jmp '} at {src:#x} (func {f:#x})")
    for n, r in ptrs[:20]:
        print(f"   pointer in {n} at {r:#x}")
