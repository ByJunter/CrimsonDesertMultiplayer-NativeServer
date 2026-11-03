from pathlib import Path
import struct,re,json,bisect,hashlib
import capstone
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a4-contencion')
p=Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe');b=p.read_bytes();u16=lambda o:struct.unpack_from('<H',b,o)[0];u32=lambda o:struct.unpack_from('<I',b,o)[0];nt=u32(0x3c);ns=u16(nt+6);op=nt+24;base=struct.unpack_from('<Q',b,op+24)[0];sh=op+u16(nt+20);secs=[]
for i in range(ns):
 o=sh+40*i;name=b[o:o+8].rstrip(b'\0').decode();vs,va,sz,ptr=struct.unpack_from('<IIII',b,o+8);flags=u32(o+36);secs.append(dict(name=name,va=va,virtual=vs,size=sz,raw=ptr,flags=flags))
def section(r):return next(s for s in secs if s['va']<=r<s['va']+max(s['size'],s['virtual']))
def raw(r):s=section(r);return s['raw']+r-s['va']
prva,psz=struct.unpack_from('<II',b,op+112+3*8);funcs=[struct.unpack_from('<III',b,o) for o in range(raw(prva),raw(prva)+psz,12)];starts=[f[0] for f in funcs]
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
points=[(0x2a78ca0,5),(0x2a79060,7),(0x281cee0,5),(0x2792a7b,7),(0x287ef60,5),(0x287dbb0,5),(0x287d4c0,5),(0x289604e,0),(0x287da50,5)]
R=dict(file=str(p),sha256=hashlib.sha256(b).hexdigest(),timestamp=hex(u32(nt+8)),base=hex(base),sections=secs,points=[])
for r,patch in points:
 f=funcs[bisect.bisect_right(starts,r)-1];covered=f[0]<=r<f[1]
 dis=list(md.disasm(b[raw(f[0]):raw(f[0])+f[1]-f[0]],f[0])) if covered else []
 boundary=r in {i.address for i in dis};local=[i for i in dis if r-16<=i.address<r+max(patch,16)]
 sig=b[raw(r):raw(r)+16];count=0;where=[]
 for s in secs:
  if s['flags']&0x20000000:
   data=b[s['raw']:s['raw']+s['size']];z=data.find(sig)
   while z>=0:count+=1;where.append(hex(s['va']+z));z=data.find(sig,z+1)
 # Direct relative branches anywhere in executable raw bytes: conservative superset, exact destinations strictly inside.
 hits=[]
 if patch:
  for s in secs:
   if not s['flags']&0x20000000:continue
   data=b[s['raw']:s['raw']+s['size']]
   for m in re.finditer(b'[\xe8\xe9\xeb\x70-\x7f]|\x0f[\x80-\x8f]',data):
    i=m.start();c=data[i];length=6 if c==15 else 5 if c in (232,233) else 2
    if i+length>len(data):continue
    disp=int.from_bytes(data[i+length-(4 if length>=5 else 1):i+length],'little',signed=True);dest=s['va']+i+length+disp
    if r<dest<r+patch:hits.append(dict(source=hex(s['va']+i),destination=hex(dest)))
 item=dict(rva=hex(r),patch=patch,section=section(r)['name'],pdata=[hex(x) for x in f] if covered else None,entry=covered and f[0]==r,boundary=boundary,bytes=b[raw(r):raw(r)+patch].hex(),signature16=sig.hex(),signature16_count=count,signature_locations=where,branch_candidates=hits,disassembly=[dict(rva=hex(i.address),bytes=i.bytes.hex(),text=i.mnemonic+' '+i.op_str) for i in local])
 R['points'].append(item);print(json.dumps(item,ensure_ascii=False))
(OUT/'pe.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
