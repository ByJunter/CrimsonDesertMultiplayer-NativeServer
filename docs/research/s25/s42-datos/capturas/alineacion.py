from pathlib import Path
import json,statistics,hashlib
p=Path(r'C:\Emulador CD-enemigos\build\research\s42\enemigos-capturas')
def wall(s):
 h,m,v=s.split(':');return int(h)*3600+int(m)*60+float(v)
a=json.loads((p/'s38-host.json').read_text());b=json.loads((p/'s38-guest.json').read_text())
one={e['payload_hex']:e for e in a['events']if e['op']==2922};pairs=[]
for e in b['events']:
 if e['op']==2922 and e['payload_hex']in one:
  v=one[e['payload_hex']]
  pairs.append({'host_wall':v['wall'],'guest_wall_plus_1h':e['wall'],'guest_minus_host_s':round(wall(e['wall'])-wall(v['wall']),3),'host_offset':v['offset'],'guest_offset':e['offset'],'payload_sha256':hashlib.sha256(bytes.fromhex(e['payload_hex'])).hexdigest()})
x={'pairs':pairs,'median_delta_s':statistics.median(q['guest_minus_host_s']for q in pairs),'note':'El delta contiene desfase residual de reloj y latencia unidireccional. No se separan sin una medida adicional.'}
(p/'alineacion-shadow.json').write_text(json.dumps(x,ensure_ascii=False,indent=2),encoding='utf-8')
manifest=json.loads((p/'manifiesto.json').read_text())
for name in ('alineacion.py','alineacion-shadow.json','informe.md'):
 f=p/name;manifest.append({'kind':'derived','path':str(f),'size':f.stat().st_size,'sha256':hashlib.file_digest(f.open('rb'),'sha256').hexdigest()})
(p/'manifiesto.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
