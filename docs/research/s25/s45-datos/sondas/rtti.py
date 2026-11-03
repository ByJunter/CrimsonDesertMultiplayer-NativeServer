import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
def name(vt):
    col = pe.u64(vt-8) - pe.BASE
    sig, o, cd, td, cls = struct.unpack('<IIIII', pe.rd(col, 20))
    nm = pe.rd(td+0x10, 120).split(b'\0')[0].decode(errors='replace')
    return hex(col), sig, hex(td), nm
for a in sys.argv[1:]:
    v=int(a,16); print(hex(v), pe.sec(v), name(v))
