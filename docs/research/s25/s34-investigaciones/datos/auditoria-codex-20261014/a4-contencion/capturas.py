from pathlib import Path
import struct,json,re,collections,statistics
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a4-contencion');R=json.loads((OUT/'recuentos.json').read_text(encoding='utf-8'))
BASE=Path(r'C:\Emulador CD\backups\p8-sesion34')
files={'B':BASE/'shadow-p1b-p2b/cdmp_packets_20261014-175057.bin','C':BASE/'shadow-p1b-p2b/cdmp_packets_20261014-180517.bin'}
S={}
for trial,p in files.items():
 blob=p.read_bytes(); magic,ver,stamp=struct.unpack_from('<4sII',blob); assert magic==b'CDPK'
 counts=collections.Counter();rows=[];pos=12;truncated=0;gaps=0;bad=0;last=0
 while pos<len(blob):
  start=pos;t,d,_,size,copied,_,session,thread=struct.unpack_from('<QBBHHHII',blob,pos);pos+=24
  b=blob[pos:pos+copied];assert len(b)==copied;pos+=copied;last=t;truncated+=copied<size
  if not b and session==0xffffffff:gaps+=thread;continue
  if len(b)<5:continue
  op=struct.unpack_from('<H',b)[0];n=struct.unpack_from('<H',b,3)[0];pay=b[5:5+n];counts[op]+=1
  if len(pay)!=n:bad+=1;continue
  if d==2 and op==2005:
   off=0
   while off<len(pay):
    if off+2>len(pay):bad+=1;break
    sz=struct.unpack_from('<H',pay,off)[0];off+=2;r=pay[off:off+sz]
    if len(r)!=sz:bad+=1;break
    if len(r)>=22:
     ty=r[0];key=struct.unpack_from('<I',r,1)[0];ctl=struct.unpack_from('<I',r,18)[0]
     if key>>28==11:rows.append(dict(t=t/1e6,offset=start,record_offset=start+24+5+off,op=op,key=key,type=ty,controller=ctl))
    off+=sz
  elif d==2 and op==2516 and len(pay)>=6:
   for off in range(6,len(pay)-3,4):rows.append(dict(t=t/1e6,offset=start,op=op,key=struct.unpack_from('<I',pay,off)[0],reason=pay[5]))
  elif d==2 and op in (2126,2335):rows.append(dict(t=t/1e6,offset=start,op=op))
 # Binary t_us is absolute QPC; textual capture t subtracts its first-packet origin. Validate that origin separately.
 log=Path(R[trial]['guest']).read_text(encoding='utf-8-sig')
 origin=[]
 for m in re.finditer(r'qpc ([\d.]+) s .*?capture t=([\d.]+)',log):origin.append(float(m[1])-float(m[2]))
 orig=statistics.median(origin);offset=R[trial]['offset_s']
 for e in rows:e['qpc']=e['t']-offset
 meta=dict(file=str(p),size=len(blob),version=ver,stamp=hex(stamp),gaps=gaps,truncated=truncated,malformed=bad,record_ops=dict(counts),capture_qpc_origin=orig,last_t=last/1e6)
 if trial=='B':
  # Use all primary capture events in time windows; not inferred duplicates by key only.
  arrival=[e for e in rows if 68646.8<=e['qpc']<=68649]
  departure=[e for e in rows if 68700<=e['qpc']<=68724]
  meta['arrival']=arrival;meta['departure']=departure
  for name,window in [('arrival',arrival),('departure',departure)]:
   by=collections.defaultdict(list)
   for e in window:by[(e['op'],e.get('reason',-1),e.get('type',-1))].append(e)
   print(trial,name,[(k,len(es),min(e['qpc'] for e in es),max(e['qpc'] for e in es),es[0]['offset'],es[-1]['offset']) for k,es in by.items()])
  # complete observed reason42 lifetimes (right censored remains separate)
  alive={};lifes=[];birth=[];death=[];duplicates=[];population=[]
  for e in rows:
   if e['op']==2005:
    if e['key'] in alive:duplicates.append(e)
    alive[e['key']]=e
    if 68724<=e['qpc']<=68753.5 and e['type']==3:birth.append(e)
   elif e['op']==2516:
    a=alive.pop(e['key'],None)
    if 68724<=e['qpc']<=68753.5 and e['reason']==42:
     death.append(e)
     if a and a['type']==3:lifes.append(e['qpc']-a['qpc'])
   if 68724<=e['qpc']<=68753.5:population.append(sum(x['type']==3 for x in alive.values()))
  meta['loop']=dict(birth3=len(birth),reason42=len(death),complete_lives=len(lifes),p50=statistics.median(lifes) if lifes else 0,population_min=min(population),population_max=max(population),duplicates_existing_key=len(duplicates),duplicate_examples=duplicates[:5])
  print('LOOP',meta['loop'])
 if trial=='B':
  known={};deathgroups=collections.Counter();localcohort=[]
  for e in rows:
   if e['op']==2005:known[e['key']]=e
   if e['op']==2516 and e['reason']==19 and 68700<e['qpc']<68702:deathgroups[known.get(e['key'],{}).get('type',-1)]+=1
  meta['departure_reason19_types']=dict(deathgroups)
  meta['arrival_births_extended']=[e for e in rows if e['op']==2005 and e['type']==3 and 68647.7<=e['qpc']<=68649.5]
  meta['guest_first_repop']=next(e for e in rows if e['op']==2005 and e['type']==3 and 68722.1<=e['qpc']<=68724)
  print('DEPARTURE types',dict(deathgroups),'ARRIVAL extended n',len(meta['arrival_births_extended']),'REPOP',meta['guest_first_repop'])
 S[trial]=meta
 print(trial,'capture integrity', {k:v for k,v in meta.items() if k not in ['record_ops','arrival','departure','loop','arrival_births_extended','departure_reason19_types','guest_first_repop']})
(OUT/'capturas.json').write_text(json.dumps(S,ensure_ascii=False,indent=2),encoding='utf-8')




