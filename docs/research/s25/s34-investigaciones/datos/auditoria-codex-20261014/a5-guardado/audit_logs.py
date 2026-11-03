from pathlib import Path
import re,json,collections,struct,hashlib,mmap
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a5-guardado')
ROOT=Path(r'C:\Emulador CD\backups\p8-sesion34')
logs={'A':ROOT/'cdmp_20261014-180308.log','B':ROOT/'paso1b/cdmp_20261014-184959.log','C':ROOT/'paso2b/cdmp_20261014-190232.log'}
results={}
for tag,path in logs.items():
 rows=[]; stats=collections.Counter(); calls=[]; stack={}; redirects=[]
 with path.open(encoding='utf-8-sig',errors='replace') as f:
  for n,s in enumerate(f,1):
   if not ('Session2 guest save probe:' in s or 'Session2 save redirect #' in s or 'Session2 save redirect:' in s or 'Session2 guest save: EXPORTED' in s): continue
   if len(s)>2500: continue
   rows.append(f'{n}: {s.rstrip()}')
   typ=re.search(r'guest save probe: (LoadSlot|SavePlayerToSlot|FlushPendingSaveData)',s)
   if typ:
    owner=re.search(r'= (the host|user 2|unidentified|unknown)',s)
    if owner: stats[typ[1]+':'+owner[1]]+=1
    if typ[1]=='SavePlayerToSlot' and '0x186E450 from' in s:
     tid=re.search(r'\[T(\d+)\]',s)[1]; call={'line':n,'entry':s.rstrip(),'redirects':[]}; calls.append(call); stack[tid]=call
    elif typ[1]=='SavePlayerToSlot' and ' returned ' in s:
     tid=re.search(r'\[T(\d+)\]',s)[1]
     if tid in stack: stack.pop(tid)['return']={'line':n,'text':s.rstrip()}
   if 'Session2 save redirect #' in s:
    tid=re.search(r'\[T(\d+)\]',s)[1]; m=re.search(r'#(\d+) (\S+)',s)
    redirects.append({'line':n,'text':s.rstrip()})
    if m: stats['redirect:'+m[2]]+=1
    if tid in stack: stack[tid]['redirects'].append({'line':n,'text':s.rstrip()})
 (OUT/f'log-{tag}-extract.txt').write_text('\n'.join(rows),encoding='utf-8')
 results[tag]={'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'stats':dict(stats),'calls':calls,'redirects':redirects}
(OUT/'logs.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
for tag,r in results.items():
 print(tag,r['stats'])
 for c in r['calls']: print('SAVE',c['line'],c['entry'],'return',c.get('return'),'redirects',len(c['redirects']))
