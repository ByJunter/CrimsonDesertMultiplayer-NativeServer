"""helpers: vt <rva> (vtables containing ptr), dis <rva> [n] (from rva, linear), fn <rva> (whole pdata func),
slot <vt> <off>, callers <rva>"""
import sys, struct, re, bisect
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe
pe = cdpe.PE()
BASE = cdpe.BASE
def vt_containing(t):
    r = cdpe.RTTI(pe)
    q = struct.pack("<Q", BASE + t)
    out = []
    for m in re.finditer(re.escape(q), pe.data):
        rva = pe.rva_of_off(m.start())
        if rva is None: continue
        # walk back to vtable start
        for k in range(0, 400):
            s = rva - 8*k
            if s in r.vt:
                out.append((s, rva - s, r.vt_name(s))); break
        else:
            out.append((None, None, hex(rva)))
    return out
def dis(rva, n=80):
    for ins in pe.disasm(rva, n, maxbytes=n*16, stop_ret=False):
        print(pe.fmt(ins))
def fn(rva):
    fs = cdpe.func_of(pe, rva)
    pd = cdpe._pd
    end = None
    for b, e, u in pd:
        if b == fs: end = e; break
    if fs is None: print("no pdata"); fs, end = rva, rva+0x300
    print(f"; func {fs:#x}-{end:#x} {pe.secname(fs)}")
    for ins in pe.md.disasm(pe.read(fs, end-fs), BASE+fs):
        print(pe.fmt(ins))
cmd = sys.argv[1]
if cmd == "vt":
    for a in sys.argv[2:]:
        t = int(a, 16)
        for v in vt_containing(t): print(hex(t), [hex(x) if isinstance(x,int) else x for x in v])
elif cmd == "dis": dis(int(sys.argv[2],16), int(sys.argv[3]) if len(sys.argv)>3 else 80)
elif cmd == "fn": fn(int(sys.argv[2],16))
elif cmd == "slot":
    vt = int(sys.argv[2],16)
    for o in sys.argv[3:]:
        off = int(o,16); print(hex(vt), hex(off), hex(pe.u64(vt+off)-BASE))
elif cmd == "callers":
    import xref
    for a in sys.argv[2:]:
        t = int(a,16)
        for s,k in xref.refs(t):
            f = cdpe.func_of(pe, s)
            print(f"{t:#x} <- {s:#x} {k} in {f:#x}" if f else f"{t:#x} <- {s:#x} {k} nopdata {pe.secname(s)}")
