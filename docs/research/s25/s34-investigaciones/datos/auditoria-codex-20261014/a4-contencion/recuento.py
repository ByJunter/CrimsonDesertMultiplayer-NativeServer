from pathlib import Path
import re,json,statistics,struct,collections
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a4-contencion')
BASE=Path(r'C:\Emulador CD\backups\p8-sesion34')
paths={'A':(BASE/'cdmp_20261014-180308.log', BASE/'shadow/cdmp_20261014-170438.log'), 'B':(BASE/'paso1b/cdmp_20261014-184959.log',BASE/'shadow-p1b-p2b/cdmp_20261014-175039.log'),'C':(BASE/'paso2b/cdmp_20261014-190232.log',BASE/'shadow-p1b-p2b/cdmp_20261014-180502.log')}
def num(x):return int(x,16) if x.startswith('0x') else int(x)
def quant(a):
 if not a:return {}
 a=sorted(a)
 def q(p):
  x=(len(a)-1)*p;i=int(x);return a[i]+(a[min(i+1,len(a)-1)]-a[i])*(x-i)
 return dict(n=len(a),p10=q(.1),p50=q(.5),p90=q(.9),p99=q(.99),max=max(a),over5=sum(x>5 for x in a))
def host(p):
 events=[];extra=[]
 for ln,s in enumerate(p.read_text(encoding='utf-8-sig').splitlines(),1):
  m=re.search(r'NpcActivation \[([^]]+)\] qpc=(\d+)',s)
  if m:
   e=dict(event=m[1],t=int(m[2])/1e6,line=ln,raw=s)
   e.update({k:num(v) for k,v in re.findall(r'([\w]+)=(0x[0-9a-fA-F]+|-?\d+)',s)})
   events.append(e)
  if 'User2Stages [' in s and len(s)<3000:
   q=re.search(r'qpc (\d+\.\d+) s',s)
   if q:extra.append(dict(t=float(q[1]),line=ln,raw=s))
 return events,extra

def guest(p):
 e=[]
 for ln,s in enumerate(p.read_text(encoding='utf-8-sig').splitlines(),1):
  m=re.search(r'NpcProbe \[invitado (alta|baja|primera-Req)\] qpc ([\d.]+) s',s)
  if not m:continue
  k=re.search(r'clave (0x[0-9A-Fa-f]+) gen (\d+)',s)
  d=dict(event=m[1],t=float(m[2]),line=ln,key=int(k[1],16),gen=int(k[2]),raw=s)
  for f in ['tipo','controlador','padre','motivo','op']:
   v=re.search(r'\b'+f+r' (0x[0-9A-Fa-f]+|\d+)',s)
   if v:d[f]=num(v[1])
  e.append(d)
 return e
R={}
for trial,(hp,gp) in paths.items():
 h,ex=host(hp);g=guest(gp)
 queued={e['key']:e for e in h if e['event']=='2005 encolado al invitado'}
 applied={e['key']:e for e in reversed(h) if e['event']=='acción OnServer' and e.get('aplicada')==1}
 req={e['key']:e for e in reversed(h) if e['event']=='primera petición' and e.get('adicional')==2}
 received={e['key']:e for e in reversed(g) if e['event']=='alta'}
 greq={e['key']:e for e in reversed(g) if e['event']=='primera-Req'}
 pairs=[received[k]['t']-e['t'] for k,e in queued.items() if k in received]
 offset=min(pairs) if pairs else None
 actions=[e for e in h if e['event']=='acción OnServer']
 x=dict(host=str(hp),guest=str(gp),action_lines=len(actions),zero_sender=sum(e.get('emisor')==0 for e in actions),applied1=sum(e.get('aplicada')==1 for e in actions),progress0=sum(e.get('progreso')==0 for e in actions),queued=len(queued),received_pairs=len(pairs),offset_s=offset)
 if offset:
  x['latencies']={name:quant([other[k]['t']-e['t']-off for k,e in queued.items() if k in other]) for name,other,off in [('Req_guest_excess',greq,offset),('Req_host',req,0),('applied_host',applied,0),('receive_excess',received,offset)]}
  cens=[]
  for k,e in queued.items():
   if k not in applied:cens.append(dict(key=hex(k),queued_line=e['line'],received=k in received,req=k in req,followup=max(z['t'] for z in h)-e['t']))
  x['no_observed_application']=cens
  arm=[e for e in ex if 'User2Stages [ARMED]' in e['raw']]
  x['arms']=arm
  if arm:
   a=arm[0]['t'];x['burst']=quant([received[k]['t']-e['t']-offset for k,e in queued.items() if k in received and a+8<=e['t']<=a+10])
 diagnoses=[e for e in h if e['event']=='sin progreso']
 x['diagnoses']=len(diagnoses)
 x['diagnoses_applied_before']=sum(e['key'] in applied and applied[e['key']]['t']<e['t'] for e in diagnoses)
 x['diagnoses_without_prior_application']=[e for e in diagnoses if e['key'] not in applied or applied[e['key']]['t']>=e['t']]
 if trial=='C':
  cutoff=69380.44
  retired=[e for e in h if e['event']=='retirada local' and e.get('asignado')==1 and e['t']<cutoff]
  x['before_cutoff']=dict(cutoff=cutoff,diagnoses=sum(e['t']<cutoff for e in diagnoses),retired_roots=len(retired),retired_root_keys=len({e['key'] for e in retired}),retired_member_lines=sum(e['event']=='retirada local' and e.get('asignado')==0 and e['t']<cutoff for e in h),guest_reason25_matching=sum(e['event']=='baja' and e.get('motivo')==25 and e['key'] in {e['key'] for e in retired} for e in g))
  x['nodes_blocked']=len([e for e in h if e['event']=='nodo bloqueado'])
 if trial=='B':
  x['relevo']= [e for e in ex if 68635<=e['t']<=68650 or 68700<=e['t']<=68725]
  x['guest_relevo']=[dict(e,aligned=e['t']-offset) for e in g if 68640<=e['t']-offset<=68650 or 68700<=e['t']-offset<=68725]
 R[trial]=x
(OUT/'recuentos.json').write_text(json.dumps(R,ensure_ascii=False,indent=2),encoding='utf-8')
for k,v in R.items():
 print(k,json.dumps({a:b for a,b in v.items() if a not in ['relevo','guest_relevo','arms','no_observed_application']},ensure_ascii=False,indent=2))
