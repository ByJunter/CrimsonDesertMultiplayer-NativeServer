import sys, json
sys.path.insert(0, '.')
from h3 import hashlittle
idx, out = sys.argv[1], sys.argv[2]
d = {}
for line in open(idx, encoding='utf-8', errors='replace'):
    p = line.split('\t')
    if len(p) < 2: continue
    path = p[1]
    base = path.rsplit('/', 1)[-1]
    if '.' in base: base = base[:base.rfind('.')]
    h = hashlittle(base)
    d.setdefault('%08X' % h, {}).setdefault(base, []).append(path)
json.dump(d, open(out, 'w'))
for k in ['9586FFA4', '8CD771B1', '519CC9E7', 'A0F3AE60', '1DC703E8']:
    print(k, {b: v[:3] for b, v in d.get(k, {}).items()})
