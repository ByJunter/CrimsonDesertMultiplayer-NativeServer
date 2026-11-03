import sys,os,re; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
ops=[int(x) for x in sys.argv[1].split(',')]
for name,va,vs,ro,rs in D.secs:
    if name not in ('.rsrc',): continue
    data=D.mm[ro:ro+rs]
    for op in ops:
        lo=op&0xff; hi=op>>8
        pats=[bytes([0x66,0x41,0xC7,0x06,lo,hi]), bytes([0x66,0xC7,0x06,lo,hi]), bytes([0x66,0x41,0xC7,0x07,lo,hi]),bytes([0x66,0xC7,0x07,lo,hi]),bytes([0x66,0x41,0xC7,0x04,0x24,lo,hi]),bytes([0x66,0xC7,0x00,lo,hi]),bytes([0x66,0x41,0xC7,0x00,lo,hi]),bytes([0x66,0xC7,0x03,lo,hi]),bytes([0x66,0x41,0xC7,0x45,0x00,lo,hi])]
        for imm in (bytes([lo,hi,0,0]),):
            for m in re.finditer(re.escape(imm),data):
                o=m.start(); b=data[o-2:o]
                # mov r32, imm32 : B8+r or 41 B8+r
                if (0xB8<=data[o-1]<=0xBF):
                    print(f"{op} movimm  {va+o-1 - (1 if data[o-2]==0x41 else 0):08X}")
        for p in pats:
            for m in re.finditer(re.escape(p),data):
                print(f"{op} movw    {va+m.start():08X} {p.hex(' ')}")
