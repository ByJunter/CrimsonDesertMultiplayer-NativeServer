import sys
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe
pe = cdpe.PE()
for a in sys.argv[1:]:
    r = int(a,16)
    b = pe.read(r,4)
    print(hex(r), hex(pe.u32(r)) if b else None, pe.secname(r))
