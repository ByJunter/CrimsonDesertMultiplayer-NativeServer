from pathlib import Path
import json,struct,re,bisect
import capstone
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a4-contencion');R=json.loads((OUT/'pe.json').read_text());b=Path(R['file']).read_bytes();ss=R['sections'];md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
def raw(r):s=next(s for s in ss if s['va']<=r<s['va']+max(s['size'],s['virtual']));return s['raw']+r-s['va']
nt=struct.unpack_from('<I',b,0x3c)[0];pr,ps=struct.unpack_from('<II',b,nt+24+112+24);fs=[struct.unpack_from('<III',b,o) for o in range(raw(pr),raw(pr)+ps,12)];starts=[x[0] for x in fs]
lines=[]
for p in R['points']:
 lines.append({k:v for k,v in p.items() if k not in ('signature_locations','disassembly')})
 for hit in p['branch_candidates']:
  r=int(hit['source'],16);f=fs[bisect.bisect_right(starts,r)-1];covered=f[0]<=r<f[1]
  dis=list(md.disasm(b[raw(f[0]):raw(f[0])+f[1]-f[0]],f[0])) if covered else []
  lines.append(dict(candidate=hit,source_pdata=[hex(x) for x in f] if covered else None,source_on_boundary=r in {i.address for i in dis},source_instructions=[(hex(i.address),i.mnemonic,i.op_str,i.bytes.hex()) for i in dis if r-8<=i.address<r+8],raw_context=b[raw(r)-10:raw(r)+10].hex()))
root=Path(r'%TEMP%\codex-s34-auditoria-20261014\codigo-d835806\src\mod\session2')
for fn,var,r in [('npc_action_probe.cpp','kActionBytes',0x2a78ca0),('npc_action_probe.cpp','kNodeBytes',0x281cee0),('host_scene_diag.cpp','kH1bBytes',0x2a79060),('docked_gimmicks_logic.h','kControllerWriteBytes',0x2792a7b)]:
 t=(root/fn).read_text(encoding='utf-8-sig');m=re.search(var+r'\[\d*\].*?\{([^}]+)\}',t,re.S)
 if not m:lines.append(dict(signature=var,error='array not located'));continue
 sig=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})(?![0-9a-fA-F])',m[1]));hits=[]
 for s in ss:
  if s['flags']&0x20000000:
   d=b[s['raw']:s['raw']+s['size']];i=d.find(sig)
   while i>=0:hits.append(hex(s['va']+i));i=d.find(sig,i+1)
 lines.append(dict(signature=var,bytes=sig.hex(),matches_target=b[raw(r):raw(r)+len(sig)]==sig,occurrences=hits))
for r,length in [(0x2a78ce0,90),(0x2a79040,70),(0x287d520,45),(0x287da50,5)]:
 lines.append(dict(disasm_from=hex(r),instructions=[(hex(i.address),i.mnemonic,i.op_str) for i in md.disasm(b[raw(fs[bisect.bisect_right(starts,r)-1][0]):raw(r)+length],fs[bisect.bisect_right(starts,r)-1][0]) if i.address>=r]))
(OUT/'pe-detalle.json').write_text(json.dumps(lines,indent=2),encoding='utf-8')
for item in lines:print(json.dumps(item))


