import json, sys, collections
HOST = '90100000'
for path in sys.argv[1:]:
    a = json.load(open(path))
    ctl = collections.Counter(v['ctl'] for v in a.values() if v['type'] == 3 and v['ctl'] != HOST)
    u2 = ctl.most_common(1)[0][0]
    print('==', path, 'u2', u2, 'records', len(a))
    roots = {k: v for k, v in a.items() if v['ctl'] == u2 and v['type'] == 3 and (not v['parent'] or v['parent'] == '00000000')}
    fam = collections.Counter(); names = collections.Counter(); rootnames = collections.Counter()
    for k, v in roots.items():
        kids = v['kids']
        if not kids: continue
        rootnames[v['name']] += 1
        stack = [(c, 1) for c in kids]
        while stack:
            c, depth = stack.pop()
            cv = a.get(c)
            if not cv:
                fam[('nunca recibido', '-', depth)] += 1; continue
            who = 'u2' if cv['ctl'] == u2 else ('host' if cv['ctl'] == HOST else cv['ctl'])
            fam[(cv['type'], who, depth)] += 1
            names[(cv['type'], who, cv['name'], v['name'])] += 1
            stack += [(g, depth + 1) for g in cv['kids']]
    for x in sorted(fam, key=str): print('  hijo tipo %s ctl %s prof %s: %d' % (x + (fam[x],)))
    for x in sorted(names, key=str):
        if x[1] != 'u2' or x[0] != 3: print('   ', x, names[x])
    print('  raices u2 con hijos por nombre:', dict(rootnames.most_common(12)))
    others = collections.Counter((v['type'], v['name']) for v in a.values() if v['ctl'] == u2 and v['type'] != 3)
    print('  otros actores con ctl u2:', dict(others))
    for k in ('B0100578', 'B01004A5', 'B01004BF', 'B01004C8'):
        if k in a: print('  ', k, a[k])
