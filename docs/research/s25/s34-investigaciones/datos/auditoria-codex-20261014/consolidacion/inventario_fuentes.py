"""Inventario reproducible de fuentes S34 y validación independiente del contenedor CDPK."""
from pathlib import Path
import struct, json, hashlib, collections
OUT=Path(__file__).resolve().parent
ROOT=Path(r'C:\Emulador CD\backups\p8-sesion34')
REPORTS=Path(r'C:\Emulador CD-npc-s33-fix\docs\research\s25\s34-investigaciones')
files=[ROOT/'cdmp_packets_20261014-180319.bin',ROOT/'shadow/cdmp_packets_20261014-170455.bin',ROOT/'paso1b/cdmp_packets_20261014-185010.bin',ROOT/'shadow-p1b-p2b/cdmp_packets_20261014-175057.bin',ROOT/'paso2b/cdmp_packets_20261014-190242.bin',ROOT/'shadow-p1b-p2b/cdmp_packets_20261014-180517.bin']
rows=[]
for path in files:
    st=path.stat(); h=hashlib.sha256(); counts=collections.Counter(); gaps=[]; errors=[]; prev=None; first=None; last=None
    with path.open('rb') as f:
        head=f.read(12);h.update(head);magic,version,stamp=struct.unpack('<4sII',head)
        if magic!=b'CDPK':raise ValueError(str(path))
        while True:
            off=f.tell();header=f.read(24)
            if not header:break
            h.update(header)
            if len(header)!=24: errors.append({'offset':off,'error':'cabecera parcial'});break
            us,direction,pad,size,copied,pad2,session,thread=struct.unpack('<QBBHHHII',header)
            body=f.read(copied);h.update(body)
            if len(body)!=copied:errors.append({'offset':off,'error':'payload parcial'});break
            counts['registros']+=1;counts[f'dir_{direction}']+=1
            if copied<size:counts['truncados']+=1
            if copied>size:errors.append({'offset':off,'error':'copied > size'})
            if prev is not None and us<prev:counts['timestamp_regresivo']+=1
            prev=us;first=us if first is None else min(first,us);last=us if last is None else max(last,us)
            if session==0xffffffff and size==0 and copied==0:
                gaps.append({'offset':off,'us':us,'perdidos':thread});counts['registros_perdidos_declarados']+=thread
            elif copied>=5:
                op,slot,plen=struct.unpack_from('<HbH',body)
                if plen+5!=size:counts['longitud_troc_distinta']+=1
                counts[f'op_{op}']+=1
    rows.append({'ruta':str(path),'bytes':st.st_size,'sha256':h.hexdigest(),'version':version,'timestamp_pe':hex(stamp),'min_us':first,'max_us':last,'conteos':dict(counts),'huecos':gaps,'errores':errors})
manifest=[]
for p in sorted(REPORTS.glob('*.md')):
    if p.name.upper()=='AUDITORIA.MD':continue
    b=p.read_bytes();manifest.append({'ruta':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
(OUT/'inventario-fuentes.json').write_text(json.dumps({'informes':manifest,'capturas':rows},indent=2,ensure_ascii=False),encoding='utf8')
print(json.dumps([{'archivo':Path(x['ruta']).name,'registros':x['conteos']['registros'],'truncados':x['conteos'].get('truncados',0),'huecos':len(x['huecos']),'perdidos':x['conteos'].get('registros_perdidos_declarados',0),'errores':x['errores']} for x in rows],indent=2))
