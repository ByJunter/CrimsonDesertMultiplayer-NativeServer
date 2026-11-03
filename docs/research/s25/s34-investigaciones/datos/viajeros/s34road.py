"""S34 viajeros: cohorte 2550 del invitado (cualquier tipo + familia), controlador, Req y posiciones.
Solo lectura de capturas CDPKv2. Uso: python -I -B s34road.py <tag>"""
import struct, mmap, collections, json, math, sys
from pathlib import Path
O = Path(__file__).parent
B = Path(r'C:\Emulador CD\backups\p8-sesion34')
# tag: guest bin, host bin, user2 char, host load t, host prepare t, host leaves t (None = never near)
CASES = {
 '1b': (B/'shadow-p1b-p2b/cdmp_packets_20261014-175057.bin', B/'paso1b/cdmp_packets_20261014-185010.bin', 0xA01001D5, 261.952745, 271.126643, 325.397111),
 '2b': (B/'shadow-p1b-p2b/cdmp_packets_20261014-180517.bin', B/'paso2b/cdmp_packets_20261014-190242.bin', 0xA010024A, None, None, None),
}
R = struct.Struct('<QBBHHHII'); u32 = lambda p, o=0: struct.unpack_from('<I', p, o)[0]
OPS = (2005, 2550, 2223, 2556, 2734, 2291, 2405, 2697, 2991, 2508, 2828, 2516)
def read(path):
    with open(path, 'rb') as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as data:
        assert data[:4] == b'CDPK'; off = 12; t0 = None
        while off + 24 <= len(data):
            ts, d, fl, wire, cp, res, sess, thr = R.unpack_from(data, off); off += 24
            p = data[off:off+cp]; off += cp
            if len(p) < 5: continue
            if t0 is None: t0 = ts
            op = struct.unpack_from('<H', p)[0]
            if op in OPS: yield (ts - t0) / 1e6, d, op, p[5:]
def records(p):
    pos = 0
    while pos + 2 <= len(p):
        n = struct.unpack_from('<H', p, pos)[0]; pos += 2
        if not n or pos + n > len(p): return
        yield p[pos:pos+n]; pos += n
def plaus(k): return (k >> 28) in (0xA, 0xB)
def family(r):
    for n in range(33):
        if len(r) < 6 + 4*n: continue
        if struct.unpack_from('<H', r, len(r)-2-4*n)[0] != n: continue
        ch = [u32(r, len(r)-4*n+4*c) for c in range(n)]
        par = u32(r, len(r)-6-4*n)
        if all(plaus(c) for c in ch) and (par == 0 or plaus(par)): return par, ch
    return None, []
def meta(r):
    if len(r) < 34: return None
    n = u32(r, 30)
    if n > (len(r)-34)//2: return None
    end = 34 + 2*n
    if end + 100 > len(r): return None
    return dict(name=r[34:end].decode('utf16', 'replace'), req=u32(r, end+88), sub=u32(r, end+92), road=u32(r, end+96))
def ingest(path):
    acts = collections.defaultdict(list); ev = collections.defaultdict(list); roads = []; removes = collections.defaultdict(list)
    for t, d, op, p in read(path):
        if op == 2005 and d == 2:
            for r in records(p):
                if len(r) < 0x16: continue
                acts[u32(r, 1)].append((t, bytes(r)))
        elif op == 2550 and d == 1:
            for at in range(5, len(p) - 23, 24):
                road, num, cat, idx, x, y, z, a = struct.unpack_from('<IHBB3ff', p, at)
                roads.append(dict(t=t, road=road, num=num, cat=cat, idx=idx, pos=(x, y, z)))
        elif op == 2516 and d == 2 and len(p) >= 6:
            reason = p[5]; n = (len(p) - 6) // 4
            for i in range(n): removes[u32(p, 6+4*i)].append((t, reason))
        elif len(p) >= 4:
            k = u32(p)
            if (k >> 28) != 0xB: continue
            v = None
            off = 0x8E if op == 2223 else 0x8F if op == 2991 else 4 if op in (2556, 2697) else None
            if off is not None and len(p) >= off + 12:
                v = struct.unpack_from('<3f', p, off)
                if not all(math.isfinite(c) for c in v): v = None
            ev[k].append((t, d, op, v))
    return acts, ev, roads, removes
def extent(events, base, d, ops, lo, hi):
    ps = [e[3] for e in events if e[1] == d and e[2] in ops and e[3] and lo <= e[0] < hi]
    if base is None or not ps: return None, len(ps)
    return max(math.hypot(p[0]-base[0], p[2]-base[2]) for p in ps), len(ps)
