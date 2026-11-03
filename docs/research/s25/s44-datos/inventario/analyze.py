# S44 inventory: every 2661 the guest client received (captures + guest logs), with what the mod did. Read only.
# usage: analyze.py <backups_dir> <caps.jsonl> <names.json> <outdir>
import sys, os, re, json, struct, glob, hashlib, csv
from collections import defaultdict, Counter

B, CAPS, NAMES, OUT = sys.argv[1:5]
HOST = 0xA0100001

# ---------------------------------------------------------------- guest logs (deduplicated runs)
def guest_logs():
    pats = ['p8-2inst-*/guest.log', 'p8-2inst-*/cdmp.guest*.log', 'p8-enemigos-invitado-20261015/guest.log',
            'p8-sesion*-shadow/*.log', 'p8-sesion9-shadow/*/cdmp.log', 'p8-sesion38/shadow/*.log', 'p8-sesion34/shadow*/*.log']
    files = []
    for p in pats:
        files += [f for f in glob.glob(os.path.join(B, p)) if 'cdmp_packets' not in os.path.basename(f)]
    runs = {}
    for f in sorted(set(files)):
        with open(f, 'rb') as h:
            head = h.read(400)
        if not head:
            continue
        key = hashlib.md5(head).hexdigest()
        sz = os.path.getsize(f)
        r = runs.setdefault(key, {'files': [], 'best': None, 'size': -1})
        r['files'].append(os.path.relpath(f, B).replace('\\', '/'))
        if sz > r['size']:
            r['size'], r['best'] = sz, f
    return runs

RX_PFX = re.compile(r'^\[(\d\d):(\d\d):(\d\d)\.(\d+)\] (\w+) \[T\d+\] (.*)$')
RX_CAPT = re.compile(r'capture t=([0-9.]+)')
RX_APPROX = re.compile(r'\(~(\d\d):(\d\d):(\d\d)\.(\d+)\)')
RX_IN = re.compile(r'GuestStage \[2661 in 0x\w+\].*?\| 2661 payload (\d+) bytes \([^)]*\) kind (\d+) \| corr=(\S+) \| addressed actor 0x(\w+) owner 0x(\w+) sequencer 0x(\w+) \| bindings (\d+):? ?(.*?) \| payload')
RX_BIND = re.compile(r'0x(\w+)->0x(\w+)\(type (\d+)\)')
RX_MORE = re.compile(r'\(\+(\d+) more\)')
RX_ROUTE = re.compile(r'GuestSeqRoute \[2661 PrepareSequencerAck[^\]]*\].*?corr=(\S+) \| packet actor 0x(\w+).*?\(character 0x(\w+)\)(.*)$')
RX_RES = re.compile(r'\| resource 0x([0-9A-F]+)')
RX_RESULT = re.compile(r'GuestStage \[2661 result 0x\w+\].*?\| sequencer 0x(\w+) of 0x(\w+) in manager 0x\w+: 0x\w+ result (0x\w+) = (\w+)')
RX_MISSING = re.compile(r'GuestStage \[2661 missing actor 0x\w+\].*?\| sequencer 0x(\w+) \(owner 0x(\w+)\).*?bound actor 0x(\w+) \(node 0x(\w+), type (\d+)\)')
RX_XVETO = re.compile(r'GuestXVeto \[(\w+)\].*?\| (VETOED \([^)]*\)|WOULD VETO \(probe\)|ALLOWED \([^)]*\)|[^|]*?) (\w+)( on| off| add| remove)? of manager 0x\w+ \(character 0x(\w+);.*?stage \'([^\']*)\' 0x\w+ \(key 0x(\w+)\)')
RX_2981 = re.compile(r'GuestSeqRoute \[2981 [^\]]*\].*?corr=2981:(\w+):0x(\w+):0x(\w+) .*?\| (ROUTED[^|]*|DENIED[^|]*|kept[^|]*|left[^|]*|NOT[^|]*|[^|]*?)(?: \||$)')
RX_2810 = re.compile(r'GuestStage \[2810 in 0x\w+\].*?corr=2810:(\w+):0x(\w+):0x(\w+)')
RX_MARK = re.compile(r'=== MARK (\d+) \(Ctrl\+F12\) ===')
RX_PLAY3 = re.compile(r'GuestSeqPlay \[3\]')
RX_NAMEKEY = re.compile(r"'(cd_[a-z0-9_]+)'[^|]*?(?:\(key 0x(\w+)\)|sequencer id 0x(\w+))")
RX_INI_DENY = re.compile(r'guest_seq_route_deny: a 2661[^)]*\): ((?:0x[0-9A-F]+ ?)+)')

