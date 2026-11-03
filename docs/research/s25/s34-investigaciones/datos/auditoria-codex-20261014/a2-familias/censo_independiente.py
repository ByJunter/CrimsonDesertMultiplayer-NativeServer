import pathlib,struct,json,re,collections
ROOT=pathlib.Path(r'C:\Emulador CD\backups\p8-sesion34'); OUT=pathlib.Path(__file__).parent
CASES={'A':('shadow/cdmp_packets_20261014-170455.bin','cdmp_20261014-180308.log','shadow/cdmp_20261014-170438.log',0x9010029A),'B':('shadow-p1b-p2b/cdmp_packets_20261014-175057.bin','paso1b/cdmp_20261014-184959.log','shadow-p1b-p2b/cdmp_20261014-175039.log',0x901001D4),'C':('shadow-p1b-p2b/cdmp_packets_20261014-180517.bin','paso2b/cdmp_20261014-190232.log','shadow-p1b-p2b/cdmp_20261014-180502.log',0x90100249)}
def u32(b,o=0): return struct.unpack_from('<I',b,o)[0]
def u16(b,o=0): return struct.unpack_from('<H',b,o)[0]
def key(k): return f'{k:08X}'
def family(b):
 candidates=[]
 for n in range(min(32,(len(b)-6)//4)+1):
  q=len(b)-6-4*n
  if u16(b,q+4)!=n: continue
  parent=u32(b,q); children=list(struct.unpack_from('<'+'I'*n,b,q+6))
  if (not parent or parent>>28 in (10,11)) and all(c>>28 in (10,11) for c in children): candidates.append((parent,children))
 return candidates

def run(label,spec):
 cap,hlog,glog,ctl=spec; actors={}; loads=[]; reqs={}; req2223={}; removals=collections.defaultdict(list); stats=collections.Counter(); first=0
 with (ROOT/cap).open('rb') as f:
  head=f.read(12); assert head[:4]==b'CDPK' and u32(head,8)==0x6AB28F00
  while True:
   off=f.tell(); hdr=f.read(24)
   if not hdr: break
   assert len(hdr)==24
   t,d,flags,size,copied,pad,s,th=struct.unpack('<QBBHHHII',hdr); b=f.read(copied); assert len(b)==copied
   if not first:first=t
   stats['records']+=1
   if s==0xffffffff and not copied:stats['lost']+=th;continue
   if copied!=size:stats['truncated']+=1;continue
   if len(b)<5:continue
   op=u16(b); payload=b[5:]
   if op==2005 and d==2:
    q=0
    while q<len(payload):
     n=u16(payload,q); assert n>0 and q+2+n<=len(payload)
     r=payload[q+2:q+2+n]; roff=off+24+5+q+2;q+=2+n
     if len(r)<22 or u32(r,1)>>28!=11:continue
     fc=family(r);stats['family_ambiguous']+=len(fc)>1;stats['family_missing']+=not fc
     par,kids=fc[0] if fc else (None,[])
     name=''
     if len(r)>=34 and (nc:=u32(r,30))<256 and 34+2*nc<=len(r):name=r[34:34+nc*2].decode('utf-16le','replace')
     a=dict(key=key(u32(r,1)),type=r[0],controller=key(u32(r,18)),parent=key(par or 0),children=[key(c) for c in kids],name=name,t=t,elapsed=(t-first)/1e6,offset=hex(roff),record_offset=hex(off),thread=th)
     loads.append(a); actors.setdefault(a['key'],a)
   if d==1 and op in (2223,2556,2734,2405) and len(payload)>=4 and u32(payload)>>28==11:
    reqs.setdefault(key(u32(payload)),dict(t=t,op=op,offset=hex(off)))
    if op==2223:req2223.setdefault(key(u32(payload)),dict(t=t,op=op,offset=hex(off)))
   if d==2 and op==2516 and len(payload)>=10:
    for q in range(6,len(payload)-3,4): removals[key(u32(payload,q))].append(dict(t=t,reason=payload[5],offset=hex(off)))
 roots=[a for a in actors.values() if a['type']==3 and a['controller']==key(ctl) and a['parent']=='00000000']
 fams=[]
 for a in roots:
  if not a['children']:continue
  pending=list(a['children']);seen=set();members=[];missing=[]
  while pending:
   k=pending.pop(0)
   if k in seen:stats['cycles']+=1;continue
   seen.add(k)
   if k not in actors:missing.append(k);continue
   child=actors[k];members.append(child);pending.extend(child['children'])
  mixed=[c for c in members if c['controller']!=key(ctl)]
  rq=reqs.get(a['key']); last=max([c['t'] for c in members],default=a['t'])
  fams.append(dict(root=a,members=members,missing=missing,mixed=bool(mixed),first_req=rq,first_2223=req2223.get(a['key']),root_to_req_ms=(rq['t']-a['t'])/1000 if rq else None,last_child_to_req_ms=(rq['t']-last)/1000 if rq else None,removals=removals[a['key']]))
 counts=collections.Counter((c['type'],c['controller']) for fam in fams for c in fam['members'])
 lines=(ROOT/hlog).read_text(encoding='utf-8-sig',errors='replace').splitlines(); births=[]; evs=[]; selected=[]
 for ln,line in enumerate(lines,1):
  if 'NpcActivation [' in line:
   ev=re.search(r'NpcActivation \[([^]]+)\] qpc=(\d+) T(\d+) \| key=0x([0-9A-F]+).*controlador=0x([0-9A-F]+) padre=0x([0-9A-F]+)',line)
   if ev:
    v=dict(event=ev[1],qpc=int(ev[2]),thread=int(ev[3]),key=ev[4],controller=ev[5],parent=ev[6],line=ln,text=line);evs.append(v)
    if ev[1]=='nacimiento':births.append(v)
  if 'Session2 observer hold: released NPC' in line and 'restore 0' in line: selected.append(dict(line=ln,text=line))
 bindex={a['key']:a for a in births}; links=[e for e in evs if e['event']=='familia' and e['parent']!='00000000']
 observed=[]
 for e in links:
  if e['key'] in bindex and e['parent'] in bindex:
   c=bindex[e['key']];p=bindex[e['parent']];observed.append(dict(child=c['key'],parent=p['key'],delta_us=c['qpc']-p['qpc'],same_thread=c['thread']==p['thread'],birth_lines=[p['line'],c['line']],family_line=e['line']))
 out=dict(case=label,paths=dict(capture=str(ROOT/cap),hostlog=str(ROOT/hlog),guestlog=str(ROOT/glog)),stats=dict(stats),root_count=len(roots),with_children=len(fams),complete=sum(not x['mixed'] and not x['missing'] for x in fams),mixed=sum(x['mixed'] for x in fams),missing_families=sum(bool(x['missing']) for x in fams),members={f'type{t}-ctl{c}':n for (t,c),n in counts.items()},families=fams,host_birth_count=len(births),host_birth_parent_nonzero=sum(x['parent']!='00000000' for x in births),host_family_links=len(links),observed_birth_pairs=observed,timeout_release_lines=selected)
 (OUT/f'{label}-censo.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf-8')
 print(label,{k:out[k] for k in ('stats','root_count','with_children','complete','mixed','missing_families','members','host_birth_count','host_birth_parent_nonzero','host_family_links')})
 print('birthpairs',len(observed),'orderbad',sum(x['delta_us']<=0 for x in observed),'threadbad',sum(not x['same_thread'] for x in observed))
 for fam in fams:
  if fam['mixed'] or fam['missing']: print('family',fam['root']['key'],fam['root']['name'],[(c['key'],c['type'],c['name'],round((c['t']-fam['root']['t'])/1e6,6)) for c in fam['members']],fam['missing'],fam['root_to_req_ms'],fam['last_child_to_req_ms'])
 return out
if __name__=='__main__':
 allout=[run(k,v) for k,v in CASES.items()]

