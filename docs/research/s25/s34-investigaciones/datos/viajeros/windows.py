import sys, collections, math, json
sys.path.insert(0, '.')
import s34road as S
tag = '1b'; g, h, own, load, prep, leave = S.CASES[tag]
ha, he, _, hrem = S.ingest(h)
d = json.load(open('cohort1b.json', encoding='utf8'))
keys = {int(c['key'], 16): c for c in d['cohort']}
W = [('far1', 0, load), ('near', prep, leave), ('far2', leave + 8, 1e9)]
stats = collections.Counter(); ex = []
for k, c in keys.items():
    ee = sorted(he.get(k, []))
    row = {}
    for name, lo, hi in W:
        ps = [(e[0], e[3]) for e in ee if e[1] == 1 and e[2] in (2223, 2556) and e[3] and lo <= e[0] < hi]
        if len(ps) < 2: continue
        x0 = ps[0][1]; dmax = max(math.hypot(p[0]-x0[0], p[2]-x0[2]) for t, p in ps)
        # last 20 s of window movement
        tail = [p for t, p in ps if t >= min(hi, ps[-1][0]) - 20]
        dtail = max((math.hypot(p[0]-tail[0][0], p[2]-tail[0][2]) for p in tail), default=0)
        n2556 = sum(1 for e in ee if e[1] == 1 and e[2] == 2556 and lo <= e[0] < hi)
        row[name] = (round(dmax, 1), round(dtail, 1), n2556, round(ps[0][0], 1), round(ps[-1][0], 1))
        stats[(name, c['type'], 'moved' if dmax > 1 else 'still')] += 1
    if 'near' in row and 'far2' in row: ex.append((c['key'], c['type'], c['name'], row))
for k in sorted(stats): print(k, stats[k])
print('alive in near and far2:', len(ex))
for e in ex[:40]: print(e)
# 2556 rate per window per alive key
print('--- far2 (host left) for actors that moved while host near')
cnt = collections.Counter()
for k, t, n, row in ex:
    if row['near'][0] > 1:
        f = row['far2']; cnt[(t if t != 6 else 6, 'kept moving to end' if f[1] > 1 else ('stopped' if f[0] > 1 else 'no move'))] += 1
print(dict(cnt))
