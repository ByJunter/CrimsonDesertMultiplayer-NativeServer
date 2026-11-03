import sys, os, struct, collections; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import REC
data = open(sys.argv[1],'rb').read(); pos = 12; c = collections.Counter(); first = {}
key = bytes.fromhex(sys.argv[2])
while pos + REC.size <= len(data):
    t, d, _, size, copied, _, sess, th = REC.unpack_from(data, pos); pos += REC.size
    pkt = data[pos:pos+copied]; pos += copied
    if len(pkt) < 5: continue
    op = struct.unpack_from('<H', pkt, 0)[0]
    p = pkt[5:]
    if d == int(sys.argv[3]) and key in p[:16]:
        c[op] += 1; first.setdefault(op, (t, p[:24].hex()))
print(dict(d=sys.argv[3])); 
for op, n in c.most_common(): print(op, n, first[op])
