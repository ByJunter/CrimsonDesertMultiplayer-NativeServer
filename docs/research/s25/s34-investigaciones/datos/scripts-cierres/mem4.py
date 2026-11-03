import sys
sys.path.insert(0, r'<scratch>\crash_ue')
from md import *
d = Dump(sys.argv[1])
for a in sys.argv[2:]:
    nm, rest = a.split('=')
    base, lo, hi = [int(x, 16) for x in rest.split(':')]
    for off in range(lo, hi, 8):
        v = d.q(base + off)
        print(nm, '+0x%03X' % off, 'None' if v is None else '%016x' % v)
