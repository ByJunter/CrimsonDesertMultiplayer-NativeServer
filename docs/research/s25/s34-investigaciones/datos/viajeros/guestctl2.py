import sys, collections, math
sys.path.insert(0, '.')
import s34road as S
for tag in sys.argv[1:]:
    g = S.CASES[tag][0]
    acts, ev, roads, rem = S.ingest(g)
    out = []
    for k, a in acts.items():
        if (k >> 28) != 0xB: continue
        t, r = min(a, key=lambda x: x[0]); c = S.u32(r, 18)
        if (c >> 28) != 9 or c == 0x90100000 or r[0] != 3: continue
        e = sorted(x for x in ev.get(k, []) if x[1] == 1)
        ps = [(x[0], x[3]) for x in e if x[2] in (2223, 2556) and x[3]]
        if not ps: continue
        d = max(math.hypot(p[0]-ps[0][1][0], p[2]-ps[0][1][2]) for tt, p in ps)
        # path length
        L = sum(math.hypot(ps[i][1][0]-ps[i-1][1][0], ps[i][1][2]-ps[i-1][1][2]) for i in range(1, len(ps)))
        m = S.meta(r) or {}
        out.append((round(d, 1), round(L, 1), f'{k:08X}', m.get('name'), sum(1 for x in e if x[2] == 2556), round(ps[0][0],1), round(ps[-1][0],1)))
    out.sort(reverse=True)
    print(tag, out[:15])
