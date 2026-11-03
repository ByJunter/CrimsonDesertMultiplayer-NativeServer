import sys, re, collections, os
for fn in sys.argv[1:]:
    d = open(fn,'rb').read()
    print('==', os.path.basename(fn), len(d))
    c = collections.Counter(m.group().decode() for m in re.finditer(rb'[\x20-\x7e]{5,}', d))
    for s, n in sorted(c.items(), key=lambda x: (-x[1], x[0])): print('%4d %s' % (n, s[:160]))
