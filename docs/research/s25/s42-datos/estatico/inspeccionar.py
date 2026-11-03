import sys, pathlib, json, hashlib, struct
sys.path.insert(0,r'C:\Emulador CD\build\research')
import cdpe
out=pathlib.Path(r'C:\Emulador CD-enemigos\build\research\s42\enemigos-estatico'); pe=cdpe.PE()
rv=[0xa35b90,0x209cb30,0x22141e0,0x20df100,0x22156e0,0x2797510,0x2068340,0x287d4c0]
rows=[]
for rva in rv:
 st=cdpe.func_of(pe,rva); st=st if st is not None else rva; entries=[x for x in cdpe._pd if x[0]==st]; end=entries[0][1] if entries else rva+0x100
 text='\n'.join(pe.fmt(i,{}) for i in pe.md.disasm(pe.read(st,end-st),pe.base+st))
 (out/f'{rva:x}-disasm.txt').write_text(text,encoding='utf-8')
 sig=pe.read(rva,24); s=pe.section_of(rva); raw=pe.read(s[1],s[4]); rows.append(dict(rva=hex(rva),section=s[0],pdata=[hex(st),hex(end)] if entries else None,pdata_present=bool(entries),first24=sig.hex(' '),aob_count_section=raw.count(sig)))
vts={}
for vt in [0x55c4f28,0x59385f0]:
 addr=pe.u64(vt+52*8)-pe.base; vts[hex(vt)]={'vf52':hex(addr),'section':pe.secname(addr),'bytes':pe.read(addr,32).hex(' ')}
res={'exe_sha256':hashlib.sha256(pe.data).hexdigest(),'functions':rows,'vtable52':vts}
(out/'primarias.json').write_text(json.dumps(res,indent=2),encoding='utf-8')
print('Verificadas',len(rows),'entradas; salida',out/'primarias.json')
