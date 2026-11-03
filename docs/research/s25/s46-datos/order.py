# For each shop 2661 in a capture, find the first 2005 announcing each bound B-key (before or after). Read only.
import sys, struct, json, mmap
REC = struct.Struct("<QBBHHHII")
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
U64 = lambda b, o: struct.unpack_from('<Q', b, o)[0]
SHOP = {int(k, 16): v for k, v in json.load(open(sys.argv[1])).items()}
for path in sys.argv[2:]:
    first2005 = {}; scenes = []
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ); pos = 12; L = len(mm)
        while pos + 24 <= L:
            t, d, _, size, copied, _, sess, thr = REC.unpack_from(mm, pos); off = pos; pos += 24
            if copied < 5: pos += copied; continue
            data = mm[pos:pos+copied]; pos += copied
            op = data[0] | (data[1] << 8); p = data[5:]
            if d != 2: continue
            if op == 2005:
                j = 0
                while j + 2 <= len(p):
                    n = struct.unpack_from('<H', p, j)[0]; j += 2
                    if not n or j + n > len(p): break
                    r = p[j:j+n]; j += n
                    if n >= 5:
                        k = U32(r, 1)
                        if k >> 28 == 0xB: first2005.setdefault(k, []).append(t)
            elif op == 2661 and len(p) >= 91 and (len(p) - 91) % 11 == 0 and U32(p, 27) in SHOP:
                binds = [U32(p, i+4) for i in range(91, len(p), 11) if U32(p, i+4) >> 28 == 0xB]
                scenes.append((t, U32(p, 5), U64(p, 9), SHOP[U32(p, 27)], binds))
        mm.close()
    print('==', path)
    for t, owner, seq, name, binds in scenes:
        rel = []
        for k in binds:
            ts = first2005.get(k, [])
            before = [x for x in ts if x <= t]; after = [x for x in ts if x > t]
            rel.append(f'{k:08X}: 2005 ' + (f'{(t-before[-1])/1000:.1f} ms BEFORE' if before else (f'{(after[0]-t)/1000:.1f} ms AFTER' if after else 'NEVER')))
        print(f'  {t/1e6:.3f} owner {owner:08X} seq {seq:X} {name[15:]}: ' + '; '.join(rel))
