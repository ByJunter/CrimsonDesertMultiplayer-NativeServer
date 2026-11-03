import re, sys, collections
H=sys.argv[1]
rx = re.compile(r'NpcActivation \[([^\]]+)\] qpc=(\d+) T\d+ \| (.*)$'); kv=re.compile(r'(\w+)=(\S+)')
ev=collections.defaultdict(dict)
for ln in open(H,encoding='utf-8',errors='replace'):
    m=rx.search(ln)
    if not m: continue
    d=dict(kv.findall(m.group(3))); k=d.get('key'); t=m.group(1)
    if k and t not in ev[k]: ev[k][t]=(int(m.group(2)),d)
c=collections.defaultdict(list)
for k,e in ev.items():
    if 'familia' not in e: continue
    d=e['familia'][1]; p=d['padre']
    if p not in ev or '2005 encolado al invitado' not in ev[p] or '2005 encolado al invitado' not in e: 
        c[(d['tipo'],d['controlador'],'no-enq')].append(0); continue
    c[(d['tipo'],d['controlador'])].append((e['2005 encolado al invitado'][0]-ev[p]['2005 encolado al invitado'][0])/1e6)
for k,v in c.items():
    v=sorted(v); print(k,len(v),'median',v[len(v)//2],'max',v[-1], '>=5s', sum(1 for x in v if x>=5))
