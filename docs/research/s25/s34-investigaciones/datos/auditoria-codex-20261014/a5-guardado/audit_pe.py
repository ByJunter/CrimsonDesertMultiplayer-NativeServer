from pathlib import Path
import struct,mmap,re,json,hashlib
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado')
SRC=Path(r'%TEMP%\codex-s34-auditoria-20261014\codigo-d835806\src\mod\session2\guest_save_host.cpp').read_text()
EXE=Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe')
f=EXE.open('rb'); b=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
u16=lambda o:struct.unpack_from('<H',b,o)[0]
u32=lambda o:struct.unpack_from('<I',b,o)[0]
pe=u32(0x3c); opt=pe+24; n=u16(pe+6); sz=u16(pe+20)
sections=[]
for j in range(n):
 o=opt+sz+40*j; name=b[o:o+8].rstrip(b'\0').decode(); vsize,va,rawsz,ptr=struct.unpack_from('<4I',b,o+8); flags=u32(o+36)
 sections.append(dict(name=name,rva=va,vsize=vsize,rawsize=rawsz,offset=ptr,flags=flags))
def sec(r): return next(s for s in sections if s['rva']<=r<s['rva']+max(s['vsize'],s['rawsize']))
def off(r):
 s=sec(r); return s['offset']+r-s['rva']
def rva(o):
 s=next(s for s in sections if s['offset']<=o<s['offset']+s['rawsize']); return s['rva']+o-s['offset']
pdr,pds=struct.unpack_from('<II',b,opt+112+3*8)
pdata=[struct.unpack_from('<III',b,off(pdr)+i) for i in range(0,pds,12)]
cs=Cs(CS_ARCH_X86,CS_MODE_64)
results={'exe':str(EXE),'timestamp':hex(u32(pe+8)),'imagebase':hex(struct.unpack_from('<Q',b,opt+24)[0]),'sections':sections,'functions':[]}
for target in [0x186e450,0x186f570,0x1871930,0x20688a0,0x235a800,0x2358ef0]:
 x={'rva':hex(target),'offset':hex(off(target)),'section':sec(target)['name'],'pdata':[list(map(hex,p)) for p in pdata if p[0]<=target<p[1]],'head64':b[off(target):off(target)+64].hex(' '),'instructions':[f'{i.address:#x} {i.bytes.hex()} {i.mnemonic} {i.op_str}' for i in cs.disasm(b[off(target):off(target)+64],target)]}
 results['functions'].append(x)
results['signatures']=[]
for name,target in [('kLoadSlotHead',0x186f570),('kSaveHead',0x186e450),('kFlushHead',0x1871930)]:
 s=re.search(name+r'\[\] = \{(.*?)\};',SRC,re.S)[1]; pat=bytes(int(v,16) for v in re.findall(r'0x([0-9A-Fa-f]{2})',s)); hits=[];pos=0
 while True:
  pos=b.find(pat,pos)
  if pos<0:break
  hits.append({'offset':hex(pos),'rva':hex(rva(pos))});pos+=1
 results['signatures'].append({'name':name,'length':len(pat),'matches_entry':b[off(target):off(target)+len(pat)]==pat,'hits':hits})
results['direct_call_sites']=[]
for target in [0x186e450,0x186f570,0x1871930]:
 hits=[]
 for s in sections:
  if not s['flags']&0x20000000:continue
  start=s['offset'];end=start+s['rawsize'];pos=start
  while True:
   pos=b.find(b'\xe8',pos,end-4)
   if pos<0:break
   site=s['rva']+pos-start;disp=struct.unpack_from('<i',b,pos+1)[0]
   if site+5+disp==target:hits.append({'rva':hex(site),'section':s['name'],'bytes':b[pos:pos+5].hex(' ')})
   pos+=1
 results['direct_call_sites'].append({'target':hex(target),'hits':hits})
(OUT/'pe-check.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
for x in results['functions']:print(x['rva'],x['offset'],x['section'],x['pdata'],x['instructions'][:2])
print(results['signatures']);print(results['direct_call_sites'])
