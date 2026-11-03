import sys, os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
for s in sys.argv[1:]:
    a = int(s, 16); f = pe.func_of(a)
    ins = pe.dis(f[0], f[1]); idx = [i for i, x in enumerate(ins) if x.address - pe.BASE == a][0]
    print(f"== {a:#x} in {f[0]:#x}-{f[1]:#x}")
    for x in ins[max(0, idx-10):idx+8]: print(f"  {x.address-pe.BASE:#x} {x.mnemonic} {x.op_str}")
