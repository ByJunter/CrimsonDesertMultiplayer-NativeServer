# Read-only scan of cdmp captures for shop traffic. Usage: scan.py funcnpc.json out.json cap1.bin [cap2.bin...]
import sys, struct, json, mmap, collections, os
REC = struct.Struct("<QBBHHHII")
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
U64 = lambda b, o: struct.unpack_from('<Q', b, o)[0]
names = json.load(open(sys.argv[1]))
SHOP = {int(k, 16): v for k, v in names.items()}
INTER = {2671, 2663, 2720, 2916, 3000, 2743, 2136, 2789, 2366, 2458}
SCENE_REQ = {2436, 2173, 2767, 2591, 2105, 2704, 2325, 2276}
res = {}
for path in sys.argv[3:]:
    r = {'file': path, 'ops': collections.Counter(), 'shop2661': [], 'inter': [], 'shopseq_req': collections.Counter(), 'shop2810': [], 'gaps': 0}
    shopseq = {}
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        assert mm[:4] == b'CDPK'
        pos = 12; L = len(mm)
        while pos + 24 <= L:
            t, d, _, size, copied, _, sess, thr = REC.unpack_from(mm, pos)
            off = pos; pos += 24
            if sess == 0xFFFFFFFF and copied == 0:
                r['gaps'] += thr; continue
            if copied < 5:
                pos += copied; continue
            data = mm[pos:pos+copied]; pos += copied
            op = data[0] | (data[1] << 8); p = data[5:]
            if op in INTER or op == 2661 or op == 2810 or op in SCENE_REQ:
                r['ops'][f'{d}:{op}'] += 1
            if op == 2661 and d == 2 and len(p) >= 91 and (len(p) - 91) % 11 == 0:
                rh = U32(p, 27)
                if rh in SHOP:
                    seq = U64(p, 9)
                    binds = [(U32(p, i), U32(p, i+4), p[i+8]) for i in range(91, len(p), 11)]
                    shopseq[seq] = SHOP[rh]
                    r['shop2661'].append({'t': t/1e6, 'off': off, 'actor': f'{U32(p,1):08X}', 'owner': f'{U32(p,5):08X}', 'seq': f'{seq:X}', 'res': SHOP[rh],
                                          'binds': ' '.join(f'{n:08X}->{a:08X}/{ty}' for n, a, ty in binds)})
            elif op == 2810 and d == 2 and len(p) >= 12:
                seq = U64(p, 4)
                if seq in shopseq:
                    r['shop2810'].append({'t': t/1e6, 'seq': f'{seq:X}', 'actor': f'{U32(p,0):08X}', 'res': shopseq[seq], 'hex': p.hex()})
            elif op in SCENE_REQ and d == 1 and len(p) >= 8:
                seq = U64(p, 0)
                if seq in shopseq:
                    r['shopseq_req'][f'{op}:{seq:X}:{shopseq[seq]}'] += 1
            elif op in INTER:
                r['inter'].append({'t': t/1e6, 'off': off, 'dir': d, 'op': op, 'sess': sess, 'hex': p[:40].hex()})
        mm.close()
    r['ops'] = dict(r['ops']); r['shopseq_req'] = dict(r['shopseq_req'])
    res[os.path.basename(os.path.dirname(path)) + '/' + os.path.basename(path)] = r
    own = collections.Counter((e['owner'], e['res']) for e in r['shop2661'])
    print('==', path, 'gaps', r['gaps']); print(' ops', r['ops']); print(' shop2661 by owner/res', dict(own))
    print(' shop scene Req', r['shopseq_req']); print(' inter n', len(r['inter']), collections.Counter((e['dir'], e['op']) for e in r['inter']))
json.dump(res, open(sys.argv[2], 'w'), indent=1)
