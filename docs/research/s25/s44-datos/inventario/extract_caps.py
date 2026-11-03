# Extract S>C 2661 / 2810 / 2981 / 2524 (and C>S 2173/2591/2436/2767) from cdmp captures. Read only.
import sys, struct, json, mmap, os
REC = struct.Struct("<QBBHHHII")
OPS_SC = {2661, 2810, 2981, 2524, 2322}
def scan(path, out):
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        if mm[:4] != b'CDPK':
            print('not capture', path); return 0
        ver = struct.unpack_from('<I', mm, 4)[0]
        pos = 12; n = 0; t0 = None; L = len(mm)
        while pos + 24 <= L:
            t, d, _, size, copied, _, sess, thr = REC.unpack_from(mm, pos)
            pos += 24
            if t0 is None: t0 = t
            if copied >= 5:
                op = mm[pos] | (mm[pos+1] << 8)
                if (d == 2 and op in OPS_SC) or (d == 1 and op in (2173, 2591, 2436, 2767)):
                    pl = bytes(mm[pos+5:pos+copied])
                    out.write(json.dumps({'f': path, 't': (t - t0) / 1e6, 'tus': t, 'd': d, 'op': op, 'size': size, 'copied': copied, 'p': pl.hex()}) + '\n')
                    n += 1
            pos += copied
        mm.close()
        return n
if __name__ == '__main__':
    outp = sys.argv[1]
    with open(outp, 'a') as out:
        for p in sys.argv[2:]:
            print(p, scan(p, out), flush=True)
