import sys, struct, bisect
sys.path.insert(0, r'<scratch>\crash_ue')
from md import *
d = Dump(sys.argv[1])
def rng(a):
    i = bisect.bisect_right(d.starts, a) - 1
    if i < 0: return None
    s, n, fo = d.ranges[i]
    return (hex(s), hex(n)) if a < s + n else None
for a in sys.argv[2:]:
    nm, a = a.split('=') if '=' in a else (a, a)
    a = int(a, 16)
    print(nm, hex(a), rng(a))
    for off in range(0, 0x100, 8):
        v = d.q(a + off)
        if v is None: continue
        print('   +0x%03X %016x %s' % (off, v, d.sym(v) if d.mod(v) else ''))
