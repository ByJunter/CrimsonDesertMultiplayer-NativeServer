import csv, sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
rows = {r['class']: r for r in csv.DictReader(open(r'D:\RE\CrimsonDesert_2976\exports\rtti_classes.csv', encoding='utf-8'))}
def slots(cls): return [int(x, 16) for x in rows[cls]['slots'].split(';') if x]
def vt(va):  # read vtable directly
    return [pe.u64(va + 8*i) - pe.BASE for i in range(105)]
C = vt(0x558C080); K = vt(0x590DF78); S = vt(0x5B1FE10)
def desc(a):
    try:
        ins = pe.dis(a, a + 24)
    except Exception: return '?'
    if not ins: return '?'
    i0 = ins[0]
    b = pe.rd(a, 1)[0]
    if a == 0x359B50: return 'ret'
    if i0.mnemonic == 'ret': return 'ret'
    if i0.mnemonic == 'jmp': return 'thunk->' + i0.op_str
    s = ' ; '.join(f"{x.mnemonic} {x.op_str}" for x in ins[:2])
    return s[:40]
out = []
for cls, r in rows.items():
    if not cls.startswith('pa::StageChart_Function_') or not r['vtable_rva']: continue
    sl = slots(cls)
    if len(sl) < 9: continue
    ex = sl[8]
    ins = pe.dis(ex, ex + 32)
    off = None
    for x in ins:
        m = re.match(r'qword ptr \[rax \+ (0x[0-9a-f]+)\]', x.op_str)
        if x.mnemonic == 'jmp' and m: off = int(m.group(1), 16); break
        if x.mnemonic in ('ret', 'call'): break
    if off is None:
        out.append((999, cls[len('pa::StageChart_Function_'):], f"exec {ex:#x} (no trampoline: {' ; '.join(x.mnemonic+' '+x.op_str for x in ins[:4])})", '', '', ''))
        continue
    k = off // 8
    out.append((off, cls[len('pa::StageChart_Function_'):], f"exec {ex:#x}", f"C {C[k]:#x} [{pe.sec(C[k])}] {desc(C[k])}", f"common {K[k]:#x} {desc(K[k])}", f"S {S[k]:#x} {desc(S[k])}"))
for o in sorted(out):
    print(f"+{o[0]:#05x} | {o[1]:<38} | {o[2]} | {o[3]} | {o[4]} | {o[5]}")
