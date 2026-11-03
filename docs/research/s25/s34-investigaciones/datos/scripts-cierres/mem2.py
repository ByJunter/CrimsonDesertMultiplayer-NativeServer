import sys
sys.path.insert(0, r'<scratch>\crash_ue')
from md import *
d = Dump(sys.argv[1])
for a in sys.argv[2:]:
    a = int(a, 16); b = d.read(a, 0x40)
    print(hex(a), b.hex() if b else None)
# list ranges not in stacks
big = [(hex(s), d_) for s, d_, fo in d.ranges if d_ >= 0x1000]
print(len(d.ranges), big[:40])
