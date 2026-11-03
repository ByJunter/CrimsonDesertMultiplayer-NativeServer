import pathlib,re,json,collections,runpy
O=pathlib.Path(__file__).parent
ns=runpy.run_path(str(O/'censo_independiente.py'));R=ns['ROOT'];cases=ns['CASES']
summary={}
for lab,(cap,hlog,glog,ctl) in cases.items():
 rows={};fl={}
 for ln,line in enumerate((R/glog).read_text(encoding='utf8',errors='replace').splitlines(),1):
  if 'fase recibido-tunel' not in line:continue
  m=re.search(r'NpcProbe \[invitado alta\].*clave 0x([0-9A-F]+) gen (\d+).*tipo (\d+) controlador 0x([0-9A-F]+) padre 0x([0-9A-F]+) hijos (\d+)',line)
  if m:rows.setdefault(m[1],dict(key=m[1],generation=int(m[2]),type=int(m[3]),controller=m[4],parent=m[5],n=int(m[6]),line=ln,children=[]))
  m=re.search(r'NpcProbe \[invitado familia\].*raiz 0x([0-9A-F]+).*hijos (\d+) \[([^]]*)\]',line)
  if m:fl[m[1]]=re.findall(r'0x([0-9A-F]+)',m[3])
 for k,kids in fl.items():
  if k in rows:rows[k]['children']=kids
 roots=[a for a in rows.values() if a['type']==3 and a['controller']==f'{ctl:08X}' and a['parent']=='00000000']; families=[]
 for a in roots:
  if not a['n']:continue
  pending=list(a['children']);seen=set();kids=[];missing=[]
  while pending:
   k=pending.pop(0)
   if k in seen:continue
   seen.add(k)
   if k not in rows:missing.append(k);continue
   c=rows[k];kids.append(c);pending.extend(c['children'])
  families.append(dict(root=a,children=kids,missing=missing,mixed=any(c['controller']!=a['controller'] for c in kids)))
 raw=json.loads((O/f'{lab}-censo.json').read_text());rawf={f['root']['key'] for f in raw['families']};logf={f['root']['key'] for f in families}
 result=dict(roots=len(roots),with_children=len(families),complete=sum(not f['missing'] and not f['mixed'] for f in families),mixed=sum(f['mixed'] for f in families),missing=sum(bool(f['missing']) for f in families),log_not_game=sorted(logf-rawf),game_not_log=sorted(rawf-logf),families=families)
 summary[lab]=result;print(lab,{k:v for k,v in result.items() if k!='families'})
 for k in result['log_not_game']:print('undelivered family',rows[k])
(O/'censo-log-vs-bin.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
