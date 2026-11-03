import re, sys, collections, datetime
H = sys.argv[1]; G = sys.argv[2]; T0 = sys.argv[3]; T1 = sys.argv[4]
# host: qpc<->wall anchor and events
hev = []  # (host_qpc_s, kind, info)
anchor = None
rx_u2 = re.compile(r'User2Stages \[([^\]]+)\] qpc ([\d.]+) s \(~([\d:.]+)\)(.*)$')
rx_act = re.compile(r'NpcActivation \[([^\]]+)\] qpc=(\d+) T\d+ \| (.*)$')
kv = re.compile(r'(\w+)=(\S+)')
enq = {}
for ln in open(H, encoding='utf-8', errors='replace'):
    m = rx_u2.search(ln)
    if m:
        tag, q, wall, rest = m.groups(); q = float(q)
        if anchor is None: anchor = (q, wall)
        if tag in ('create', 'delete', 'ARMED', 'ARM requested', 'DRAINED now', 'drain at host prepare', 'CONTROL ABORT', 'control', 'timer', 'lists built', 'evaluation', 'goodbye while ARMED', 'would DRAIN', 'DRAIN held'):
            if tag == 'timer' and '2043' not in rest: continue
            who = 'user2' if '(user 2)' in rest and tag in ('create', 'delete', 'timer') else ('host' if '(host)' in rest else '')
            hev.append((q, tag, who, rest[:160]))
        continue
    m = rx_act.search(ln)
    if m:
        d = dict(kv.findall(m.group(3)))
        if m.group(1) == '2005 encolado al invitado' and d.get('key') not in enq: enq[d['key']] = int(m.group(2)) / 1e6
def hwall(q):
    aq, aw = anchor
    t = datetime.datetime.strptime(aw, '%H:%M:%S.%f') + datetime.timedelta(seconds=q - aq)
    return t.strftime('%H:%M:%S.%f')[:-3]
# guest
grx = re.compile(r'NpcProbe \[invitado (alta|baja)\] qpc ([\d.]+) s .*?clave (0x[0-9A-F]+)(.*)$')
srx = re.compile(r'GuestSchedRoute \[(2126|2335)[^\]]*\] qpc ([\d.]+) s .*?of character (0x[0-9A-F]+)')
gev = []
galta = {}
for ln in open(G, encoding='utf-8', errors='replace'):
    m = grx.search(ln)
    if m:
        tag, q, k, rest = m.groups(); q = float(q)
        if tag == 'alta':
            c = re.search(r'tipo (\d+) controlador (0x[0-9A-F]+) padre (0x[0-9A-F]+)', rest)
            gev.append((q, 'alta', k, c.groups() if c else None))
            galta.setdefault(k, q)
        else:
            mo = re.search(r'motivo (\d+)', rest)
            gev.append((q, 'baja', k, mo.group(1) if mo else '?'))
        continue
    m = srx.search(ln)
    if m:
        gev.append((float(m.group(2)), m.group(1), m.group(3), None))
diffs = [galta[k] - q for k, q in enq.items() if k in galta]
off = min(diffs)
print('guest-host qpc offset (min)', off, 'pairs', len(diffs))
# live set
live = {}
tl = []
for q, kind, k, info in sorted(gev):
    hq = q - off
    if kind == 'alta': live[k] = info
    elif kind == 'baja': live.pop(k, None)
    tl.append((hq, kind, k, info, len(live), dict(collections.Counter((v[1] if v else '?') for v in live.values()))))
def w2q(w):
    aq, aw = anchor
    d = (datetime.datetime.strptime(w, '%H:%M:%S') - datetime.datetime.strptime(aw.split('.')[0], '%H:%M:%S')).total_seconds()
    return aq + d - float('0.' + aw.split('.')[1])
q0, q1 = w2q(T0), w2q(T1)
# per-second bins
bins = collections.defaultdict(collections.Counter)
lastlive = {}
for hq, kind, k, info, n, byc in tl:
    if not (q0 <= hq < q1): continue
    s = int(hq - q0)
    if kind == 'alta': bins[s]['alta ' + (info[1] if info else '?') + ' t' + (info[0] if info else '?')] += 1
    elif kind == 'baja': bins[s]['baja m' + info] += 1
    else: bins[s][kind + (' host' if k == '0xA0100001' else ' u2')] += 1
    lastlive[s] = (n, byc)
hb = collections.defaultdict(list)
for q, tag, who, rest in hev:
    if q0 <= q < q1: hb[int(q - q0)].append(tag + ('(' + who + ')' if who else ''))
prev = None
for s in range(int(q1 - q0)):
    if s in lastlive: prev = lastlive[s]
    hc = collections.Counter(hb.get(s, []))
    if bins.get(s) or hc:
        print(hwall(q0 + s), 'live', prev[0] if prev else '?', prev[1] if prev else '', '| guest', dict(bins.get(s, {})), '| host', dict(hc))
