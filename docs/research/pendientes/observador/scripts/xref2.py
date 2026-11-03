import sys,os,re,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from xref import callers
def root(rva):
    f=D.func_of(rva)
    return f
if __name__=='__main__':
    for a in sys.argv[1:]:
        r=int(a,16); f=D.func_of(r)
        print(f"{r:08X} in pdata func {f[0]:08X}-{f[1]:08X}" if f else f"{r:08X} no pdata")
        if not f: continue
        for c in callers(f[0], ('.rsrc','.xtls','.text')):
            cf=D.func_of(c[1])
            print(f"   {'call' if c[2]==0xE8 else 'jmp '} from {c[0]} {c[1]:08X} (func {cf[0]:08X})" if cf else f"   {'call' if c[2]==0xE8 else 'jmp '} from {c[0]} {c[1]:08X}")
