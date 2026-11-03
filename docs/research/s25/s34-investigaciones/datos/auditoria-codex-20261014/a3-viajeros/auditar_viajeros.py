# Auditoria independiente S34. Stdlib. Formato primario capture_file.h y npc_probe_client_logic.h.
import struct,json,math,collections,statistics,pathlib
OUT=pathlib.Path(__file__).parent;BASE=pathlib.Path(r'C:\Emulador CD\backups\p8-sesion34')
FILES={'H1b':BASE/'paso1b/cdmp_packets_20261014-185010.bin','H2b':BASE/'paso2b/cdmp_packets_20261014-190242.bin','G1b':BASE/'shadow-p1b-p2b/cdmp_packets_20261014-175057.bin','G2b':BASE/'shadow-p1b-p2b/cdmp_packets_20261014-180517.bin'}
def u32(b,o):return struct.unpack_from('<I',b,o)[0]
def family(r):
 for n in range(33):
  a=len(r)-6-4*n
  if a<0:break
  if struct.unpack_from('<H',r,a+4)[0]!=n:continue
  p=u32(r,a);cs=list(struct.unpack_from('<'+'I'*n,r,a+6))
  if (p==0 or p>>28 in (10,11)) and all(c>>28 in (10,11) for c in cs):return p,cs
 return None,[]
def read(path):
 actors={};motions=collections.defaultdict(list);roadreq=[];removals=collections.defaultdict(list);stats=collections.Counter();t0=None;anomalies=[]
 with path.open('rb') as f:
  magic,ver,stamp=struct.unpack('<4sII',f.read(12));assert (magic,ver,stamp)==(b'CDPK',2,0x6AB28F00)
  while True:
   offset=f.tell();h=f.read(24)
   if not h:break
   assert len(h)==24
   t,d,flags,size,copied,pad,session,thread=struct.unpack('<QBBHHHII',h);data=f.read(copied);assert len(data)==copied;stats['records']+=1
   if session==0xffffffff and copied==0:stats['gap_lost']+=thread;anomalies.append(['gap',offset,t,thread]);continue
   if copied!=size:stats['truncated']+=1;continue
   if len(data)<5:stats['short']+=1;continue
   if t0 is None:t0=t
   ts=(t-t0)/1e6;op=struct.unpack_from('<H',data)[0];n=struct.unpack_from('<H',data,3)[0];p=data[5:5+n]
   if n>len(data)-5:stats['oversized_wire']+=1;continue
   if flags:stats['mod_extra']+=1
   if op==2550 and d==1:roadreq.append([ts,offset,p.hex(),session])
   if op==2005 and d==2:
    i=0
    while i+2<=len(p):
     length=struct.unpack_from('<H',p,i)[0];i+=2
     if length<5 or i+length>len(p):stats['bad_2005']+=1;anomalies.append(['2005',offset,i,length,len(p)]);break
     r=p[i:i+length];key=u32(r,1);typ=r[0];i+=length
     if key>>28!=11 or length<22:continue
     ctl=u32(r,18);req=road=sub=0;name='';par,cs=family(r)
     if len(r)>=34:
      end=34+2*u32(r,30)
      if end<=len(r):name=r[34:end].decode('utf-16le',errors='replace')
      if end+100<=len(r):req=u32(r,end+88);sub=u32(r,end+92);road=u32(r,end+96)
     a=actors.setdefault(key,{'key':f'{key:08X}','t':ts,'offset':offset,'record_offset':offset+24+5+i-length,'type':typ,'controller':f'{ctl:08X}','requester':f'{req:08X}','road':road,'sub':sub,'name':name,'parent':f'{par:08X}' if par is not None else None,'children':[f'{c:08X}' for c in cs],'load_count':0,'controllers':[],'load_events':[]})
     a['load_count']+=1
     if f'{ctl:08X}' not in a['controllers']:a['controllers'].append(f'{ctl:08X}')
     a['load_events'].append([ts,offset,flags])
   elif op==2516 and d==2 and len(p)>=6:
    for i in range(6,len(p)-3,4):removals[u32(p,i)].append([ts,p[5],offset])
   elif op in (2223,2556,2734,2405,2291,2828,2697,2991) and len(p)>=4:
    key=u32(p,0)
    if key>>28!=11:continue
    xyz=None;posoff={2223:142,2991:143,2556:4,2697:4}.get(op)
    if posoff is not None and len(p)>=posoff+12:
     xyz=list(struct.unpack_from('<fff',p,posoff))
     if not all(math.isfinite(x) for x in xyz):xyz=None
    motions[key].append([ts,d,op,xyz,offset,flags])
 return {'path':str(path),'t0':t0,'stats':dict(stats),'actors':actors,'motions':motions,'roadreq':roadreq,'removals':removals,'anomalies':anomalies}
