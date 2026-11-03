import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
def vt(va): return [pe.u64(va + 8*i) - pe.BASE for i in range(105)]
C = vt(0x558C080); K = vt(0x590DF78); S = vt(0x5B1FE10)
names = {}
for line in open('scf.txt'):
    m = re.match(r'\+(0x[0-9a-f]+) \| (\S+)', line)
    if m and m.group(1) != '0x3e7': names[int(m.group(1), 16)//8] = m.group(2)
RES = {0x205DC30: 'resolve205DC30', 0xE0314C0: 'resolveXtlsE0314C0', 0x205D7C0: 'owner205D7C0', 0x8B44E0: 'GetLocalPlayer', 0x205E520: 'byNodeData205E520', 0x15A77B0: 'stage15A77B0'}
GLOB = {0x6D691E0: 'g6D691E0(client)', 0x6D69190: 'gWorldSlot', 0x6D69518: 'gCliMgr', 0x6D69520: 'gSrvMgr'}
def scan(a, depth=0, seen=None):
    seen = seen or set()
    if a in seen or depth > 2: return set()
    seen.add(a)
    out = set()
    f = pe.func_of(a)
    if pe.sec(a) != '.rsrc':
        return {f'xtls'}
    if not f:
        ins = pe.dis(a, a+16)
        if ins and ins[0].mnemonic == 'jmp' and ins[0].op_str.startswith('0x'):
            t = int(ins[0].op_str, 16) - pe.BASE
            return {'thunk->xtls' if pe.sec(t) != '.rsrc' else ''} | scan(t, depth, seen)
        return {'?'}
    for x in pe.dis(f[0], f[1]):
        if x.mnemonic in ('call', 'jmp') and x.op_str.startswith('0x'):
            t = int(x.op_str, 16) - pe.BASE
            if t in RES: out.add(RES[t])
            elif pe.sec(t) != '.rsrc': out.add('calls-xtls')
            elif depth < 1 and x.mnemonic == 'call': out |= {s + '(d)' for s in scan(t, depth+1, seen) if not s.endswith('(d)') and s not in ('calls-xtls','xtls')}
        m = re.search(r'\[rip \+ (0x[0-9a-f]+)\]', x.op_str)
        if m:
            t = x.address - pe.BASE + x.size + int(m.group(1), 16)
            if t in GLOB: out.add(GLOB[t])
    return out
for k in range(105):
    if C[k] == 0x359B50: continue
    tag = 'COMMON' if C[k] == K[k] else ('C+S' if S[k] != 0x359B50 else 'CLIENT-ONLY')
    nm = names.get(k) or names.get(k-1, '?') + '(end?)'
    if k < 0x20: nm = f'slot{k}'
    print(f"+{k*8:#05x} {nm:<36} {tag:<11} {C[k]:#x} {sorted(scan(C[k]))}")