def main(tag):
    gpath, hpath, own, load, prep, leave = CASES[tag]
    ga, ge, roads, grem = ingest(gpath)
    ha, he, _, hrem = ingest(hpath)
    ids = {r['road'] for r in roads}
    cohort = {}
    for k, ars in ga.items():
        t, r = min(ars, key=lambda x: x[0]); m = meta(r)
        if (k >> 28) != 0xB or not m or m['req'] != own or not m['road']: continue
        par, ch = family(r)
        cohort[k] = dict(key=f'{k:08X}', type=r[0], name=m['name'], road=m['road'], sub=m['sub'], in2550=m['road'] in ids,
                         gctl=f"{u32(r,18):08X}", gbirth=t, g2005=len(ars), parent=f'{par:08X}' if par else None,
                         children=[f'{c:08X}' for c in ch])
    # family members not themselves carrying road metadata
    fam = {}
    for k, c in cohort.items():
        for ch in c['children']:
            kk = int(ch, 16)
            if kk not in cohort and kk in ga:
                t, r = min(ga[kk], key=lambda x: x[0]); m = meta(r) or {}
                fam[kk] = dict(key=ch, type=r[0], name=m.get('name'), road=m.get('road'), req=f"{m.get('req',0):08X}", gctl=f"{u32(r,18):08X}", root=c['key'])
    hb = lambda k: min((t for t, r in ha.get(k, [])), default=None)
    far_hi = load if load is not None else 1e9
    out = []
    for k, c in cohort.items():
        h = ha.get(k, [])
        c['h2005'] = len(h); c['hctl'] = sorted({f"{u32(r,18):08X}" for t, r in h}); c['hbirth'] = hb(k)
        ee = sorted(he.get(k, []), key=lambda e: e[0]); gg = sorted(ge.get(k, []), key=lambda e: e[0])
        first = next((e for e in ee if e[1] == 1 and e[2] in (2223, 2556) and e[3]), None)
        base = first[3] if first else None
        c['host_req_far'] = dict(collections.Counter(e[2] for e in ee if e[1] == 1 and e[0] < far_hi))
        c['host_first_req'] = next((e[0] for e in ee if e[1] == 1), None)
        c['host_first_2556'] = next((e[0] for e in ee if e[1] == 1 and e[2] == 2556), None)
        c['ext_far'], c['npos_far'] = extent(ee, base, 1, (2223, 2556), 0, far_hi)
        if prep is not None:
            c['ext_near'], c['npos_near'] = extent(ee, base, 1, (2223, 2556), prep, min(prep + 30, leave or 1e9))
            c['ext_near_all'], _ = extent(ee, base, 1, (2223, 2556), prep, leave or 1e9)
            c['ext_after_leave'], c['npos_after_leave'] = extent(ee, base, 1, (2223, 2556), leave + 8, 1e9) if leave else (None, 0)
        c['guest_req'] = dict(collections.Counter(e[2] for e in gg if e[1] == 1))
        c['guest_in'] = dict(collections.Counter(e[2] for e in gg if e[1] == 2))
        gb = next((e[3] for e in gg if e[1] == 2 and e[3]), None)
        c['guest_in_ext'], _ = extent(gg, base or gb, 2, (2697, 2991), 0, 1e9)
        c['hremove'] = hrem.get(k, [])[:3]; c['gremove'] = grem.get(k, [])[:3]
        c['born_before_load_15'] = c['hbirth'] is not None and c['hbirth'] < far_hi - 15
        out.append(c)
    famout = []
    for k, f in fam.items():
        h = ha.get(k, []); f['hctl'] = sorted({f"{u32(r,18):08X}" for t, r in h}); f['h2005'] = len(h)
        ee = sorted(he.get(k, []), key=lambda e: e[0])
        f['host_req_far'] = dict(collections.Counter(e[2] for e in ee if e[1] == 1 and e[0] < far_hi))
        f['guest_req'] = dict(collections.Counter(e[2] for e in ge.get(k, []) if e[1] == 1))
        famout.append(f)
    # all guest 2005 records with requester = own (any key class), by type
    bytype = collections.Counter(); ctl = collections.Counter()
    for c in out: bytype[c['type']] += 1; ctl[(c['gctl'], tuple(c['hctl']))] += 1
    val = [c for c in out if c['born_before_load_15'] and c['ext_far'] is not None]
    summ = dict(tag=tag, entries2550=len(roads), cohort=len(out), types=dict(bytype), controllers={f'{a}|{",".join(b)}': n for (a, b), n in ctl.items()},
                family_extra=len(famout), family_types=dict(collections.Counter(f['type'] for f in famout)),
                family_ctl=dict(collections.Counter(f['gctl'] for f in famout)),
                guest_reqs=sum(sum(c['guest_req'].values()) for c in out) + sum(sum(f['guest_req'].values()) for f in famout),
                host_has_2005=sum(c['h2005'] > 0 for c in out),
                host_req_any=sum(bool(c['host_req_far']) for c in out),
                valid_far=len(val), still_far=sum(c['ext_far'] <= 1 for c in val), moved_far=sum(c['ext_far'] > 1 for c in val),
                moved_far_list=[(c['key'], c['type'], c['name'], round(c['ext_far'], 2)) for c in val if c['ext_far'] > 1])
    if prep is not None:
        summ['still_then_moved_near30'] = sum(c['ext_far'] <= 1 and (c.get('ext_near') or 0) > 1 for c in val)
        summ['still_then_moved_near_all'] = sum(c['ext_far'] <= 1 and (c.get('ext_near_all') or 0) > 1 for c in val)
        summ['still_with_near_samples'] = sum(c['ext_far'] <= 1 and c.get('npos_near', 0) > 0 for c in val)
    print(json.dumps(summ, indent=1, ensure_ascii=False))
    (O / f'cohort{tag}.json').write_text(json.dumps(dict(summary=summ, cohort=out, family=famout), indent=1, ensure_ascii=False), encoding='utf8')
if __name__ == '__main__': main(sys.argv[1])
