import sys,os,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
vt=int(sys.argv[1],16); n=int(sys.argv[2])
for i in range(n):
    p=struct.unpack('<Q',D.rd(vt+8*i,8))[0]-0x140000000
    s=D.sec(p)
    print(f"slot {i:3d} +0x{8*i:03X}: {p:08X} {s[0] if s else '?'}")
