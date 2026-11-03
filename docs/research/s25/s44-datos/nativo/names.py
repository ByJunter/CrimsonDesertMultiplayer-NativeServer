import re, os, sys, glob, struct, collections, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read
root = sys.argv[1]
pat = [re.compile(r"'(cd_[a-z0-9_]+)'[^|]{0,40}?\(key 0x([0-9A-F]+)\)"), re.compile(r"'(cd_[a-z0-9_]+)' sequencer id 0x([0-9A-F]+)"),
       re.compile(r"'(cd_[a-z0-9_]+)'[^|]{0,60}?key 0x([0-9A-F]+)"), re.compile(r"seq(?:uencer)? 0x([0-9A-F]+)[^|]{0,80}?'(cd_[a-z0-9_]+)'")]
res_names = collections.defaultdict(collections.Counter)
dirs = collections.defaultdict(lambda: {'logs': [], 'caps': []})
for dp, dn, fn in os.walk(root):
    for f in fn:
        p = os.path.join(dp, f)
        if f.startswith('cdmp_packets') and f.endswith('.bin') and os.path.getsize(p) > 1e6: dirs[dp]['caps'].append(p)
        elif f.endswith('.log') and not f.startswith('cdmp_packets') and os.path.getsize(p) > 1000: dirs[dp]['logs'].append(p)
for d, v in dirs.items():
    if not v['caps'] or not v['logs']: continue
    key2res = collections.defaultdict(set)
    for c in v['caps']:
        for t, dd, op, p in read(c):
            if len(p) >= 31: key2res[struct.unpack_from('<Q', p, 9)[0]].add(struct.unpack_from('<I', p, 27)[0])
    for lg in v['logs']:
        with open(lg, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                if "'cd_" not in line: continue
                for i, rx in enumerate(pat):
                    for m in rx.finditer(line):
                        if i == 3: k, n = int(m.group(1), 16), m.group(2)
                        else: n, k = m.group(1), int(m.group(2), 16)
                        for r in key2res.get(k, ()): res_names[f"{r:08X}"][n] += 1
out = {r: dict(c.most_common(4)) for r, c in res_names.items()}
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'resnames.json'), 'w'), indent=1)
print(len(out))
