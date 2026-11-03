import sys, collections, math
sys.path.insert(0, '.')
import s34road as S
for tag in sys.argv[1:]:
    g = S.CASES[tag][0]
    acts, ev, roads, rem = S.ingest(g)
    ctl = collections.Counter(); moved = collections.Counter(); n2556 = 0
    for k, a in acts.items():
        if (k >> 28) != 0xB: continue
        t, r = min(a, key=lambda x: x[0]); c = S.u32(r, 18)
        if (c >> 28) != 9 or c == 0x90100000: continue
        e = [x for x in ev.get(k, []) if x[1] == 1]
        ps = [x[3] for x in e if x[2] in (2223, 2556) and x[3]]
        d = max((math.hypot(p[0]-ps[0][0], p[2]-ps[0][2]) for p in ps), default=None)
        ctl[(f'{c:08X}', r[0])] += 1
        n2556 += sum(1 for x in e if x[2] == 2556)
        if d is not None: moved[(r[0], 'moved' if d > 1 else 'still')] += 1
        else: moved[(r[0], 'noReq')] += 1
    print(tag, 'guest-controlled 2005 by (ctl,type)', dict(ctl), 'movement', dict(moved), 'guest 2556 sent', n2556)
