# Compares every (rva, bytes) pair declared in observer_stage.cpp with the PE.
import sys,os,re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
src=open(sys.argv[1],encoding='utf8').read()
rvas=dict(re.findall(r'constexpr uintptr_t (k\w+) = (0x[0-9A-Fa-f]+);',src))
arrs=dict(re.findall(r'constexpr uint8_t (k\w+?)Bytes\[\] = \{([^}]*)\}',src,re.S))
ok=True
for name,body in arrs.items():
    b=bytes(int(x,16) for x in re.findall(r'0x([0-9A-Fa-f]{2})',body))
    r=rvas.get(name)
    if r is None: print('no rva for',name); ok=False; continue
    rv=int(r,16); pe=D.rd(rv,len(b)); s=D.sec(rv)[0]
    good = pe==b and s=='.rsrc'
    ok&=good
    print(f"{name:18} exe+0x{rv:X} {s} {len(b):2} bytes {'OK' if good else 'MISMATCH '+pe.hex(' ')}")
print('ALL OK' if ok else 'SOME MISMATCH')
