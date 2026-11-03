import sys
sys.path.insert(0, r'<scratch>\crash_ue')
import walk as W
from md import *
dump, asi, mode = sys.argv[1], sys.argv[2], sys.argv[3]
W.PATHS['cdmp.asi'] = asi
d = Dump(dump)
regs = ctx(d, d.exc['ctx'])
if mode == 'exc':
    for f in W.walk(d, dict(regs), maxf=40):
        print(f[0], hex(f[1]) if isinstance(f[1], int) else f[1], hex(f[2]) if isinstance(f[2], int) else '', f[3])
elif mode == 'scan':
    sp = regs['rsp']
    t = [t for t in d.threads if t['tid'] == d.exc['tid']][0]
    top = t['stack'] + t['stacksz']
    a = sp
    while a < top and a < sp + 0x3000:
        v = d.q(a)
        mm = d.mod(v) if v else None
        if mm and mm[1] in ('CrimsonDesert.exe', 'cdmp.asi'):
            pe = W.getpe(mm[1])
            c = W.is_call_before(d, pe, mm[0], v) if pe else '?'
            print(hex(a), 'rsp+0x%X' % (a - sp), d.sym(v), 'CALL' if c else '')
        a += 8
elif mode == 'frames':
    # print registers at each frame (nonvolatile recovered)
    r = dict(regs)
    for n in range(25):
        m = d.mod(r['rip'])
        print(n, d.sym(r['rip']), ' '.join('%s=%x' % (k, r[k]) for k in ('rbx','rsi','rdi','rbp','r12','r13','r14','r15','rsp')))
        pe = W.getpe(m[1]) if m else None
        if not pe: break
        W.unwind(d, pe, m[0], r)
