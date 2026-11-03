from pathlib import Path
import struct,json,collections
ROOT=Path(__file__).resolve().parent
BASE=Path(r'C:\Emulador CD\backups')
FILES={'poseH':BASE/'p8-2inst-pose-anfitrion/cdmp_packets.host_20261015-083504.bin','poseG':BASE/'p8-2inst-pose-anfitrion/cdmp_packets.guest_20261015-083521.bin','s38H':BASE/'p8-sesion38/cdmp_packets_20261015-125539.bin','s38G':BASE/'p8-sesion38/shadow/cdmp_packets_20261015-115854.bin'}
OPS={2953,2945,2665,2671,2720,2136,2663,3000,2916,2525,2743,2758,2514,2609,3082,2779,2297,3089,2774,2485,2370,2900,2928,2221,2145,2516}
summary={}
for label,path in FILES.items():
 out=[];actors={};t0=None;counts=collections.Counter();bykey=collections.defaultdict(collections.Counter);gaps=0
 with path.open('rb') as f:
  magic,ver,pe=struct.unpack('<4sII',f.read(12));assert magic==b'CDPK'
  while True:
   off=f.tell();h=f.read(24)
   if not h:break
   assert len(h)==24
   raw_t,d,pad,size,n,pad2,sess,thread=struct.unpack('<QBBHHHII',h);data=f.read(n);assert len(data)==n
   if t0 is None:t0=raw_t
   t=raw_t-t0
   if sess==0xffffffff and not n:gaps+=thread;continue
   if n<5:continue
   op,slot,plen=struct.unpack_from('<HBH',data);pl=data[5:];counts[(d,op)]+=1
   if op==2005:
    pos=0
    while pos+2<=len(pl):
     ln=struct.unpack_from('<H',pl,pos)[0];rec=pl[pos+2:pos+2+ln]
     if len(rec)!=ln or ln<34:break
     typ=rec[0];key,owner=struct.unpack_from('<II',rec,1);ctl=struct.unpack_from('<I',rec,18)[0];nl=struct.unpack_from('<I',rec,30)[0]
     if nl>1000:break
     name=rec[34:34+2*nl].decode('utf-16le','replace');a={'t':t/1e6,'off':off,'record_off':off+24+5+pos+2,'type':typ,'key':f'{key:08X}','owner':f'{owner:08X}','controller':f'{ctl:08X}','name':name}
     actors.setdefault(f'{key:08X}',[]).append(a);pos+=ln+2
   if op in OPS:
    key=struct.unpack_from('<I',pl)[0] if len(pl)>=4 else 0
    bykey[f'{key:08X}'][(d,op)]+=1
    out.append({'t':t/1e6,'off':off,'d':d,'op':op,'size':size,'session':sess,'hex':pl.hex(),'key':f'{key:08X}'})
 (ROOT/f'{label}-packets.json').write_text(json.dumps(out),encoding='utf8')
 (ROOT/f'{label}-actors.json').write_text(json.dumps(actors,ensure_ascii=False),encoding='utf8')
 summary[label]={'path':str(path),'qpc_base_us':t0,'version':ver,'pe':hex(pe),'gaps':gaps,'counts':{f'{d}:{op}':num for (d,op),num in counts.items() if op in OPS},'actors':len(actors)}
 print(label,summary[label])
(ROOT/'resumen-capturas.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
