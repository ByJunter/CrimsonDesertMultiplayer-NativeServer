# Which host 2661 were prepared by the stage-element path 0x287C200 (ShopProbe [stage]/[funcnpc]) on the host. Read only.
import sys, re, json
from collections import defaultdict, Counter
names = json.load(open(sys.argv[1]))
def nm(h):
    e = names.get('%08X' % h); return sorted(e)[0] if e else '?'
RX_SD = re.compile(r'^\[(\S+)\].*SceneDiag \[session 1 \(sniffer\)\].*?2661 PrepareSequencerAck actor 0x\w+ owner 0xA0100001 sequencer 0x(\w+) resource 0x(\w+)')
RX_SP = re.compile(r'^\[(\S+)\].*ShopProbe \[(stage|funcnpc)\].*?prepared by 0x287C200: \'([^\']*)\' sequencer id 0x(\w+), owner 0x(\w+)')
res = defaultdict(Counter)
for f in sys.argv[2:]:
    sd = {}; sp = {}; nstage = 0; last_stage = None
    for line in open(f, encoding='utf-8', errors='replace'):
        m = RX_SD.search(line)
        if m:
            sd.setdefault(int(m.group(2), 16), (m.group(1), int(m.group(3), 16)))
            continue
        m = RX_SP.search(line)
        if m and m.group(5) == 'A0100001':
            sp[int(m.group(4), 16)] = m.group(3)
            if m.group(2) == 'stage':
                nstage += 1; last_stage = m.group(1)
    cutoff = last_stage if nstage >= 24 else '99'
    for seq, (t, r) in sd.items():
        if t > cutoff and seq not in sp:
            res[nm(r)]['after-cap'] += 1
            continue
        res[nm(r)]['287C200' if seq in sp else 'NOT-287C200'] += 1
for k, v in sorted(res.items()):
    print(k.ljust(48), dict(v))
