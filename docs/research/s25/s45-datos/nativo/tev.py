import csv, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
rows = list(csv.DictReader(open(r'D:\RE\CrimsonDesert_2976\exports\rtti_classes.csv', encoding='utf-8')))
def vt(va, n): return [pe.u64(va + 8*i) - pe.BASE for i in range(n)]
def follow(a, depth=0):
    # describe a slot target: ret / thunk chain / first call
    try: ins = pe.dis(a, a + 64)
    except Exception: return f"{a:#x}?"
    if not ins: return f"{a:#x}?"
    s = []
    for x in ins[:8]:
        s.append(f"{x.mnemonic} {x.op_str}")
        if x.mnemonic in ('ret', 'jmp'): break
    return f"{a:#x}[{pe.sec(a)}]: " + ' ; '.join(s)
base = None
for r in rows:
    c = r['class']
    if not (c.endswith('_ITimelineEventData') or c.endswith('_ITimeLineEventData')) or not r['vtable_rva'] or c.startswith('"'): continue
    if int(r['nslots']) != 109: continue
    v = vt(int(r['vtable_rva'], 16), 109)
    if base is None: base = v
    print(f"== {c[4:]} vt {r['vtable_rva']}")
    for k in (97, 98, 99, 100, 101, 102, 103):
        print(f"   [{k}] {follow(v[k])}")
