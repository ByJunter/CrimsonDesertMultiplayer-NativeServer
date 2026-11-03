from pathlib import Path
import importlib.util,sys,struct,json,re
p=Path(__file__).with_name('audit_dumps.py');s=importlib.util.spec_from_file_location('dumpaudit',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
from capstone import Cs,CS_ARCH_X86,CS_MODE_64
pe=m.PE(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe');cs=Cs(CS_ARCH_X86,CS_MODE_64);cs.detail=True
targets=[0x88F832,0x1831598,0x232D5FF,0x1F63FE6,0x17F328F,0x2792A7B,0x1831594,0x8646D0,0x8B7D78,0x439B80,0x281CEE0,0x281DBD0,0x2820D80]
out={'timestamp':hex(pe.timestamp),'targets':{}}
for t in targets:
 f=pe.func(t);inst=list(cs.disasm(pe.read(f[0],f[1]-f[0]),f[0]));near=[{'rva':hex(i.address),'bytes':i.bytes.hex(' '),'asm':i.mnemonic+' '+i.op_str}for i in inst if t-20<=i.address<t+28]
 row={'section':pe.section(t)[0],'pdata':list(map(hex,f)),'is_entry':f[0]==t,'is_instruction':any(i.address==t for i in inst),'near':near}
 if t in(0x2792A7B,0x1831594):
  patch=7 if t==0x2792A7B else 11
  # Firma exacta de 24 bytes; enumeración completa del fichero, no sólo primer resultado.
  sig=pe.read(t,24);found=[];at=0
  while True:
   at=pe.b.find(sig,at)
   if at<0:break
   found.append(hex(at));at+=1
  interior=[]
  for i in inst:
   if (i.mnemonic.startswith('j')or i.mnemonic=='call') and len(i.operands)==1 and i.operands[0].type==2 and t<i.operands[0].imm<t+patch:interior.append(hex(i.address))
  row.update(signature=sig.hex(' '),file_matches=found,patch_inspected=patch,intrafunction_direct_targets_inside=interior)
 out['targets'][hex(t)]=row
# Búsqueda independiente de E8 rel32 hacia 1831560; validar que cae en instrucción de una función pdata.
calls=[];sec=pe.section(0x1831560);blob=pe.read(sec[1],sec[4]);at=0
while True:
 at=blob.find(b'\xe8',at)
 if at<0:break
 r=sec[1]+at
 if at+5<=len(blob)and r+5+struct.unpack_from('<i',blob,at+1)[0]==0x1831560:
  f=pe.func(r);valid=False
  if f:valid=any(i.address==r and i.mnemonic=='call'for i in cs.disasm(pe.read(f[0],f[1]-f[0]),f[0]))
  calls.append({'site':hex(r),'function':hex(f[0])if f else None,'verified_instruction':valid})
 at+=1
out['calls_1831560']=calls
out['heuristic_layer_reads']={}
for sec in pe.sections:
 if sec[0] in('.rsrc','.xtls'):
  blob=pe.read(sec[1],sec[4]);xs=[hex(sec[1]+x.start())for x in re.finditer(rb'[\x48-\x4f]\x8b[\x84\x8c\x94\x9c\xa4\xac\xb4\xbc][\xc0-\xff]\x88\x00\x00\x00',blob)]
  out['heuristic_layer_reads'][sec[0]]={'count':len(xs),'rvas':xs,'warning':'Coincidencias de bytes: no prueba que cada base sea CharacterControl ni dominancia global.'}
Path(__file__).with_name('estatico-independiente.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
for k,v in out['targets'].items():print(k,v)
print('calls',calls)
