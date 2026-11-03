from pathlib import Path
import json,struct,collections,hashlib,re
p=Path(r'C:\Emulador CD-enemigos\build\research\s42\enemigos-capturas')
def u32(b,o=0):return struct.unpack_from('<I',b,o)[0]
def hx(n):return '0x%08X'%n
def strip(e):return {k:v for k,v in e.items()if k not in ('record_hex','payload_hex','keys')}
cohort={'labels':{},'limitations':['Los huecos explícitos no permiten concluir ausencia absoluta.','2922 es resultado de impacto; sin decodificar flags no equivale a daño de vida.','El nombre Bandit no demuestra por sí solo hostilidad configurada.']}
table=[]
for label in ('local-host','local-guest','s38-host','s38-guest'):
 x=json.loads((p/(label+'.json')).read_text(encoding='utf-8'));h={}
 h['capture']=x['capture'];h['log']=x['log'];h['stats']=x['stats'];h['marks']=x['marks'];h['raw_first_us']=x['raw_first_us'];h['base_wall_seconds']=x['base_wall_seconds'];h['anchor_range_s']=x['anchor_range']
 rel=[]
 for e in x['events']:
  if e['op']!=2597:continue
  b=bytes.fromhex(e['payload_hex']);assert b[0]==4 and (len(b)-6)%13==0
  rel.append(dict(strip(e),target=hx(u32(b,1)),reset=b[5],pairs=[{'a':u32(b,o),'b':u32(b,o+4),'relation':b[o+8],'expiry':u32(b,o+9)}for o in range(6,len(b),13)]))
 h['relations']=rel
 h['actors']=[]
 for k in ('0xB010164D','0xB010164E','0xB010164F','0xB0101650','0xB0101654','0xB0101656','0xB010175F'):
  if not label.startswith('local'):break
  aa=[a for a in x['actors']if a['key']==k]
  if not aa:continue
  a=aa[0];b=bytes.fromhex(a['record_hex']);aa2=dict(strip(a),user_owner=hx(u32(b,5)),character_info=u32(b,22),faction=u32(b,26),actions=x['actions'].get(k,{}))
  es=[e for e in x['events']if e['op']in(2147,2530,2922,2740,2516) and (e.get('key')==k or any(i['key']==k and i['at'] in (0,4) for i in e.get('keys',[])))]
  aa2['impacts_received']=sum(e['op']==2922 and e['payload_hex'].startswith(bytes.fromhex(k[2:])[::-1].hex())for e in es)
  aa2['hp']=[]
  for e in es:
   if e['op']!=2147:continue
   b=bytes.fromhex(e['payload_hex']);aa2['hp'].append(dict(strip(e),stat=u32(b,4),hp_scaled=struct.unpack_from('<q',b,8)[0],max_hp_scaled=struct.unpack_from('<q',b,24)[0],sequence=u32(b,64),source=hx(u32(b,84))))
  aa2['death']=[strip(e)for e in es if e['op']==2530]
  aa2['crime']=[strip(e)for e in es if e['op']==2740]
  aa2['removal']=[strip(e)for e in es if e['op']==2516]
  aa2['impact_offsets']=[e['offset']for e in es if e['op']==2922]
  h['actors'].append(aa2)
  if label=='local-guest':table.append('| '+ ' | '.join([k,a['name'],str(a['type']),str(aa2['faction']),a['controller'],a['parent'],str(aa2['impacts_received']),str(len(aa2['hp'])),str(len(aa2['death'])),str(a['offset'])])+' |')
 h['bandit_births']=[dict(strip(a),faction=u32(bytes.fromhex(a['record_hex']),26))for a in x['actors']if a['name']=='Bandit']
 h['hp_by_key']=dict(collections.Counter(hx(u32(bytes.fromhex(e['payload_hex'])))for e in x['events']if e['op']==2147))
 cohort['labels'][label]=h
(p/'cohorte.json').write_text(json.dumps(cohort,ensure_ascii=False,indent=2),encoding='utf-8')
(p/'tabla-cohorte.md').write_text('| Clave | Nombre | Tipo | Facción | Controlador | Padre | 2922 recibidos | 2147 | 2530 | Offset 2005 invitado |\n|---|---|---|---|---|---|---|---|---|---|\n'+'\n'.join(table)+'\n',encoding='utf-8')
manifest=[]
for label,h in cohort['labels'].items():
 for kind in ('capture','log'):
  path=Path(h[kind]);digest=hashlib.file_digest(path.open('rb'),'sha256').hexdigest();manifest.append({'label':label,'kind':kind,'path':str(path),'size':path.stat().st_size,'sha256':digest})
for path in (p/'analizar.py',p/'finalizar.py',p/'cohorte.json',p/'tabla-cohorte.md'):
 manifest.append({'kind':'derived','path':str(path),'size':path.stat().st_size,'sha256':hashlib.file_digest(path.open('rb'),'sha256').hexdigest()})
(p/'manifiesto.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print((p/'tabla-cohorte.md').read_text())
for lab,h in cohort['labels'].items():print(lab,'bandits',len(h['bandit_births']),'factions',collections.Counter(a['faction']for a in h['bandit_births']),'rel',h['relations'])
