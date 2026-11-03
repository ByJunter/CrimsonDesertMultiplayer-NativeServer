import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
# for each call site of a target, show the next N instructions mentioning vf178 / GetLocalPlayer / 0x4fe260 / cmp
for line in open('callers1.txt'):
    p = line.split()
    if p[0] != sys.argv[1]: continue
    site = int(p[3], 16); f = pe.func_of(site)
    ins = pe.dis(f[0], f[1])
    idx = [i for i, x in enumerate(ins) if x.address - pe.BASE == site][0]
    hits = []
    for x in ins[idx+1: idx+60]:
        s = f"{x.mnemonic} {x.op_str}"
        if '0x178]' in s or '0x8b44e0' in s or '0x4fe260' in s or '0xa0]' in s or ('0x58]' in s and 'rip' not in s and x.mnemonic=='cmp'):
            hits.append(f"{x.address-pe.BASE:#x} {s}")
    print(f"{site:#x} in {f[0]:#x}: {hits[:5]}")
