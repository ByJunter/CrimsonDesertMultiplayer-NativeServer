import sys, struct, json
from pathlib import Path
sys.path.insert(0, r'C:\Emulador CD-opcion-d\build\research\s33-npc-20261014\b\dependencias')
from cap import iter_capture
from nm_cap import recs2005, tail_children
u32 = lambda p, o=0: struct.unpack_from('<I', p, o)[0]
def name(r):
    n = u32(r, 30)
    return r[34:34 + n * 2].decode('utf-16le', errors='replace') if n < 100 else '?%d' % n
out = {}
for t, d, op, sl, ln, sz, p, thr, ts in iter_capture(sys.argv[1]):
    if op != 2005: continue
    for o, r in recs2005(p):
        if len(r) < 40: continue
        k = u32(r, 1)
        if '%08X' % k in out: continue
        n, par = tail_children(r)
        parent = u32(r, par) if par is not None else None
        kids = [u32(r, par + 6 + 4 * i) for i in range(n)] if n else []
        out['%08X' % k] = dict(t=round(t, 3), dir=d, type=r[0], ctl='%08X' % u32(r, 18), name=name(r),
                               parent='%08X' % parent if parent is not None else None, kids=['%08X' % x for x in kids])
json.dump(out, open(sys.argv[2], 'w'), indent=0)
print(len(out))
