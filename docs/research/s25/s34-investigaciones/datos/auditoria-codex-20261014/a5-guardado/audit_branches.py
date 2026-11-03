from pathlib import Path
import struct,json
exec(compile(open(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado\audit_pe.py',encoding='utf-8-sig').read(),'audit_pe.py','exec'))
heads=[0x186e450,0x186f570,0x1871930,0x20688a0,0x235a800,0x2358ef0];inside={h+i for h in heads for i in range(1,5)};hits=[]
for s in sections:
 if not s['flags']&0x20000000:continue
 start=s['offset'];end=start+s['rawsize']
 for op in [b'\xe8',b'\xe9']+[b'\x0f'+bytes([v]) for v in range(0x80,0x90)]:
  pos=start
  while True:
   pos=b.find(op,pos,end-4)
   if pos<0:break
   site=s['rva']+pos-start;length=len(op)+4;target=site+length+struct.unpack_from('<i',b,pos+len(op))[0]
   if target in inside:hits.append({'rva':hex(site),'target':hex(target),'bytes':b[pos:pos+length].hex()})
   pos+=1
short=[]
for h in heads:
 for p in pdata:
  if p[0]<=h+132 and p[1]>=h-132:
   for i in cs.disasm(b[off(p[0]):off(p[0])+min(p[1]-p[0],1000000)],p[0]):
    if i.mnemonic.startswith('j') or i.mnemonic=='call':
     try:t=int(i.op_str,16)
     except ValueError:continue
     if t in inside:short.append({'rva':hex(i.address),'target':hex(t),'mnemonic':i.mnemonic})
out={'rel32_raw_hits_inside_first_5_bytes':hits,'decoded_nearby_branch_hits_inside_first_5_bytes':short,'scope':'Direct branches only; no proof about indirect destinations or patched live code.'}
(OUT/'branch-check.json').write_text(json.dumps(out,indent=2),encoding='utf-8');print(out)
