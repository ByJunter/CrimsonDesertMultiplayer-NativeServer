from pathlib import Path
import json,re,hashlib
p=Path(r'C:\Emulador CD-enemigos\build\research\s42\enemigos-capturas')
data={}
wanted={'0xB010164D','0xB010164E','0xB010164F','0xB0101650','0xB0101654','0xB0101656','0xB010175F'}
for label in ('local-host','local-guest','s38-host','s38-guest'):
 x=json.loads((p/(label+'.json')).read_text(encoding='utf-8'))
 data[label]={'actors':[a for a in x['actors']if label.startswith('local')and a['key']in wanted],
 'events':[e for e in x['events']if e['op']in(2597,2530,2740)or(e['op']==2147 and any(k['key']in wanted for k in e.get('keys',[])))],
 'first_requests':{k:v for k,v in x['firsts'].items()if k.split('/')[0]in wanted and label.startswith('local')}}
(p/'evidencia-minima.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
extract=[]
logs={'local-host':r'C:\Emulador CD\backups\p8-enemigos-invitado-20261015\host.log','local-guest':r'C:\Emulador CD\backups\p8-enemigos-invitado-20261015\guest.log'}
for label,path in logs.items():
 for n,line in enumerate(Path(path).read_text(encoding='utf-8-sig',errors='replace').splitlines(),1):
  if '=== MARK' in line or ('C12CB783' in line and any(s in line for s in ('[censo]','[create]','[load]','[control]','[sin progreso]'))):
   extract.append(f'{label}:{n}: {line}')
(p/'extractos-log.txt').write_text('\n'.join(extract)+'\n',encoding='utf-8')
manifest=json.loads((p/'manifiesto.json').read_text())
for name in ('evidencia-minima.json','extractos-log.txt','archivar.py'):
 f=p/name;manifest.append({'kind':'derived','path':str(f),'size':f.stat().st_size,'sha256':hashlib.file_digest(f.open('rb'),'sha256').hexdigest()})
(p/'manifiesto.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
