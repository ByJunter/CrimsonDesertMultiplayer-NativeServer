"""Recuenta la escena candidata directamente desde .bin; no requiere los JSON grandes previos."""
from pathlib import Path
import json,struct,collections
ROOT=Path(__file__).resolve().parent
FILES=json.loads((ROOT/'resumen-capturas.json').read_text())
UUID=bytes.fromhex('4fb9eeab670f74563c343d640c000000')
XYZ=struct.pack('<fff',-10668.78515625,617.0676879882812,-3838.27294921875)
KEYS={0xB0100393,0xB0100394}
all_results={}
for label,meta in FILES.items():
 result={'path':meta['path'],'qpc_base_us':None,'gaps':0,'truncated_records':0,'bad_frames':0,'counts':collections.Counter(),'events':[],'matching_geometry':[],'positive_family':[],'family_motion':[]}
 with Path(meta['path']).open('rb') as f:
  magic,version,pe=struct.unpack('<4sII',f.read(12));assert (magic,version,pe)==(b'CDPK',2,0x6ab28f00)
  while True:
   off=f.tell();head=f.read(24)
   if not head:break
   assert len(head)==24
   raw,d,_,size,n,_,session,thread=struct.unpack('<QBBHHHII',head);data=f.read(n);assert len(data)==n
   if result['qpc_base_us'] is None:result['qpc_base_us']=raw
   t=(raw-result['qpc_base_us'])/1e6
   if session==0xffffffff and n==0:result['gaps']+=thread;continue
   if n!=size:result['truncated_records']+=1;continue
   if n<5:continue
   op,slot,plen=struct.unpack_from('<HBH',data);p=data[5:]
   if plen!=len(p):result['bad_frames']+=1;continue
   matched=False
   if op==2928 and len(p)>=25 and p[0]==4 and (len(p)-9)%16==0:
    matched=any(p[i:i+16]==UUID for i in range(9,len(p),16))
   elif op==2145 and len(p)>=22 and p[0]==4 and (len(p)-6)%16==0:
    matched=any(p[i:i+16]==UUID for i in range(6,len(p),16))
   elif op==2953 and len(p)>=21 and p[0]==4 and (len(p)-5)%16==0:
    matched=any(p[i:i+16]==UUID for i in range(5,len(p),16))
   elif op==2665 and len(p)==615 and struct.unpack_from('<I',p,4)[0]==1:
    matched=p[8:24]==UUID
   if matched:
    result['counts'][f'{d}:{op}']+=1
    event={'t':t,'off':off,'direction':d,'opcode':op,'payload_hex':p.hex()}
    if op==2665:event.update(state_window_24_40=p[24:40].hex(),geometry_exact=XYZ in p)
    result['events'].append(event)
   if op==2005:
    pos=0
    while pos+2<=len(p):
     ln=struct.unpack_from('<H',p,pos)[0];rec=p[pos+2:pos+2+ln]
     if len(rec)!=ln or ln<34:break
     typ=rec[0];key=struct.unpack_from('<I',rec,1)[0];ctl=struct.unpack_from('<I',rec,18)[0];namelen=struct.unpack_from('<I',rec,30)[0]
     a={'t':t,'off':off,'record_off':off+31+pos,'type':typ,'key':f'{key:08X}','controller':f'{ctl:08X}','len':ln,'name':rec[34:34+2*namelen].decode('utf16','replace')}
     if typ==7 and XYZ in rec:a['geometry_offsets']=[i for i in range(ln-11) if rec[i:i+12]==XYZ];result['matching_geometry'].append(a)
     if label.startswith('pose') and key in KEYS:result['positive_family'].append(a)
     pos+=ln+2
   if op==3089 and len(p)>=8 and label.startswith('pose'):
    if struct.unpack_from('<I',p)[0] in KEYS:result['family_motion'].append({'t':t,'off':off,'direction':d,'payload_hex':p.hex()})
 result['counts']=dict(result['counts'])
 all_results[label]=result
 print(label, result['counts'],'gaps',result['gaps'],'truncated',result['truncated_records'],'geometry',len(result['matching_geometry']))
(ROOT/'evidencia-resumen.json').write_text(json.dumps(all_results,indent=2),encoding='utf8')