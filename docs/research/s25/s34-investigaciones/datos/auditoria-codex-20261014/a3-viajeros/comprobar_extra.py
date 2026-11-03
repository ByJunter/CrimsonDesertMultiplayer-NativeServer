import runpy,pathlib,contextlib,io,json,re,collections,struct
out=pathlib.Path(__file__).parent
with contextlib.redirect_stdout(io.StringIO()):x=runpy.run_path(str(out/'auditar_viajeros.py'));pe=runpy.run_path(str(out/'auditar_pe.py'))
D=x['D'];summary={}
for suf in ('1b','2b'):
 h,g=D['H'+suf],D['G'+suf];req='A01001D5' if suf=='1b' else 'A010024A';co=[a for a in g['actors'].values() if a['requester']==req and a['road']];keys=[int(a['key'],16) for a in co];roadids=set();entries=0
 for t,offset,raw,sess in g['roadreq']:
  p=bytes.fromhex(raw)
  assert (len(p)-5)%24==0
  entries+=(len(p)-5)//24
  roadids.update(struct.unpack_from('<I',p,o)[0] for o in range(5,len(p),24))
 event=[]
 for k in keys:
  a=h['actors'][k];event.append((a['t'],1,k));rs=h['removals'].get(k,[])
  if rs:event.append((rs[0][0],-1,k))
 active=set();peak=[]
 for t,sgn,k in sorted(event):
  if sgn>0:active.add(k)
  else:active.discard(k)
  if len(active)>len(peak):peak=sorted(active);pt=t
 summary[suf]={'2550_packets':len(g['roadreq']),'2550_entries':entries,'road_ids':[hex(v) for v in sorted(roadids)],'roads_matched':sum(a['road'] in roadids for a in co),'unmatched':[(a['key'],a['type'],hex(a['road']),a['name']) for a in co if a['road'] not in roadids],'peak_active_host_capture':len(peak),'peak_time':pt,'peak_keys':[hex(k) for k in peak]}
 if suf=='2b':
  summary[suf]['four_missing']=[{**g['actors'][int(k,16)],'events_guest':g['motions'].get(int(k,16),[]),'events_host':h['motions'].get(int(k,16),[])} for k in x['summary']['paired2b']['no_recv_position']]
logfiles={'H1b':pathlib.Path(str(x['FILES']['H1b']).replace('cdmp_packets_20261014-185010.bin','cdmp_20261014-184959.log')),'H2b':pathlib.Path(str(x['FILES']['H2b']).replace('cdmp_packets_20261014-190242.bin','cdmp_20261014-190232.log'))}
for alias,p in logfiles.items():
 entries=[];birth=[];witness=[]
 for n,l in enumerate(p.read_text(encoding='utf-8',errors='replace').splitlines(),1):
  if 'NpcActivation [viajero]' in l:
   (entries if 'tipo=0 ' in l else birth).append((n,l))
  if 'User2Stages [state]' in l and ('distance 6' in l or 'distance 0' in l or 'distance 1 ' in l):witness.append((n,l))
  if '[owner load]' in l or '[prepare]' in l or '[drain at host prepare]' in l:witness.append((n,l))
 summary[alias]={'road_log_entries':len(entries),'road_log_births':len(birth),'first_entries':entries[:1],'first_births':birth[:1],'world_state':witness}
# Search direct displacement stores only, not a proof against alias/copy/VM writes.
b=pe['b'];sec=next(s for s in pe['secs'] if s[0]=='.rsrc');raw=b[sec[3]:sec[3]+sec[4]];candidates=[]
for pat in (rb'\xc6[\x80-\xbf]\xcd\x03\x00\x00\x01',rb'\x66\xc7[\x80-\xbf]\xcc\x03\x00\x00[\x00-\xff]\x01'):
 for m in re.finditer(pat,raw):
  rv=sec[1]+m.start();f=pe['fn'](rv);confirmed=False
  if f:
   for i in pe['md'].disasm(pe['read'](f[0],f[1]-f[0]),0x140000000+f[0]):
    if i.address-0x140000000==rv:confirmed=True;break
  candidates.append({'rva':hex(rv),'pdata':[hex(v) for v in f] if f else None,'instruction_boundary':confirmed,'bytes':m.group().hex(' ')})
summary['direct_3cd_stores_rsrc']=candidates
(out/'comprobaciones-extra.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v if k not in ('1b','2b','H1b','H2b') else {a:b for a,b in v.items() if a not in ('four_missing','world_state','peak_keys')} for k,v in summary.items()},ensure_ascii=False,indent=2))
