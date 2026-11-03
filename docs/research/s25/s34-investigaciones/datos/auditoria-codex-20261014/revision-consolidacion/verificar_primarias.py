from pathlib import Path
import struct, json
ROOT=Path(r'%TEMP%\codex-s34-auditoria-20261014\revision-consolidacion')
def u32(b,o): return struct.unpack_from('<I',b,o)[0]
def u64(b,o): return struct.unpack_from('<Q',b,o)[0]
dump=Path(r'C:\Emulador CD\backups\p8-sesion34\shadow-p1b-p2b\cdmp_crash_20261014-180710_6364_1.dmp')
b=dump.read_bytes(); assert b[:4]==b'MDMP'
ranges=[]
for i in range(u32(b,8)):
    kind,size,off=struct.unpack_from('<III',b,u32(b,12)+12*i)
    if kind==5:
        for j in range(u32(b,off)):
            va,n,at=struct.unpack_from('<QII',b,off+4+16*j); ranges.append((va,n,at))
    if kind==9:
        n,at=struct.unpack_from('<QQ',b,off)
        for j in range(n):
            va,count=struct.unpack_from('<QQ',b,off+16+16*j); ranges.append((va,count,at)); at+=count
    if kind==3:
        for j in range(u32(b,off)):
            pos=off+4+48*j; va,count,at=struct.unpack_from('<QII',b,pos+24); ranges.append((va,count,at))
def read(va,n=8):
    for start,count,at in ranges:
        if start<=va and va+n<=start+count:
            off=at+va-start
            return {'va':hex(va),'offset':hex(off),'value':hex(int.from_bytes(b[off:off+n],'little'))}
    return {'va':hex(va),'missing':True}
def chain(va,offset):
    v=read(va)
    return None if 'value' not in v else read(int(v['value'],16)+offset)
actor=0x3f3da87a680
out={'dump':str(dump),'B0100578':{'key':read(actor+0x60,4),'components':read(actor+0x68),'cc_slot':chain(actor+0x68,0x40),'hierarchy_slot':chain(actor+0x68,0x1b8)}}
exe=Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe')
b=exe.read_bytes(); pe=u32(b,0x3c); sec=pe+24+struct.unpack_from('<H',b,pe+20)[0]; sections=[]
for i in range(struct.unpack_from('<H',b,pe+6)[0]):
    at=sec+40*i; name=b[at:at+8].split(b'\0')[0].decode(); virtual_size,va,raw_size,raw=struct.unpack_from('<IIII',b,at+8); sections.append((name,va,virtual_size,raw_size,raw))
def pebytes(va,n):
    for name,start,virtual,rawsize,raw in sections:
        if start<=va and va+n<=start+rawsize:
            at=raw+va-start
            return {'rva':hex(va),'section':name,'offset':hex(at),'bytes':b[at:at+n].hex(' ')}
    return None
out['pe']={'path':str(exe),'timestamp':hex(u32(b,pe+8)),'base':hex(u64(b,pe+24+24)),'mark':pebytes(0x28995e9,9),'logout':pebytes(0x2068340,5)}
(ROOT/'verificaciones-primarias.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
print(json.dumps(out,indent=2))
