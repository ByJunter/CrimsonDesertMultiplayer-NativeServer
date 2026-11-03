from pathlib import Path
import struct,json,bisect
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
ROOT=Path(r'%TEMP%\codex-s34-auditoria-20261014\revision-consolidacion')
b=Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe').read_bytes()
u32=lambda o:struct.unpack_from('<I',b,o)[0]
p=u32(0x3c); st=p+24+struct.unpack_from('<H',b,p+20)[0]; ss=[]
for i in range(struct.unpack_from('<H',b,p+6)[0]):
 at=st+40*i; name=b[at:at+8].split(b'\0')[0].decode(); vs,va,n,raw=struct.unpack_from('<IIII',b,at+8); ss.append((name,va,vs,n,raw))
def off(r): return next(raw+r-va for name,va,vs,n,raw in ss if va<=r<va+n)
r,n=struct.unpack_from('<II',b,p+24+112+3*8); pdata=[struct.unpack_from('<III',b,off(r)+12*j) for j in range(n//12)]; starts=[x[0] for x in pdata]
def bounds(r):
 i=bisect.bisect_right(starts,r)-1
 return pdata[i] if i>=0 and r<pdata[i][1] else None
sig=bytes.fromhex('e9 ab 17 07 0c'); target=0x2068340
hits=[]; at=0
while (at:=b.find(sig,at))>=0:
 sec=next(((name,va+at-raw) for name,va,vs,n,raw in ss if raw<=at<raw+n),None)
 hits.append({'file_offset':hex(at),'section':sec[0] if sec else None,'rva':hex(sec[1]) if sec else None});at+=1
candidates=[]
for name,va,vs,n,raw in ss:
 if name!='.rsrc': continue
 data=b[raw:raw+n]
 for opcode in [b'\xe8',b'\xe9']+[bytes([0x0f,x])for x in range(0x80,0x90)]:
  at=0; size=len(opcode)+4
  while (at:=data.find(opcode,at))>=0:
   if at+size<=len(data):
    dest=va+at+size+struct.unpack_from('<i',data,at+len(opcode))[0]
    if target<=dest<target+5: candidates.append((va+at,dest,size,opcode.hex()))
   at+=1
 for rv in range(target-132,target+133):
  at=rv-va
  if not (0<=at<len(data)-1): continue
  op=data[at]
  if op in (0xeb,0xe0,0xe1,0xe2,0xe3) or 0x70<=op<=0x7f:
   dest=rv+2+struct.unpack_from('<b',data,at+1)[0]
   if target<=dest<target+5: candidates.append((rv,dest,2,hex(op)))
md=Cs(CS_ARCH_X86,CS_MODE_64); out=[]
for rv,dest,size,op in candidates:
 bound=bounds(rv); validated=False; ins_text=None
 if bound:
  a,z,u=bound
  for ins in md.disasm(b[off(a):off(z-1)+1],a):
   if ins.address==rv:
    validated=ins.size==size and (ins.mnemonic=='call' or ins.mnemonic.startswith('j') or ins.mnemonic.startswith('loop'));ins_text=ins.mnemonic+' '+ins.op_str;break
   if ins.address>rv: break
 out.append({'source':hex(rv),'target':hex(dest),'encoding':op,'pdata':([hex(x) for x in bound] if bound else None),'validated_instruction':validated,'instruction':ins_text})
i=bisect.bisect_right(starts,target)
result={'signature':sig.hex(' '),'file_signature_hits':hits,'thunk_pdata':bounds(target),'neighbors':[[hex(x)for x in a]for a in pdata[max(0,i-1):i+1]],'direct_candidates':out,'interior_candidates':[a for a in out if int(a['target'],16)>target],'scope':'All raw .rsrc E8/E9 rel32 and 0F80..8F rel32; all unprefixed short Jcc/JMP/LOOP/JRCXZ able to reach the block. Candidate sources checked from pdata starts with Capstone. No indirect-target proof; no .xtls enumeration.'}
(ROOT/'thunk-logout.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({'signature_hits':len(hits),'direct_candidates':len(out),'validated':sum(x['validated_instruction'] for x in out),'interior_candidates':len(result['interior_candidates'])},indent=2))

