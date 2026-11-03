# Aggregate occurrences.json by resource (per owner class). Read only.
import sys, json, csv, os, re
from collections import defaultdict, Counter
occ = json.load(open(sys.argv[1]))
out = sys.argv[2]

def short(run):
    run = run.replace('(capture only) ', 'cap:')
    run = re.sub(r'cdmp_packets[._]', '', run)
    run = run.replace('p8-2inst-', '2i-').replace('p8-sesion', 's').replace('cdmp.guest_20261015-', 'g').replace('cdmp_2026', '').replace('/guest.log', '').replace('.log', '')
    return run

agg = defaultdict(lambda: defaultdict(list))
combo = Counter()
for o in occ:
    combo[(o['owner_cls'], 'actor=owner' if o['actor'] == o['owner'] else 'actor!=owner')] += 1
    key = (o['owner_cls'], o['resource'])
    a = agg[key]
    for k in ('run', 'name', 'family', 'kind', 'nb', 'sig', 'stage', 'positioned', 'n_arrivals', 'n2524', 'binds_complete', 'u17', 'tail'):
        a[k].append(o.get(k))
    a['verdicts'] += o['verdicts']
    a['results'] += [r.split(' ')[0] for r in o['results']]
    a['missing'] += o['missing']
    a['xveto'] += o['xveto']
    a['s2981'] += o['s2981']
    a['live_at_marks'] += [short(o['run']) + ':' + m for m in o['live_at_marks']]
    a['marks_near'] += [short(o['run']) + ':' + m + '@' + str(dt) for m, dt in o['marks_near']]
    a['host_roles'] += o['host_roles']
    a['guest_roles'] += o['guest_roles']
    a['n_npc'].append(o['n_npc'])
    if o['tail']:
        t = bytes.fromhex(o['tail'])
        a['b1718'].append('%d/%d' % (o['u17'] & 0xFF, o['u17'] >> 8))
        k55 = int.from_bytes(t[4:8], 'little')
        a['k55'].append('0' if k55 == 0 else ('H' if k55 == 0xA0100001 else ('A(player)' if k55 >> 28 == 0xA else ('N(=stage hi)' if o['stage'] and int(o['stage'], 16) >> 32 == k55 else 'N'))))
        a['last'].append('0x%02X' % t[-1])
        st = int(o['stage'], 16)
        a['stage_form'].append('id' if st >> 32 == 0 else 'key<<32|0x%X' % (st & 0xFFFFFFFF) if (st >> 60) == 0xB else 'hi<<32')
    a['src'].append('%s:%s' % (o['run'], o['log_line']))
    a['seqs'].append('%s/%s' % (short(o['run']), o['seq']))

rows = []
for (cls, res), a in agg.items():
    runs = Counter(short(r) for r in a['run'])
    sigs = Counter(a['sig'])
    rows.append({
        'owner_cls': cls, 'resource': res, 'name': next((n for n in a['name'] if n), ''),
        'family': next((f for f in a['family'] if f and f != '?'), '?'),
        'kinds': ' '.join(str(k) for k in sorted(set(a['kind']))),
        'nb': ' '.join(str(k) for k in sorted(set(a['nb']))),
        'host_roles': ' '.join(sorted(set(a['host_roles']))) or '-',
        'guest_roles': ' '.join(sorted(set(a['guest_roles']))) or '-',
        'npc_binds': ' '.join(str(k) for k in sorted(set(a['n_npc']))),
        'signatures': ' || '.join('%s (x%d)' % (s or '(none)', n) for s, n in sigs.most_common(4)) + (' ...+%d' % (len(sigs) - 4) if len(sigs) > 4 else ''),
        'stages': ' '.join(sorted({hex(int(s, 16)) for s in a['stage'] if s})),
        'positioned': ' '.join(sorted({str(p) for p in a['positioned'] if p is not None})),
        'n_occ': len(a['run']), 'n_runs': len(runs),
        'runs': ' '.join('%s(x%d)' % (r, n) for r, n in sorted(runs.items())),
        'verdicts': ' '.join('%s:%d' % kv for kv in Counter(a['verdicts']).most_common()),
        'results': ' '.join('%s:%d' % kv for kv in Counter(a['results']).most_common()),
        'missing': len(a['missing']),
        'xveto': ' | '.join('%s x%d' % kv for kv in Counter(a['xveto']).most_common()),
        's2981': ' '.join(str(s) for s in sorted(set(a['s2981']))),
        'n2524': sum(a['n2524']),
        'live_at_marks': ' '.join(sorted(set(a['live_at_marks']))),
        'marks_near': ' '.join(sorted(set(a['marks_near']))),
        'complete_binds': all(a['binds_complete']),
        'u17': ' '.join(sorted({str(u) for u in a['u17'] if u is not None})),
        'tails': len({t for t in a['tail'] if t}),
        'b17_b18': ' '.join(sorted(set(a['b1718']))), 'k55': ' '.join(sorted(set(a['k55']))),
        'last_byte': ' '.join(sorted(set(a['last']))), 'stage_form': ' '.join(sorted(set(a['stage_form']))),
        'seqs': ' '.join(a['seqs'][:12]) + (' ...' if len(a['seqs']) > 12 else ''),
        'src_first': a['src'][0],
    })
rows.sort(key=lambda r: (r['owner_cls'], r['family'], r['name'], str(r['resource'])))
cols = list(rows[0].keys())
with open(os.path.join(out, 'by_resource.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=cols)
    w.writeheader()
    w.writerows(rows)
json.dump(rows, open(os.path.join(out, 'by_resource.json'), 'w'), indent=1)
print('combos', combo)
print('resources per owner class', Counter(r['owner_cls'] for r in rows))
