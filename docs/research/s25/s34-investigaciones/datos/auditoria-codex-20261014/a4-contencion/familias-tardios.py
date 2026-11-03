from pathlib import Path
import re,json,statistics
OUT=Path(r'%TEMP%\codex-s34-auditoria-20261014\a4-contencion');R=json.loads((OUT/'recuentos.json').read_text());Z={}
for trial,x in R.items():
 lines=Path(x['host']).read_text(encoding='utf-8-sig').splitlines();queues={};actions={};six=[]
 for n,s in enumerate(lines,1):
  if 'NpcActivation [' not in s:continue
  v={k:int(v,16) if v.startswith('0x') else int(v) for k,v in re.findall(r'(\w+)=(0x[0-9A-Fa-f]+|-?\d+)',s)}
  if '[2005 encolado al invitado]' in s:queues[v['key']]=dict(v,line=n)
  if '[acción OnServer]' in s and v.get('aplicada')==1:actions[v['key']]=dict(v,line=n)
 for k,e in queues.items():
  if e.get('tipo')==6 and e.get('padre') in queues:
   q=queues[e['padre']];six.append(dict(key=hex(k),parent=hex(e['padre']),controller=hex(e['controlador']),delay_s=(e['qpc']-q['qpc'])/1e6,line=e['line'],parent_queue_line=q['line']))
 parent6={e['padre'] for e in queues.values() if e.get('tipo')==6}
 late=[dict(key=hex(k),elapsed_s=(a['qpc']-queues[k]['qpc'])/1e6,action_line=a['line'],has6=k in parent6) for k,a in actions.items() if k in queues and (a['qpc']-queues[k]['qpc'])>5e6]
 Z[trial]=dict(six_delays=six,late_actions=late);print(trial,json.dumps(Z[trial]))
(OUT/'familias-tardios.json').write_text(json.dumps(Z,indent=2),encoding='utf-8')
