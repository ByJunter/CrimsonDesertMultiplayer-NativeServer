from pathlib import Path
import struct,json,re,collections
U16=lambda b,o=0:struct.unpack_from('<H',b,o)[0]
U32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
BASE=Path(r'C:\Emulador CD\backups\p8-sesion34')
FILES={'A-host':'cdmp_packets_20261014-180319.bin','A-guest':'shadow/cdmp_packets_20261014-170455.bin','B-host':'paso1b/cdmp_packets_20261014-185010.bin','B-guest':'shadow-p1b-p2b/cdmp_packets_20261014-175057.bin','C-host':'paso2b/cdmp_packets_20261014-190242.bin','C-guest':'shadow-p1b-p2b/cdmp_packets_20261014-180517.bin'}
LOGS={'A-host':'cdmp_20261014-180308.log','B-host':'paso1b/cdmp_20261014-184959.log','C-host':'paso2b/cdmp_20261014-190232.log'}
def records(path):
 with path.open('rb')as f:
  header=f.read(12);assert header[:4]==b'CDPK';assert U32(header,4)==2
  num=0
  while True:
   pos=f.tell();h=f.read(24)
   if not h:break
   assert len(h)==24
   t,dr,pad,size,copied,pad2,session,thread=struct.unpack('<QBBHHHII',h);p=f.read(copied);assert len(p)==copied;num+=1
   yield num,pos,t,dr,size,session,thread,p
def family(b):
 candidates=[]
 for n in range((len(b)-6)//4+1):
  at=len(b)-6-4*n
  if U16(b,at+4)!=n:continue
  parent=U32(b,at);children=struct.unpack_from('<'+'I'*n,b,at+6)if n else ()
  if (parent==0 or parent>>28 in(10,11))and all(k>>28 in(10,11)for k in children):candidates.append((parent,children))
 return candidates
def restore(b):
 # Reconocimiento por ruta serializada y sus bytes vecinos, no importamos lector del autor.
 out=[]
 for m in re.finditer(rb'character[^\x00-\x1f]{0,1024}?app_xml',b):
  p,e=m.span()
  if p>=4 and U32(b,p-4)==e-p and e+8<len(b) and b[e+8] in(0,1):out.append((e+8,b[e+8],m.group().decode('ascii')))
 return out
def main():
 output={}
 for name,rel in FILES.items():
  births=[];actions=collections.defaultdict(list);alive={};orphans=[];gaps=[];bad=[];sizes=collections.Counter();first={}
  for num,pos,t,dr,size,session,thread,p in records(BASE/rel):
   if session==0xffffffff and not p:gaps.append((num,pos,thread));continue
   if len(p)<5:continue
   op=U16(p);pay=p[5:];sizes[op]+=1
   if dr==1 and op in(2223,2556,2734,2405,2291) and len(pay)>=4:actions[U32(pay)].append({'op':op,'t':t/1e6,'offset':hex(pos),'record':num})
   if dr!=2:continue
   if op==2174:alive.clear()
   if op==2516:
    for j in range(6,len(pay)-3,4):alive.pop(U32(pay,j),None)
   if op!=2005:continue
   at=0;batch=[]
   while at<len(pay):
    if at+2>len(pay):bad.append((num,'short length'));break
    n=U16(pay,at);at+=2
    if not n or at+n>len(pay):bad.append((num,'invalid length',n));break
    b=pay[at:at+n];begin=pos+24+5+at;at+=n
    if len(b)<0x16 or b[0]==0:continue
    fam=family(b);rs=restore(b);key=U32(b,1)
    row={'key':f'{key:08X}','type':b[0],'controller':f'{U32(b,0x12):08X}','owner':f'{U32(b,5):08X}','t':t/1e6,'offset':hex(begin),'record':num,'size':n,'family_candidates':len(fam),'restore_candidates':len(rs)}
    if len(fam)==1:row.update(parent=f'{fam[0][0]:08X}',children=[f'{x:08X}'for x in fam[0][1]])
    if len(rs)==1:row.update(restore=rs[0][1],restore_offset=hex(begin+rs[0][0]),app=rs[0][2])
    batch.append(row);births.append(row);first.setdefault(key,row)
   keys={x['key']for x in batch}
   for row in batch:
    parent=row.get('parent','00000000')
    if parent!='00000000' and parent not in alive and parent not in keys:orphans.append(row)
   for row in batch:alive[row['key']]=row
  split=[]
  for b in births:
   p=first.get(int(b.get('parent','0'),16))
   if p and b['controller']=='90100000' and p['controller']!='90100000':split.append(dict(b,parent_controller=p['controller'],parent_t=p['t'],delay=b['t']-p['t']))
  interesting={int(x['key'],16)for x in split}|{int(x['parent'],16)for x in split}|{0xB01001F6,0xB0100577,0xB0100578}
  first_packet=next(records(BASE/rel))[2]
  output[name]={'file':str(BASE/rel),'first_packet_t_us':first_packet,'time_note':'t=timestamp original del BIN / 1e6; restar first_packet_t_us/1e6 para el tiempo relativo de captura. No comparar t entre PC.','gaps':gaps,'bad_records':bad,'births':births,'split':split,'orphans':orphans,'actions':{f'{k:08X}':actions[k]for k in interesting},'totals':dict(sizes)}
  print(name,'births',len(births),'split',[(x['key'],x['type'],x.get('restore'),round(x['delay'],4))for x in split],'orphans',[(x['key'],x['parent'])for x in orphans],'gaps',gaps,'bad',bad[:5])
 logs={}
 for name,rel in LOGS.items():
  rows=[];previous=-1
  for i,line in enumerate((BASE/rel).read_text(encoding='utf-8',errors='replace').splitlines(),1):
   m=re.search(r'after the timeout with restore 0 (\d+)',line)
   if m and int(m[1])!=previous:rows.append({'line':i,'time':line[:14],'count':int(m[1])});previous=int(m[1])
  logs[name]={'file':str(BASE/rel),'timeout_counter_changes':rows};print(name,rows)
 output['logs']=logs
 output['latencies_2223']=[]
 for name,parents in [('A-guest',['B01004A4','B010049D']),('C-guest',['B010048E','B01004BE'])]:
  a=output[name]
  for k in parents:
   b=next(x for x in a['births']if x['key']==k);ch=[x for x in a['split']if x['parent']==k];req=next((x for x in a['actions'][k]if x['op']==2223),None)
   if req:output['latencies_2223'].append({'capture':name,'parent':k,'parent_record':b['record'],'parent_offset':b['offset'],'req':req,'since_parent_seconds':req['t']-b['t'],'since_last_child_seconds':req['t']-max(x['t']for x in ch)})
 Path(__file__).with_name('capturas-independientes.json').write_text(json.dumps(output,indent=2),encoding='utf-8')
if __name__=='__main__':main()
