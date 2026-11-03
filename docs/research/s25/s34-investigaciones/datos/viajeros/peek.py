import sys, struct, collections, math
sys.path.insert(0, '.')
import s34road as S
tag = sys.argv[1]; keys = [int(k, 16) for k in sys.argv[2:]]
g, h = S.CASES[tag][0], S.CASES[tag][1]
for side, path in (('G', g), ('H', h)):
    acts, ev, roads, rem = S.ingest(path)
    for k in keys:
        a = acts.get(k, [])
        for t, r in a[:2]:
            m = S.meta(r); par, ch = S.family(r)
            print(side, f'{k:08X}', 'type', r[0], 't', round(t, 3), 'ctl', f'{S.u32(r,18):08X}', m and {kk: (hex(v) if isinstance(v, int) else v) for kk, v in m.items()}, 'par', par and hex(par), 'ch', [hex(c) for c in ch])
        e = sorted(ev.get(k, []))
        print(side, f'{k:08X}', collections.Counter((x[1], x[2]) for x in e))
        ps = [(round(x[0], 2), x[1], x[2], tuple(round(c, 1) for c in x[3])) for x in e if x[3]]
        print(side, ' first', ps[:4]); print(side, ' last', ps[-3:])
        print(side, ' rem', rem.get(k))
