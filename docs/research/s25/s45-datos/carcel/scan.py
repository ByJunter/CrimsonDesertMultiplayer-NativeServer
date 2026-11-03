# Read-only: decode S>C 2661/2981/2524/2810/2322 and a set of other opcodes from a cdmp capture (.bin) in a t window.
import sys, struct, mmap, json
REC = struct.Struct("<QBBHHHII")
def scan(path, t_lo, t_hi, ops):
    out = []
    with open(path, 'rb') as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        assert mm[:4] == b'CDPK'
        pos = 12; t0 = None; L = len(mm)
        while pos + 24 <= L:
            t, d, _, size, copied, _, sess, thr = REC.unpack_from(mm, pos); pos += 24
            if t0 is None: t0 = t
            tr = (t - t0) / 1e6
            if tr > t_hi: break
            if copied >= 2 and tr >= t_lo:
                op = mm[pos] | (mm[pos+1] << 8)
                if op in ops:
                    out.append((tr, d, op, bytes(mm[pos+5:pos+copied]), sess))
            pos += copied
        mm.close()
    return out
if __name__ == '__main__':
    path, lo, hi, outp = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    ops = {int(x) for x in sys.argv[5].split(',')}
    rows = scan(path, lo, hi, ops)
    with open(outp, 'w') as o:
        for tr, d, op, p, s in rows:
            o.write(json.dumps({'t': tr, 'd': d, 'op': op, 'p': p.hex(), 's': s}) + '\n')
    print(len(rows))
