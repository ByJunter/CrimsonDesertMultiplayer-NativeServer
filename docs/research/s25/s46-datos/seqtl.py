# Timeline of one sequencer id's packets + interaction packets in a window. Read only. seqtl.py cap.bin seqhex t0 t1
import sys, struct, mmap
REC = struct.Struct("<QBBHHHII")
U32 = lambda b, o: struct.unpack_from('<I', b, o)[0]
U64 = lambda b, o: struct.unpack_from('<Q', b, o)[0]
path, seq, t0, t1 = sys.argv[1], int(sys.argv[2], 16), float(sys.argv[3]), float(sys.argv[4])
INTER = {2671, 2663, 2720, 2916, 3000, 2743, 2136, 2789, 2366, 2458, 2404}
with open(path, 'rb') as f:
    mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ); pos = 12; L = len(mm); last = None
    while pos + 24 <= L:
        t, d, _, size, copied, _, sess, thr = REC.unpack_from(mm, pos); pos += 24
        data = mm[pos:pos+copied]; pos += copied
        ts = t / 1e6
        if ts < t0 or copied < 5: continue
        if ts > t1: break
        op = data[0] | (data[1] << 8); p = data[5:]
        hit = None
        if op in INTER and op != 2404: hit = p[:28].hex()
        elif op in (2436, 2173, 2767, 2591, 2105, 2704, 2325) and d == 1 and len(p) >= 8 and U64(p, 0) == seq: hit = p[:24].hex()
        elif op in (2981, 2661) and len(p) >= 13 and U64(p, 5 if op == 2981 else 9) == seq: hit = p[:32].hex()
        elif op == 2524 and len(p) >= 12 and seq.to_bytes(8, 'little') in bytes(p[:24]): hit = p[:32].hex()
        elif op == 2810 and len(p) >= 12 and U64(p, 4) == seq: hit = p.hex()
        if hit: print(f'{ts:.3f} {"C>S" if d == 1 else "S>C"} {op} {size} {hit}')
