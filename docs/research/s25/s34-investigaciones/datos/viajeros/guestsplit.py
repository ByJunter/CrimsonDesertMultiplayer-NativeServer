import sys, collections, math
sys.path.insert(0, '.')
import s34road as S
for tag in sys.argv[1:]:
    g = S.CASES[tag][0]; own = S.CASES[tag][2]
    acts, ev, roads, rem = S.ingest(g)
    res = collections.defaultdict(lambda: [0, 0, 0, []])
    for k, a in acts.items():
        if (k >> 28) != 0xB: continue
        t, r = min(a, key=lambda x: x[0]); m = S.meta(r)
        if not m or (m['req'] >> 28) != 0xA or not m['road']: continue
        who = 'user2' if m['req'] == own else f"{m['req']:08X}"
        ps = [x[3] for x in ev.get(k, []) if x[1] == 2 and x[2] in (2697, 2991) and x[3]]
        rq = sum(1 for x in ev.get(k, []) if x[1] == 1)
        d = max((math.hypot(p[0]-ps[0][0], p[2]-ps[0][2]) for p in ps), default=None)
        e = res[(who, r[0])]; e[0] += 1
        if d is not None:
            e[1] += 1; e[2] += d > 1
        if rq: e[3].append(f'{k:08X}')
        res[(who, 'xz')].append if False else None
    print('==', tag)
    for key, (n, withpos, moved, req) in sorted(res.items(), key=lambda x: str(x[0])):
        print(key, 'n', n, 'with S>C pos', withpos, 'moved>1m', moved, 'guest Req keys', len(req))
    # where were they? first pos per requester
    xs = collections.defaultdict(list)
    for k, a in acts.items():
        t, r = min(a, key=lambda x: x[0]); m = S.meta(r)
        if not m or (m['req'] >> 28) != 0xA or not m['road']: continue
        ps = [x[3] for x in ev.get(k, []) if x[1] == 2 and x[3]]
        if ps: xs['user2' if m['req'] == own else f"{m['req']:08X}"].append((round(ps[0][0]), round(ps[0][2]), round(t)))
    for w, l in xs.items():
        print(w, 'x range', min(p[0] for p in l), max(p[0] for p in l), 'z range', min(p[1] for p in l), max(p[1] for p in l), 't range', min(p[2] for p in l), max(p[2] for p in l))
