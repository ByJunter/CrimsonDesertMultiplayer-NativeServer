import re, sys, collections
H = sys.argv[1]; G = sys.argv[2]
rx = re.compile(r'^\[(\d\d:\d\d:\d\d\.\d+)\] \w+ \[T\d+\] NpcActivation \[([^\]]+)\] qpc=(\d+) T\d+ \| (.*)$')
kv = re.compile(r'(\w+)=(\S+)')
ev = collections.defaultdict(lambda: collections.defaultdict(list))
allev = []
for ln in open(H, encoding='utf-8', errors='replace'):
    m = rx.match(ln)
    if not m: continue
    tag = m.group(2); q = int(m.group(3)); d = dict(kv.findall(m.group(4)))
    allev.append((q, tag, d))
    if d.get('key'): ev[d['key']][tag].append((q, d))
g = collections.defaultdict(dict)
grx = re.compile(r'NpcProbe \[invitado ([^\]]+)\] qpc ([\d.]+) s \(~([\d:.]+)\).*?clave (0x[0-9A-F]+)(.*)$')
for ln in open(G, encoding='utf-8', errors='replace'):
    m = grx.search(ln)
    if not m: continue
    tag, q, wall, k, rest = m.groups()
    if tag not in g[k]: g[k][tag] = (float(q), wall, rest)
ARM = 69348054779
OFF = -68241540323.0
def T(q): return (q - ARM) / 1e6
# never applied tracked
print('== tracked (2005 enqueued) without applied action')
na = []
for k, e in ev.items():
    if '2005 encolado al invitado' not in e or 'acción OnServer' in e: continue
    enq = e['2005 encolado al invitado'][0][0]
    b = e.get('nacimiento') or e.get('familia')
    typ = b[0][1].get('tipo') if b else '?'
    par = b[0][1].get('padre') if b else '?'
    gr = g.get(k, {})
    na.append((T(enq), k, typ, par, 'guestAlta' in gr or 'alta' in gr, 'primera-Req' in gr, 'baja' in gr, [t for t in e]))
for r in sorted(na): print(f'{r[0]:+7.2f} {r[1]} tipo={r[2]} padre={r[3]} galta={r[4]} gReq={r[5]} gbaja={r[6]} tags={r[7]}')
# >5s applied
print('== applied >5s after enqueue')
for k, e in ev.items():
    if '2005 encolado al invitado' in e and 'acción OnServer' in e:
        enq = e['2005 encolado al invitado'][0][0]; ap = e['acción OnServer'][0][0]
        if ap - enq > 5e6:
            print(k, 'enq', f'{T(enq):+.2f}', 'applied', f'{T(ap):+.2f}', 'galta', g.get(k,{}).get('alta',(0,))[0]*1e6 - OFF - enq if 'alta' in g.get(k,{}) else None, [t for t in e])
# guest first Req absolute time histogram (host arm-relative)
print('== guest first-Req instants (arm-relative, 0.1 s bins) for tracked')
h = collections.Counter()
for k, e in ev.items():
    if '2005 encolado al invitado' in e and 'primera-Req' in g.get(k, {}):
        h[round(T(g[k]['primera-Req'][0]*1e6 - OFF), 1)] += 1
for t in sorted(h): print(f'{t:+.1f}: {h[t]}')
print('== enqueue instants (0.5s bins)')
h = collections.Counter()
for k, e in ev.items():
    if '2005 encolado al invitado' in e: h[round(T(e['2005 encolado al invitado'][0][0])*2)/2] += 1
for t in sorted(h): print(f'{t:+.1f}: {h[t]}')
# timeline of sin progreso / retirada local roots / veto
print('== containment timeline per 2 s (arm-relative)')
h = collections.defaultdict(collections.Counter)
for q, tag, d in allev:
    if tag in ('sin progreso', 'retirada local', 'nodo bloqueado', 'baja', 'nacimiento', 'familia', 'contención rechazada', 'veto omitido'):
        h[int(T(q)//2*2)][tag] += 1
for t in sorted(h):
    if -2 <= t <= 60: print(t, dict(h[t]))
# retirada local: dato/adicional values
c = collections.Counter((d.get('asignado'), d.get('dato'), d.get('adicional')) for q, tag, d in allev if tag == 'retirada local')
print('retirada local dato/adicional', c.most_common(12))
c = collections.Counter(d.get('dato') for q, tag, d in allev if tag == 'baja')
print('baja motivos', c.most_common())
c = collections.Counter(d.get('dato') for q, tag, d in allev if tag == 'contención rechazada')
print('rechazos', c.most_common())
nb = [(T(q), d.get('stage'), d.get('recurso'), d.get('nodo'), d.get('dato'), d.get('adicional')) for q, tag, d in allev if tag == 'nodo bloqueado']
print('nodo bloqueado', len(nb)); [print(x) for x in nb[:10]]
# guest view: how many retired keys got a guest baja and when relative
rr = [d['key'] for q, tag, d in allev if tag == 'retirada local' and d.get('asignado') == '1']
print('retired keys(assigned=1 lines)', len(set(rr)), 'with guest baja', sum(1 for k in set(rr) if 'baja' in g.get(k, {})))
mot = collections.Counter(re.search(r'motivo (\d+)', g[k]['baja'][2]).group(1) for k in set(rr) if 'baja' in g.get(k, {}))
print('guest baja motivo for retired', mot)
