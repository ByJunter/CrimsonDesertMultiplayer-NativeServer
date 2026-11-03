# Host-side pass: every 2661 of the host's character its own client got (SceneDiag) and whether observer_seq_copy
# SENT it to user 2. Read only. usage: hostpass.py <backups> <names.json> <outdir> <by_resource.json>
import sys, os, re, glob, hashlib, json, csv
from collections import defaultdict, Counter
B, NAMES, OUT, BYRES = sys.argv[1:5]
pats = ['p8-2inst-*/host*.log', 'p8-2inst-*/cdmp.host*.log', 'p8-enemigos-invitado-20261015/host*.log',
        'p8-sesion*-host/*.log', 'p8-sesion*-host/*/*.log', 'p8-sesion34/*.log', 'p8-sesion34/paso*/*.log', 'p8-sesion38/*.log']
files = sorted({f for p in pats for f in glob.glob(os.path.join(B, p)) if 'cdmp_packets' not in f})
runs = {}
for f in files:
    with open(f, 'rb') as h:
        head = h.read(400)
    if not head:
        continue
    k = hashlib.md5(head).hexdigest()
    r = runs.setdefault(k, {'files': [], 'best': None, 'size': -1})
    r['files'].append(os.path.relpath(f, B).replace('\\', '/'))
    if os.path.getsize(f) > r['size']:
        r['size'], r['best'] = os.path.getsize(f), f
RX_SD = re.compile(r'SceneDiag \[session 1 \(sniffer\)\].*?2661 PrepareSequencerAck actor 0x(\w+) owner 0x(\w+) sequencer 0x(\w+) resource 0x(\w+) bindings (\d+)')
RX_OS = re.compile(r'ObserverStage \[2661 PrepareSequencerAck\].*?owner actor 0x(\w+) sequencer 0x(\w+) \| corr=\S+ \| bindings \d+:? ?(.*?) \| live')
RX_CP = re.compile(r'ObserverSeqCopy \[copy\].*?\| sequencer 0x(\w+) \((\w+)\): SENT')
RX_GUEST = re.compile(r'user 2 = user 0x(\w+) character 0x(\w+)')
per = defaultdict(lambda: {'prep': 0, 'sent': 0, 'runs': set(), 'runs_sent': set(), 'binds': Counter(), 'nb': set()})
runinfo = []
for k, r in runs.items():
    rel = os.path.relpath(r['best'], B).replace('\\', '/')
    prep = {}
    binds = {}
    sent = set()
    guest = None
    with open(r['best'], 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            if 'SceneDiag [session 1 (sniffer)]' in line and '2661' in line:
                m = RX_SD.search(line)
                if m and int(m.group(2), 16) == 0xA0100001:
                    prep.setdefault(int(m.group(3), 16), (int(m.group(4), 16), int(m.group(5))))
            elif 'ObserverStage [2661' in line:
                m = RX_OS.search(line)
                if m and int(m.group(1), 16) == 0xA0100001:
                    binds.setdefault(int(m.group(2), 16), m.group(3))
            elif 'ObserverSeqCopy [copy]' in line:
                m = RX_CP.search(line)
                if m:
                    sent.add(int(m.group(1), 16))
            elif guest is None and 'user 2 = user' in line:
                m = RX_GUEST.search(line)
                if m:
                    guest = m.group(2)
    if not prep:
        continue
    runinfo.append((rel, len(prep), len(sent & set(prep)), guest))
    for seq, (res, nb) in prep.items():
        p = per[res]
        p['prep'] += 1
        p['runs'].add(rel)
        p['nb'].add(nb)
        b = binds.get(seq)
        if b:
            p['binds'][re.sub(r'0xB[0-9A-F]{7}', 'N', b).replace('0xA0100001', 'H')] += 1
        if seq in sent:
            p['sent'] += 1
            p['runs_sent'].add(rel)
allnames = json.load(open(NAMES))
def nm(h):
    e = allnames.get('%08X' % h)
    return sorted(e.keys())[0] if e else ''
guest_res = {r['resource'] for r in json.load(open(BYRES)) if r['owner_cls'] == 'H'}
rows = []
for res, p in per.items():
    rows.append({'resource': '0x%X' % res, 'name': nm(res), 'host_prepared': p['prep'], 'host_runs': len(p['runs']),
                 'sent_to_user2': p['sent'], 'runs_sent': len(p['runs_sent']),
                 'seen_on_guest': ('0x%X' % res) in guest_res,
                 'nb': ' '.join(map(str, sorted(p['nb']))),
                 'binds': ' || '.join('%s x%d' % kv for kv in p['binds'].most_common(2)),
                 'runs_sent_list': ' '.join(sorted(p['runs_sent']))[:400]})
rows.sort(key=lambda r: r['name'])
with open(os.path.join(OUT, 'host_side.csv'), 'w', newline='', encoding='utf-8') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)
for ri in sorted(runinfo):
    print('RUN', ri)
for r in rows:
    print(r['resource'], r['name'][:44].ljust(44), 'prep', r['host_prepared'], 'runs', r['host_runs'], 'SENT', r['sent_to_user2'], 'runs', r['runs_sent'], 'guest-seen', r['seen_on_guest'], '|', r['binds'][:90])
