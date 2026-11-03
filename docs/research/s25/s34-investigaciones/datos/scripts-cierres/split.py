import sys, struct, importlib.util
spec = importlib.util.spec_from_file_location('ac', r'C:\Emulador CD-npc-s33-fix\tools\scripts\analyze_capture.py')
ac = importlib.util.module_from_spec(spec); spec.loader.exec_module(ac)
path, u2 = sys.argv[1], int(sys.argv[2], 16)
pk = ac.read_capture(path)
t0 = pk[0][0]
first = {}   # key -> (t, type, ctl, parent)
rows = []
for t, d, op, slot, plen, size, pl, th in pk:
    if d != 2 or op != 2005: continue
    tt = (t - t0) / 1e6
    for r in ac.actor_records(pl):
        if len(r) < 0x16: continue
        k = struct.unpack_from('<I', r, 1)[0]
        if k >> 28 != 0xB: continue
        ctl = struct.unpack_from('<I', r, 0x12)[0]
        fam = ac.record_family(r)
        par = fam[0] if fam else None
        if k not in first:
            first[k] = (tt, r[0], ctl, par)
split = []
for k, (tt, typ, ctl, par) in first.items():
    if par and par in first and ctl != u2 and first[par][2] == u2:
        split.append((tt, k, typ, ctl, par, tt - first[par][0]))
split.sort()
print(f'{path}: keys {len(first)}; host-controlled (ctl!=user2, ctl!=0) {sum(1 for v in first.values() if v[2] not in (u2,0))}; user2-controlled {sum(1 for v in first.values() if v[2]==u2)}')
print(f'split-family members (child ctl != user 2, its parent ctl == user 2): {len(split)}')
for tt, k, typ, ctl, par, dt in split:
    print(f'  t={tt:9.3f} child {k:08X} type {typ} ctl {ctl:08X} parent {par:08X} (+{dt:.3f} s after the parent\'s first 2005)')
