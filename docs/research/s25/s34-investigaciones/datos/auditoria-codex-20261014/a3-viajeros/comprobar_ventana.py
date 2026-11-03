import pathlib,runpy,io,contextlib,json,collections
p=pathlib.Path(__file__).parent
with contextlib.redirect_stdout(io.StringIO()): x=runpy.run_path(str(p/'auditar_viajeros.py'))
out={}
for alias in ['G1b','G2b']:
 z=x['D'][alias];groups=collections.defaultdict(list)
 for k,a in z['actors'].items():
  if int(a['requester'],16)>>28!=10 or not a['road']:continue
  m=x['dist'](z['motions'].get(k,[]),tmin=a['t'],d=2,ops=(2697,2991));groups[(a['requester'],a['type'])].append((a,m))
 out[alias]=[{'requester':r,'type':t,'n':len(v),'has_position':sum(m['samples']>0 for a,m in v),'gt1':sum((m['max'] or 0)>1 for a,m in v),'zero_or_missing':[(a['key'],m['samples'],m['max']) for a,m in v if not (m['max'] or 0)>1]} for (r,t),v in groups.items()]
(p/'posiciones-tras-alta.json').write_text(json.dumps(out,indent=2),encoding='utf8');print(json.dumps({a:[{k:v for k,v in g.items() if k!='zero_or_missing'} for g in gs] for a,gs in out.items()},indent=2))
