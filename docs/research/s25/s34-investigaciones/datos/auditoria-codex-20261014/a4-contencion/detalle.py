import sys,json,re,statistics,collections
from pathlib import Path
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a4-contencion');R=json.loads((OUT/'recuentos.json').read_text())
for t in ('A','B','C'):
 print(t,'unobserved application',len(R[t]['no_observed_application']),R[t]['no_observed_application'][:12])
x=R['B'];g=x['guest_relevo'];offset=x['offset_s']
for lo,hi in [(68646.8,68649),(68700,68702),(68722,68724),(68722,68753.5),(68724.422,68753.422)]:
 z=[e for e in g if lo<=e['aligned']<=hi]
 print('logwindow',lo,hi,collections.Counter((e['event'],e.get('tipo'),e.get('motivo'),hex(e.get('controlador',0))) for e in z))
# direct entire guest log times, selective lines
log=Path(x['guest']).read_text(encoding='utf-8-sig').splitlines();events=[]
for n,s in enumerate(log,1):
 m=re.search(r'NpcProbe \[invitado (alta|baja)\] qpc ([\d.]+)',s)
 if m:
  k=re.search(r'clave (0x[0-9A-F]+)',s);ty=re.search(r'tipo (\d+)',s);reason=re.search(r'motivo (\d+)',s);ctl=re.search(r'controlador (0x[0-9A-F]+)',s)
  events.append(dict(t=float(m[2])-offset,line=n,event=m[1],key=k[1],ty=int(ty[1]) if ty else None,reason=int(reason[1]) if reason else None,ctl=ctl[1] if ctl else None))
for lo,hi in [(68646.8,68649),(68700,68702),(68722,68724),(68722,68753.5),(68724.422,68753.422)]:
 z=[e for e in events if lo<=e['t']<=hi]
 print('alllogwindow',lo,hi,collections.Counter((e['event'],e['ty'],e['reason'],e['ctl']) for e in z))
 for what in [('baja',None,19),('alta',3,None)]:
  e=[e for e in z if (e['event'],e['ty'],e['reason'])==what]
  if e:print(what,len(e),'first',e[0],'last',e[-1])
# host stages exact widths
ex=x['relevo']
for lo,hi,who in [(68647,68650,'host'),(68721,68724,'user 2')]:
 es=[e for e in ex if lo<=e['t']<=hi and '[create]' in e['raw'] and '('+who+')' in e['raw']]
 print('stages',who,len(es),'width',es[-1]['t']-es[0]['t'], 'first',es[0], 'last',es[-1])
