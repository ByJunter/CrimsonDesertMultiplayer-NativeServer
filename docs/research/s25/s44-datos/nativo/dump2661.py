import struct, sys, glob, os, collections, json
REC = struct.Struct("<QBBHHHII")
def read(path):
    data = open(path,'rb').read()
    if data[:4] != b"CDPK": return
    pos = 12
    while pos + REC.size <= len(data):
        t, d, _, size, copied, _, sess, thread = REC.unpack_from(data, pos)
        pos += REC.size
        pkt = data[pos:pos+copied]; pos += copied
        if len(pkt) < 5: continue
        op = struct.unpack_from("<H", pkt, 0)[0]
        if op == 2661: yield t, d, pkt[5:]
res = collections.defaultdict(lambda: {'n':0,'files':set(),'pd':collections.Counter(),'binds':collections.Counter(),'actor':collections.Counter()})
for path in sys.argv[1:]:
    tag = os.path.basename(os.path.dirname(path)) + '/' + os.path.basename(path)[:26]
    for t, d, p in read(path):
        if len(p) < 91: continue
        kind = p[0]; actor, owner = struct.unpack_from('<II', p, 1); seq = struct.unpack_from('<Q', p, 9)[0]
        a, b = p[17], p[18]
        st = struct.unpack_from('<Q', p, 19)[0]; r = struct.unpack_from('<I', p, 27)[0]
        rest = p[31:91]
        nb = (len(p)-91)//11
        bl = []
        for i in range(nb):
            node, act = struct.unpack_from('<II', p, 91+11*i); ty, b1, b2 = p[99+11*i], p[100+11*i], p[101+11*i]
            cls = act >> 28
            bl.append(f"{node:08X}>{'P' if act==actor else ('A' if cls==0xA else ('B' if cls==0xB else hex(cls)))}:{ty}/{b1}/{b2}")
        e = res[r]; e['n'] += 1; e['files'].add(tag)
        e['pd'][f"k{kind} a{a} b{b} st{st:#x} tail{rest[16:].hex()}"] += 1
        e['binds'][' '.join(bl)] += 1
        e['actor'][f"{actor:08X}" if actor>>28!=0xA else 'A'] += 1
out = {}
for r, e in sorted(res.items(), key=lambda kv: -kv[1]['n']):
    out[f"{r:08X}"] = {'n': e['n'], 'files': len(e['files']), 'pd': dict(e['pd'].most_common(3)), 'binds': dict(e['binds'].most_common(3))}
json.dump(out, open(sys.argv[0].replace('dump2661.py','res2661.json'),'w'), indent=1)
print(len(out))