def secs(h, m, s, frac):
    return int(h) * 3600 + int(m) * 60 + int(s) + float('0.' + frac)

def parse_guest_log(path):
    d = {'in': [], 'route': [], 'result': [], 'missing': [], 'xveto': [], 'r2981': [], 'd2810': [], 'marks': [],
         'play3': [], 'names': {}, 'guest_key': None, 'offsets': [], 'deny': None, 'first': None, 'last': None}
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for ln, line in enumerate(f, 1):
            m = RX_PFX.match(line)
            if not m:
                continue
            wall = secs(*m.group(1, 2, 3, 4))
            if d['first'] is None:
                d['first'] = m.group(1) + ':' + m.group(2) + ':' + m.group(3)
            d['last'] = m.group(1) + ':' + m.group(2) + ':' + m.group(3)
            body = m.group(6)
            ct = RX_CAPT.search(body)
            ap = RX_APPROX.search(body)
            capt = float(ct.group(1)) if ct else None
            approx = secs(*ap.group(1, 2, 3, 4)) if ap else None
            if capt is not None and approx is not None and len(d['offsets']) < 2000:
                d['offsets'].append(approx - capt)
            ev_wall = approx if approx is not None else wall
            if 'MARK' in body:
                mm = RX_MARK.search(body)
                if mm:
                    d['marks'].append({'n': int(mm.group(1)), 'wall': wall, 'line': ln})
                    continue
            if d['deny'] is None and 'guest_seq_route_deny: a 2661' in body:
                md = RX_INI_DENY.search(body)
                if md:
                    d['deny'] = md.group(1).strip()
            if 'GuestStage [2661 in' in body:
                mi = RX_IN.search(body)
                if mi:
                    binds = [(int(a, 16), int(b, 16), int(t)) for a, b, t in RX_BIND.findall(mi.group(8))]
                    more = RX_MORE.search(mi.group(8))
                    d['in'].append({'line': ln, 'wall': ev_wall, 'capt': capt, 'size': int(mi.group(1)), 'kind': int(mi.group(2)),
                                    'corr': mi.group(3), 'actor': int(mi.group(4), 16), 'owner': int(mi.group(5), 16),
                                    'seq': int(mi.group(6), 16), 'nb': int(mi.group(7)), 'binds': binds,
                                    'more': int(more.group(1)) if more else 0})
                continue
            if 'GuestSeqRoute [2661' in body and body.startswith('GuestSeqRoute'):
                mr = RX_ROUTE.search(body)
                if mr:
                    if d['guest_key'] is None:
                        d['guest_key'] = int(mr.group(3), 16)
                    tail = mr.group(4)
                    verdict = 'DENIED' if 'DENIED' in tail else ('ROUTED' if 'ROUTED' in tail else re.sub(r'0x[0-9A-F]+', 'H', tail.split('|')[-1].strip())[:80])
                    seg = [s.strip() for s in tail.split('|')]
                    vtext = next((s for s in seg if s.startswith(('ROUTED', 'DENIED', 'NOT', 'kept', 'left', 'REFUSED', 'not', 'own', 'WOULD'))), '')
                    rs = RX_RES.search(body)
                    corr = mr.group(1)
                    _, a, s, n = corr.split(':')
                    d['route'].append({'line': ln, 'wall': ev_wall, 'capt': capt, 'corr': corr, 'owner_from_corr': int(a, 16),
                                       'seq': int(s, 16), 'verdict': verdict, 'vtext': vtext[:160],
                                       'resource': int(rs.group(1), 16) if rs else None})
                continue
            if 'GuestStage [2661 result' in body:
                mr = RX_RESULT.search(body)
                if mr:
                    d['result'].append({'line': ln, 'seq': int(mr.group(1), 16), 'owner': int(mr.group(2), 16),
                                        'code': mr.group(3), 'res': mr.group(4), 'wall': ev_wall})
                continue
            if 'GuestStage [2661 missing' in body:
                mr = RX_MISSING.search(body)
                if mr:
                    d['missing'].append({'line': ln, 'seq': int(mr.group(1), 16), 'owner': int(mr.group(2), 16),
                                         'actor': int(mr.group(3), 16), 'node': int(mr.group(4), 16), 'type': int(mr.group(5))})
                continue
            if body.startswith('GuestXVeto ['):
                mx = RX_XVETO.search(body)
                if mx:
                    d['xveto'].append({'line': ln, 'wall': ev_wall, 'cat': mx.group(1), 'how': mx.group(2).split(' (')[0],
                                       'handler': mx.group(3) + (mx.group(4) or ''), 'char': int(mx.group(5), 16),
                                       'name': mx.group(6), 'key': int(mx.group(7), 16)})
                continue
            if body.startswith('GuestSeqRoute [2981'):
                m2 = RX_2981.search(body)
                if m2:
                    d['r2981'].append({'line': ln, 'wall': ev_wall, 'owner': int(m2.group(1), 16), 'seq': int(m2.group(2), 16),
                                       'state': int(m2.group(3), 16), 'v': m2.group(4)[:60]})
                continue
            if 'GuestStage [2810 in' in body:
                m2 = RX_2810.search(body)
                if m2:
                    d['d2810'].append({'line': ln, 'wall': ev_wall, 'owner': int(m2.group(1), 16), 'seq': int(m2.group(2), 16)})
                continue
            if body.startswith('GuestSeqPlay [3]'):
                d['play3'].append({'line': ln, 'wall': ev_wall})
                continue
            if "'cd_" in body:
                for nm, k1, k2 in RX_NAMEKEY.findall(body):
                    d['names'].setdefault(int(k1 or k2, 16), set()).add(nm)
    d['offsets'].sort()
    d['offset'] = d['offsets'][len(d['offsets']) // 2] if d['offsets'] else None
    return d

# ---------------------------------------------------------------- captures
def load_caps():
    caps = defaultdict(lambda: {'p2661': [], 'p2810': [], 'p2981': [], 'p2524': Counter()})
    seen = set()
    for line in open(CAPS):
        r = json.loads(line)
        k = (r['f'], r['tus'], r['op'], r['p'])
        if k in seen:
            continue
        seen.add(k)
        f = r['f'].replace('C:/Emulador CD/backups/', '')
        p = bytes.fromhex(r['p'])
        c = caps[f]
        if r['d'] != 2:
            continue
        if r['op'] == 2661 and len(p) >= 91 and (len(p) - 91) % 11 == 0:
            binds = []
            for i in range(91, len(p), 11):
                binds.append((struct.unpack_from('<I', p, i)[0], struct.unpack_from('<I', p, i + 4)[0], p[i + 8], p[i + 9], p[i + 10]))
            pos = struct.unpack_from('<4f', p, 35)
            c['p2661'].append({'t': r['t'], 'kind': p[0], 'actor': struct.unpack_from('<I', p, 1)[0],
                               'owner': struct.unpack_from('<I', p, 5)[0], 'seq': struct.unpack_from('<Q', p, 9)[0],
                               'u17': struct.unpack_from('<H', p, 17)[0], 'stage': struct.unpack_from('<Q', p, 19)[0],
                               'res': struct.unpack_from('<I', p, 27)[0], 'u31': struct.unpack_from('<I', p, 31)[0],
                               'pos': pos, 'tail': p[51:91].hex(), 'binds': binds, 'size': len(p)})
        elif r['op'] == 2810 and len(p) == 17:
            c['p2810'].append({'t': r['t'], 'actor': struct.unpack_from('<I', p, 0)[0], 'seq': struct.unpack_from('<Q', p, 4)[0]})
        elif r['op'] == 2981 and len(p) >= 17:
            c['p2981'].append({'t': r['t'], 'kind': p[0], 'actor': struct.unpack_from('<I', p, 1)[0],
                               'seq': struct.unpack_from('<Q', p, 5)[0], 'state': struct.unpack_from('<I', p, 13)[0]})
        elif r['op'] == 2524 and len(p) >= 13:
            c['p2524'][struct.unpack_from('<Q', p, 5)[0]] += 1
    return caps

# ---------------------------------------------------------------- names
def load_names(needed):
    allnames = json.load(open(NAMES))
    out = {}
    for h in needed:
        e = allnames.get('%08X' % h)
        if e:
            bases = sorted(e.keys())
            # prefer a sequencer/ path
            seqb = [b for b in bases if any('sequencer/' in p for p in e[b])]
            out[h] = (seqb or bases)[0] + ('' if len(bases) == 1 else ' (+%d alias)' % (len(bases) - 1))
    return out

def family(name):
    if not name:
        return '?'
    n = name.split(' ')[0]
    for pre, fam in [('cd_seq_boss_rematch', 'boss_rematch'), ('cd_seq_funcnpc', 'funcnpc (tienda/función)'),
                     ('cd_seq_spawn_docu', 'spawn_docu'), ('cd_seq_contents_weather', 'contents_weather'),
                     ('cd_seq_contents', 'contents'), ('cd_seq_quest', 'quest'), ('cd_seq_faction', 'faction'),
                     ('cd_seq_node', 'node'), ('cd_seq_reward', 'reward'), ('cd_minigame', 'minigame'),
                     ('cd_seq_minigame', 'minigame'), ('cd_seq_cutscene', 'cutscene'), ('cd_seq_gimmick', 'gimmick'),
                     ('cd_seq_spawn', 'spawn'), ('cd_seq_ambient', 'ambient'), ('cd_seq_npc', 'npc'),
                     ('cd_seq_daily', 'daily'), ('cd_seq_', None)]:
        if n.startswith(pre):
            if fam:
                return fam
            return n[len('cd_seq_'):].split('_')[0]
    return n.split('_')[0]

def who(a, guest_keys):
    if a == HOST:
        return 'H'
    if a in guest_keys:
        return 'G'
    if (a >> 28) == 0xA:
        return 'A?'
    if (a >> 28) == 0xB:
        return 'N'
    return '%08X' % a

def main():
    os.makedirs(OUT, exist_ok=True)
    runs = guest_logs()
    caps = load_caps()
    parsed = {}
    for key, r in runs.items():
        rel = os.path.relpath(r['best'], B).replace('\\', '/')
        print('parse', rel, file=sys.stderr)
        parsed[rel] = (r, parse_guest_log(r['best']))
    # link captures to runs by (owner, seq) overlap
    cap_of = {}
    for cf, c in caps.items():
        ks = {(x['owner'], x['seq']) for x in c['p2661']}
        best, bs = None, 0
        for rel, (r, d) in parsed.items():
            ls = {(x['owner'], x['seq']) for x in d['in']}
            s = len(ks & ls)
            cdir = os.path.dirname(cf)
            if not any(os.path.dirname(f) == cdir for f in r['files']):
                s = 0
            if s > bs:
                best, bs = rel, s
        if best:
            prev = cap_of.get(best)
            if prev is None or prev[1] < bs:
                if prev is not None:
                    cap_of['(capture only) ' + prev[0][0]] = ([prev[0][0]], 0)
                cap_of[best] = ([cf], bs)
            else:
                cap_of['(capture only) ' + cf] = ([cf], 0)
        else:
            cap_of['(capture only) ' + cf] = ([cf], 0)
    cap_of = {k: v[0] for k, v in cap_of.items()}
    occ = []  # one row per (run, owner, seq, first arrival)
    guest_keys = set()
    for rel, (r, d) in parsed.items():
        if d['guest_key']:
            guest_keys.add(d['guest_key'])
    for rel in sorted(set(list(parsed.keys()) + list(cap_of.keys()))):
        r, d = parsed.get(rel, (None, None))
        cfs = cap_of.get(rel, [])
        cap2661 = []
        c2810 = {}
        c2981 = defaultdict(list)
        c2524 = Counter()
        for cf in cfs:
            for x in caps[cf]['p2661']:
                cap2661.append(dict(x, cf=cf))
            for x in caps[cf]['p2810']:
                c2810.setdefault((x['actor'], x['seq']), x['t'])
            for x in caps[cf]['p2981']:
                c2981[(x['actor'], x['seq'])].append(x['state'])
            c2524.update(caps[cf]['p2524'])
        logs_in = defaultdict(list)
        route = defaultdict(list)
        result = defaultdict(list)
        missing = defaultdict(list)
        xv = defaultdict(list)
        r2981 = defaultdict(list)
        d2810 = {}
        if d:
            for x in d['in']:
                logs_in[(x['owner'], x['seq'])].append(x)
            for x in d['route']:
                route[(x['owner_from_corr'], x['seq'])].append(x)
            for x in d['result']:
                result[(x['owner'], x['seq'])].append(x)
            for x in d['missing']:
                missing[(x['owner'], x['seq'])].append(x)
            for x in d['xveto']:
                xv[(x['char'], x['key'])].append(x)
            for x in d['r2981']:
                r2981[(x['owner'], x['seq'])].append(x)
            for x in d['d2810']:
                d2810.setdefault((x['owner'], x['seq']), x)
        keys = set(logs_in) | {(x['owner'], x['seq']) for x in cap2661}
        capby = defaultdict(list)
        for x in cap2661:
            capby[(x['owner'], x['seq'])].append(x)
        off = d['offset'] if d else None
        marks = d['marks'] if d else []
        for k in keys:
            owner, seq = k
            li = logs_in.get(k, [])
            ci = capby.get(k, [])
            rt = route.get(k, [])
            # also route keyed by addressed actor (corr actor = packet actor)
            if not rt and li:
                rt = route.get((li[0]['actor'], seq), [])
            res = None
            if ci:
                res = ci[0]['res']
            elif rt and rt[0]['resource'] is not None:
                res = rt[0]['resource']
            if ci:
                b = ci[0]['binds']
                binds = [(x[0], x[1], x[2]) for x in b]
                nb = len(b)
                kind = ci[0]['kind']
                actor = ci[0]['actor']
                t_first = ci[0]['t']
                wall = (t_first + off) if off is not None else None
                stage = ci[0]['stage']
                pos = ci[0]['pos']
                tail = ci[0]['tail']
                u17 = ci[0]['u17']
                complete = True
            else:
                x = li[0]
                binds = x['binds']
                nb = x['nb']
                kind = x['kind']
                actor = x['actor']
                wall = x['wall']
                t_first = x['capt']
                stage = None
                pos = None
                tail = None
                u17 = None
                complete = x['more'] == 0
            closed = None
            if (actor, seq) in c2810 or (owner, seq) in c2810:
                tclose = c2810.get((actor, seq), c2810.get((owner, seq)))
                closed = (tclose + off) if off is not None else None
                closed_rel = tclose - (t_first or 0)
            elif k in d2810:
                closed = d2810[k]['wall']
            else:
                closed = None
            live_at_marks = []
            for mk in marks:
                if wall is not None and wall <= mk['wall'] and (closed is None or closed > mk['wall']):
                    live_at_marks.append('M%d' % mk['n'])
            nmarks_near = [('M%d' % mk['n'], round(mk['wall'] - wall, 1)) for mk in marks
                           if wall is not None and abs(mk['wall'] - wall) <= 120]
            xvk = xv.get((owner, seq), [])
            occ.append({
                'run': rel, 'folders': ';'.join(sorted({f.split('/')[0] for f in (r['files'] if r else cfs)})),
                'captures': ';'.join(cfs), 'owner': owner, 'actor': actor, 'seq': seq, 'kind': kind,
                'resource': res, 'nb': nb, 'binds': binds, 'binds_complete': complete,
                'stage': stage, 'u17': u17, 'pos': pos, 'tail': tail,
                'n_arrivals': max(len(li), len(ci)),
                'wall': wall, 'closed': closed,
                'verdicts': [x['verdict'] for x in rt], 'vtext': rt[0]['vtext'] if rt else '',
                'results': [x['res'] + ' ' + x['code'] for x in result.get(k, [])],
                'missing': ['%08X node %08X t%d' % (x['actor'], x['node'], x['type']) for x in missing.get(k, [])],
                'xveto': sorted({'%s %s' % (x['how'], x['handler']) for x in xvk}),
                'xnames': sorted({x['name'] for x in xvk}),
                'names_log': sorted(d['names'].get(seq, set())) if d else [],
                's2981': sorted(set(c2981.get((actor, seq), []) + c2981.get((owner, seq), []) + [x['state'] for x in r2981.get(k, [])])),
                'n2524': c2524.get(seq, 0),
                'live_at_marks': live_at_marks, 'marks_near': nmarks_near,
                'log_line': li[0]['line'] if li else None, 'route_line': rt[0]['line'] if rt else None,
                'guest_key': d['guest_key'] if d else None, 'deny': d['deny'] if d else None,
            })
    # names
    need = {o['resource'] for o in occ if o['resource'] is not None}
    names = load_names(need)
    for o in occ:
        gk = set(guest_keys)
        if o['guest_key']:
            gk.add(o['guest_key'])
        o['name'] = names.get(o['resource']) if o['resource'] is not None else None
        if not o['name'] and o['xnames']:
            o['name'] = o['xnames'][0] + ' (log)'
        o['family'] = family(o['name'])
        o['owner_cls'] = who(o['owner'], gk)
        o['sig'] = ' '.join('%X:%s/%d' % (b[0] & 0xFF, who(b[1], gk), b[2]) for b in o['binds'])
        o['host_roles'] = sorted({'0x%08X' % b[0] for b in o['binds'] if b[1] == HOST})
        o['guest_roles'] = sorted({'0x%08X' % b[0] for b in o['binds'] if who(b[1], gk) == 'G'})
        o['n_npc'] = sum(1 for b in o['binds'] if who(b[1], gk) == 'N')
        o['positioned'] = None if o['pos'] is None else any(abs(v) > 1e-6 for v in o['pos'][:3])
    def ser(o):
        o = dict(o)
        o['binds'] = ['0x%08X->0x%08X(t%d)' % b for b in o['binds']]
        for k in ('owner', 'actor', 'seq', 'resource', 'stage', 'guest_key'):
            if o.get(k) is not None:
                o[k] = '0x%X' % o[k]
        return o
    json.dump([ser(o) for o in occ], open(os.path.join(OUT, 'occurrences.json'), 'w'), indent=1, default=str)
    with open(os.path.join(OUT, 'occurrences.csv'), 'w', newline='', encoding='utf-8') as f:
        cols = ['run', 'folders', 'owner_cls', 'owner', 'actor', 'seq', 'kind', 'resource', 'name', 'family', 'nb', 'sig',
                'host_roles', 'guest_roles', 'n_npc', 'binds_complete', 'stage', 'u17', 'positioned', 'n_arrivals',
                'verdicts', 'results', 'missing', 'xveto', 's2981', 'n2524', 'live_at_marks', 'marks_near', 'log_line',
                'route_line', 'deny', 'tail']
        w = csv.DictWriter(f, fieldnames=cols, extrasaction='ignore')
        w.writeheader()
        for o in sorted(occ, key=lambda o: (o['run'], o['wall'] or 0)):
            s = ser(o)
            for c in cols:
                if isinstance(s.get(c), (list, tuple)):
                    s[c] = ' '.join(map(str, s[c]))
            w.writerow(s)
    print('occurrences', len(occ), 'runs', len(parsed), 'captures linked', sum(len(v) for v in cap_of.values()), file=sys.stderr)
    for rel, cfs in sorted(cap_of.items()):
        print('LINK', rel, cfs, file=sys.stderr)

main()
