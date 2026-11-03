import re, sys, collections, statistics
# Guest-side family census from NpcProbe lines (read-only).
ALTA = re.compile(r'\[invitado alta\].*?capture t=([\d.]+) .*?clave (0x[0-9A-F]+) .*?tipo (\d+) controlador (0x[0-9A-F]+) padre (0x[0-9A-F]+) hijos (\d+)')
FAM = re.compile(r'\[invitado familia\].*?capture t=([\d.]+) .*?raiz (0x[0-9A-F]+) gen \d+ hijos (\d+) \[([^\]]*)\]')
REQ = re.compile(r'\[invitado primera-Req\].*?capture t=([\d.]+) .*?clave (0x[0-9A-F]+) .*?op (\d+)')
BAJA = re.compile(r'\[invitado baja\].*?capture t=([\d.]+) .*?clave (0x[0-9A-F]+) .*?motivo (\d+)')
HOLD = re.compile(r'RemoteUpdateGuard: holding actor (0x[0-9A-F]+) \(control (0x[0-9A-F]+)')
CRASH = re.compile(r'crash capture.*exception (0x[0-9A-F]+) at (exe\+0x[0-9A-F]+)')

def run(path):
    alta = {}; fam = {}; req = {}; baja = {}; held = set(); crashes = []
    local = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = ALTA.search(line)
        if m:
            t, k, ty, c, p, h = m.groups()
            alta.setdefault(k, (float(t), int(ty), c, p, int(h)))
            continue
        m = FAM.search(line)
        if m:
            t, r, n, lst = m.groups()
            fam.setdefault(r, (float(t), [x for x in lst.split(',') if x]))
            continue
        m = REQ.search(line)
        if m:
            t, k, op = m.groups(); req.setdefault(k, (float(t), int(op))); continue
        m = BAJA.search(line)
        if m:
            t, k, mo = m.groups(); baja.setdefault(k, (float(t), int(mo))); continue
        m = HOLD.search(line)
        if m: held.add(m.group(1)); continue
        m = CRASH.search(line)
        if m and 'unhandled' in line: crashes.append(m.groups())
    # user 2 key: the most frequent non-host controller of type 3
    ctl = collections.Counter(v[2] for v in alta.values() if v[1] == 3 and v[2] != '0x90100000')
    u2 = ctl.most_common(1)[0][0] if ctl else None
    print('==', path, '| user2', u2, '| altas', len(alta), '| familias', len(fam), '| crashes', crashes)
    # census by type x controller x parent
    cen = collections.Counter()
    for k, (t, ty, c, p, h) in alta.items():
        who = 'u2' if c == u2 else ('host' if c == '0x90100000' else c)
        cen[(ty, who, 'hijo' if p != '0x00000000' else 'raiz')] += 1
    for x in sorted(cen): print('  alta tipo %d ctl %-5s %-5s %d' % (x + (cen[x],)))
    # families whose root is user 2's
    rows = []; split = collections.Counter(); child_types = collections.Counter()
    for r, (tf, kids) in fam.items():
        ra = alta.get(r)
        if not ra or ra[2] != u2: continue
        kinfo = [(c, alta.get(c)) for c in kids]
        host_kids = [c for c, a in kinfo if a and a[2] != u2]
        for c, a in kinfo:
            if a: child_types[(a[1], 'u2' if a[2] == u2 else 'host')] += 1
            else: child_types[('nunca', '-')] += 1
        if not host_kids: split['familia entera en u2'] += 1; continue
        split['familia partida'] += 1
        last_child = max((a[0] for c, a in kinfo if a), default=None)
        rq = req.get(r)
        d_alta = rq[0] - ra[0] if rq else None
        d_child = rq[0] - last_child if rq and last_child is not None else None
        rows.append((r, ra[0], len(kids), len(host_kids), last_child, rq[0] if rq else None, d_alta, d_child,
                     baja.get(r), sum(1 for c in host_kids if c in held)))
    print('  familias raiz u2:', dict(split), '| hijos por (tipo, ctl):', dict(child_types))
    for r in sorted(rows, key=lambda x: x[1]):
        k, ta, n, nh, tc, tr, da, dc, b, nhold = r
        f = lambda v: '-' if v is None else '%.3f' % v
        print('   raiz %s alta %s hijos %d (host %d, contenidos sin accion %d) ultimo hijo %s primera Req %s | Req-alta %s Req-ultimo-hijo %s | baja %s'
              % (k, f(ta), n, nh, nhold, f(tc), f(tr), f(da), f(dc), b))
    # roots of u2 without children: first Req delay distribution (baseline)
    base = [req[k][0] - a[0] for k, a in alta.items() if a[2] == u2 and a[3] == '0x00000000' and a[4] == 0 and k in req]
    if base:
        base.sort()
        print('  linea base u2 sin hijos: n %d mediana %.3f p95 %.3f max %.3f' % (len(base), statistics.median(base), base[int(.95 * (len(base) - 1))], base[-1]))
    with_kids_all_u2 = []
    for r, (tf, kids) in fam.items():
        ra = alta.get(r)
        if ra and ra[2] == u2 and r in req and all(alta.get(c) and alta[c][2] == u2 for c in kids):
            with_kids_all_u2.append(req[r][0] - ra[0])
    if with_kids_all_u2:
        with_kids_all_u2.sort()
        print('  familias enteras u2: n %d mediana %.3f max %.3f' % (len(with_kids_all_u2), statistics.median(with_kids_all_u2), with_kids_all_u2[-1]))
    # host-controlled children of u2 roots that the guard held (no action)
    print('  RemoteUpdateGuard retenidos:', len(held))

for p in sys.argv[1:]: run(p)