def dist(events,tmin=-1,tmax=1e20,d=None,ops=None):
 ps=[e for e in events if e[3] is not None and tmin<=e[0]<tmax and (d is None or e[1]==d) and (ops is None or e[2] in ops) and not e[5]]
 if not ps:return {'samples':0,'max':None}
 x0,z0=ps[0][3][0],ps[0][3][2];ds=[math.hypot(e[3][0]-x0,e[3][2]-z0) for e in ps];idx=max(range(len(ds)),key=ds.__getitem__)
 return {'samples':len(ps),'max':max(ds),'first':ps[0],'peak':ps[idx],'last':ps[-1]}
D={a:read(p) for a,p in FILES.items()};summary={};detail={}
for alias,z in D.items():
 groups=collections.defaultdict(list);allroad={}
 for key,a in z['actors'].items():
  if int(a['requester'],16)>>28!=10 or not a['road']:continue
  events=z['motions'].get(key,[]);v=dict(a);v['recv']=dist(events,d=2,ops=(2697,2991));v['send']=dist(events,d=1,ops=(2223,2556));v['req_counts']=dict(collections.Counter(str(e[2]) for e in events if e[1]==1 and not e[5]));v['removals']=z['removals'].get(key,[]);groups[(a['requester'],a['type'])].append(v);allroad[f'{key:08X}']=v
 summary[alias]={'stats':z['stats'],'anomalies':z['anomalies'],'t0':z['t0'],'road_packets':len(z['roadreq']),'groups':[]}
 for (req,typ),aa in groups.items():summary[alias]['groups'].append({'requester':req,'type':typ,'count':len(aa),'controllers':dict(collections.Counter(a['controller'] for a in aa)),'recv_gt1':sum((a['recv']['max'] or 0)>1 for a in aa),'recv_samples':sum(a['recv']['samples']>0 for a in aa),'send_gt1':sum((a['send']['max'] or 0)>1 for a in aa),'send_samples':sum(a['send']['samples']>0 for a in aa)})
 detail[alias]=allroad
for suf in ('1b','2b'):
 h=D['H'+suf];g=D['G'+suf];req='A01001D5' if suf=='1b' else 'A010024A';cohort=[a for a in detail['G'+suf].values() if a['requester']==req];keys=[int(a['key'],16) for a in cohort]
 offs=[h['actors'][k]['t']-g['actors'][k]['t'] for k in h['actors'].keys()&g['actors'].keys()]
 far=[];survivors=[];lat=[];ev=collections.Counter();gre=collections.Counter();limit=261.952745 if suf=='1b' else 1e20
 for k in keys:
  if k not in h['actors']:continue
  actor=h['actors'][k];he=h['motions'].get(k,[]);m=dist(he,tmax=limit,d=1,ops=(2223,2556))
  if m['samples'] and actor['t']<limit-15:far.append({'key':f'{k:08X}','type':actor['type'],'name':actor['name'],'max':m['max'],'birth':actor['t'],'evidence':m})
  first=next((e for e in he if e[1]==1 and not e[5]),None)
  if first:lat.append(first[0]-actor['t'])
  ev.update(str(e[2]) for e in he if e[1]==1 and e[0]<limit and not e[5]);gre.update(str(e[2]) for e in g['motions'].get(k,[]) if e[1]==1 and not e[5])
 if suf=='1b':
  for a in far:
   if a['max']>1:continue
   k=int(a['key'],16)
   if not any(r[0]<271.126643 for r in h['removals'].get(k,[])):survivors.append({'key':a['key'],'type':a['type'],'near':dist(h['motions'].get(k,[]),tmin=271.126643,tmax=325.397111,d=1,ops=(2223,2556))})
 childkeys={int(c,16) for a in cohort for c in a['children']};children=[g['actors'][k] for k in childkeys if k in g['actors'] and k not in keys];childreq=collections.Counter(str(e[2]) for k in childkeys if k not in keys for e in g['motions'].get(k,[]) if e[1]==1)
 summary['paired'+suf]={'requester':req,'cohort_n':len(keys),'both_n':sum(k in h['actors'] for k in keys),'align_n':len(offs),'median_delta':statistics.median(offs),'far_n':len(far),'far_gt1':[a for a in far if a['max']>1],'survivors':len(survivors),'survivors_gt1':sum((a['near']['max'] or 0)>1 for a in survivors),'req_latency_n':len(lat),'req_latency_median':statistics.median(lat),'req_latency_max':max(lat),'host_req_counts':dict(ev),'guest_req_counts':dict(gre),'extra_children_n':len(children),'extra_children_types':dict(collections.Counter(a['type'] for a in children)),'extra_children_guest_reqs':dict(childreq),'no_recv_position':[a['key'] for a in cohort if a['recv']['samples']==0]}
 detail['far'+suf]=far;detail['survivors'+suf]=survivors;detail['children'+suf]=children
(OUT/'recuentos.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8');(OUT/'cohortes-evidencias.json').write_text(json.dumps(detail,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(summary,ensure_ascii=False,indent=2))
