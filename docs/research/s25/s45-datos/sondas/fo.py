import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
for a in sys.argv[1:]:
    a=int(a,16); f=pe.func_of(a); print(hex(a), 'func', f and (hex(f[0]),hex(f[1])), pe.sec(a))
