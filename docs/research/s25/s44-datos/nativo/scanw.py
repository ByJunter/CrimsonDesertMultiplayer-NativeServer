import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe, re
lo, hi = int(sys.argv[1],16), int(sys.argv[2],16); pat = re.compile(sys.argv[3])
for b, e in pe.FUNCS:
    if b < lo or b >= hi: continue
    try:
        for i in pe.dis(b, e):
            s = f"{i.mnemonic} {i.op_str}"
            if pat.search(s): print(f"{b:#x} {i.address-pe.BASE:#x}: {s}")
    except Exception as ex: pass
