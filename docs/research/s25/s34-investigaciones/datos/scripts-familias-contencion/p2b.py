import re, sys, collections, statistics as st
H = sys.argv[1]; G = sys.argv[2]
rx = re.compile(r'^\[(\d\d:\d\d:\d\d\.\d+)\] \w+ \[T\d+\] NpcActivation \[([^\]]+)\] qpc=(\d+) T\d+ \| (.*)$')
kv = re.compile(r'(\w+)=(\S+)')
ev = collections.defaultdict(lambda: collections.defaultdict(list))
lines = []
for ln in open(H, encoding='utf-8', errors='replace'):
    m = rx.match(ln)
    if not m: continue
    tag = m.group(2); q = int(m.group(3)); d = dict(kv.findall(m.group(4)))
    k = d.get('key')
    lines.append((q, tag, d))
    if k: ev[k][tag].append((q, d))
# host U-e control / client primera-Req
ctl = {}
for ln in open(H, encoding='utf-8', errors='replace'):
    m = re.search(r'User2Stages \[control\] qpc ([\d.]+) s .*NPC (0x[0-9A-F]+) \(type (\d)', ln)
    if m: ctl[m.group(2)] = (float(m.group(1)), int(m.group(3)))
# guest
g = collections.defaultdict(dict)
grx = re.compile(r'NpcProbe \[invitado ([^\]]+)\] qpc ([\d.]+) s \(~([\d:.]+)\).*?clave (0x[0-9A-F]+)(.*)$')
for ln in open(G, encoding='utf-8', errors='replace'):
    m = grx.search(ln)
    if not m: continue
    tag, q, wall, k, rest = m.groups()
    q = float(q)
    if tag not in g[k]: g[k][tag] = (q, wall, rest)
def us(x): return int(x)
ARM = 69348054779
print('arm qpc us', ARM)
# offset: host enqueue (us) vs guest alta (s)
pairs = []
for k, e in ev.items():
    if '2005 encolado al invitado' in e and 'alta' in g.get(k, {}):
        hq = e['2005 encolado al invitado'][0][0]
        gq = g[k]['alta'][0] * 1e6
        pairs.append((hq, gq - hq, k))
pairs.sort()
diffs = [p[1] for p in pairs]
off = min(diffs)
print('pairs', len(pairs), 'min diff', off, 'median', st.median(diffs))
# latency relative to min (one-way excess)
def bucket(t0, t1):
    sel = [p[1] - off for p in pairs if t0 <= p[0] < t1]
    if not sel: return None
    sel.sort()
    return len(sel), sel[len(sel)//2]/1e3, sel[int(len(sel)*0.9)]/1e3, sel[-1]/1e3
for a in range(-60, 40, 2):
    b = bucket(ARM + a*1e6, ARM + (a+2)*1e6)
    if b: print(f'enqueue t_arm{a:+d}..{a+2:+d}s: n={b[0]} excess median {b[1]:.0f} ms p90 {b[2]:.0f} max {b[3]:.0f}')
# per sin-progreso unit
print('\n== sin progreso units')
rows = []
for k, e in ev.items():
    if 'sin progreso' not in e: continue
    sp = e['sin progreso'][0][0]
    enq = e.get('2005 encolado al invitado', [(None,)])[0][0]
    born = e.get('nacimiento', [(None,)])[0][0]
    fr = e.get('primera petición', [(None,)])[0][0]
    act = [x for x in e.get('acción OnServer', [])]
    gal = g.get(k, {}).get('alta')
    gfr = g.get(k, {}).get('primera-Req')
    gbaja = g.get(k, {}).get('baja')
    rows.append((k, born, enq, sp, fr, act[0][0] if act else None, act[0][1] if act else None,
                 (gal[0]*1e6 - off) if gal else None, (gfr[0]*1e6 - off) if gfr else None, gbaja, ctl.get(k)))
def rel(x, base):
    return '-' if x is None else f'{(x-base)/1e6:+.2f}'
n_fr = sum(1 for r in rows if r[4]); n_act = sum(1 for r in rows if r[5]); n_gal = sum(1 for r in rows if r[7]); n_gfr = sum(1 for r in rows if r[8])
print('units', len(rows), 'with host first Req', n_fr, 'with OnServer applied', n_act, 'guest alta', n_gal, 'guest firstReq', n_gfr)
cnt = collections.Counter()
for r in rows:
    k, born, enq, sp, fr, aq, ad, gal, gfr, gb, c = r
    cls = []
    if aq and aq < sp: cls.append('applied-before-deadline')
    elif aq: cls.append('applied-after')
    if fr and fr < sp: cls.append('hostReq-before')
    if gal is None: cls.append('guest-never-received')
    elif gal > sp: cls.append('guest-received-after-deadline')
    if gfr is not None and gfr < sp: cls.append('guestReq-before')
    cnt[' '.join(cls) or 'nothing'] += 1
for c, n in cnt.most_common(): print(n, c)
print('\nsample:')
for r in sorted(rows, key=lambda r: r[3])[:25]:
    k, born, enq, sp, fr, aq, ad, gal, gfr, gb, c = r
    print(k, 'type', c[1] if c else '?', 'enq->guestAlta', rel(gal, enq), 'enq->hostFirstReq', rel(fr, enq), 'enq->applied', rel(aq, enq), 'enq->guestFirstReq', rel(gfr, enq), 'enq->sinprog', rel(sp, enq), 'guestBaja', gb[2][:60] if gb else '-')
# all assigned: distribution of enqueue->first applied action and enqueue->first Req
print('\n== distributions over all tracked (enqueue -> event)')
d_fr = []; d_ap = []; d_gal = []; d_gfr = []
for k, e in ev.items():
    if '2005 encolado al invitado' not in e: continue
    enq = e['2005 encolado al invitado'][0][0]
    if 'primera petición' in e: d_fr.append((e['primera petición'][0][0] - enq)/1e6)
    if 'acción OnServer' in e: d_ap.append((e['acción OnServer'][0][0] - enq)/1e6)
    if 'alta' in g.get(k, {}): d_gal.append((g[k]['alta'][0]*1e6 - off - enq)/1e6)
    if 'primera-Req' in g.get(k, {}): d_gfr.append((g[k]['primera-Req'][0]*1e6 - off - enq)/1e6)
def q(v):
    v = sorted(v)
    if not v: return 'n=0'
    return f'n={len(v)} p10 {v[len(v)//10]:.2f} p50 {v[len(v)//2]:.2f} p90 {v[int(len(v)*.9)]:.2f} p99 {v[min(len(v)-1,int(len(v)*.99))]:.2f} max {v[-1]:.2f}  >5s {sum(1 for x in v if x>5)}'
print('enq->hostFirstReq', q(d_fr)); print('enq->applied', q(d_ap)); print('enq->guestAlta(excess over min)', q(d_gal)); print('enq->guestFirstReq(excess)', q(d_gfr))
tracked = [k for k, e in ev.items() if '2005 encolado al invitado' in e]
print('tracked with enqueue', len(tracked))
