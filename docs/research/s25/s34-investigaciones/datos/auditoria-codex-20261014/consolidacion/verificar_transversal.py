"""Auditoría independiente: PE, firmas y recuentos de sondas S34. Solo lectura."""
from pathlib import Path
import struct, json, hashlib, mmap, re, collections, argparse
import capstone
BASE=Path(__file__).resolve().parent
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--codigo',type=Path,default=BASE.parent/'codigo-d835806',help='Instantánea src/tests de d835806 extraída por git archive')
SRC=ap.parse_args().codigo
EXE=Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe')
LOGROOT=Path(r'C:\Emulador CD\backups\p8-sesion34')
LOGS={'A':LOGROOT/'cdmp_20261014-180308.log','B':LOGROOT/'paso1b/cdmp_20261014-184959.log','C':LOGROOT/'paso2b/cdmp_20261014-190232.log'}
out={'logs':{},'pe':{},'firmas':[]}
for trial,path in LOGS.items():
    b=path.read_bytes(); lines=b.decode('utf-8-sig',errors='replace').splitlines()
    items=[]; counter=collections.Counter()
    for no,line in enumerate(lines,1):
        if 'NpcActivation [acción OnServer]' not in line: continue
        fields={k:v for k,v in re.findall(r'(emisor|resultado|rama|aplicada|progreso|controlador|key)=([^ ,;]+)',line)}
        counter['total']+=1
        for k,v in fields.items(): counter[k+'='+v]+=1
        items.append({'linea':no,'texto':line,'campos':fields})
    out['logs'][trial]={'ruta':str(path),'sha256':hashlib.sha256(b).hexdigest(),'recuento':dict(counter),'registros':items}
with EXE.open('rb') as f:
    d=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
    p=struct.unpack_from('<I',d,0x3c)[0]; n=struct.unpack_from('<H',d,p+6)[0]; opt=p+24; osize=struct.unpack_from('<H',d,p+20)[0]
    imagebase=struct.unpack_from('<Q',d,opt+24)[0]; timestamp=struct.unpack_from('<I',d,p+8)[0]
    sections=[]
    for i in range(n):
        name,vsize,rva,rsize,raw=struct.unpack_from('<8sIIII',d,opt+osize+i*40)
        sections.append({'nombre':name.rstrip(b'\0').decode('ascii'),'rva':rva,'vsize':vsize,'raw':raw,'rsize':rsize})
    def section(r): return next((s for s in sections if s['rva']<=r<s['rva']+max(s['vsize'],s['rsize'])),None)
    def off(r):
        s=section(r)
        if not s or r-s['rva']>=s['rsize']: raise ValueError(hex(r))
        return s['raw']+r-s['rva']
    exr,exn=struct.unpack_from('<II',d,opt+112+3*8)
    pdata=[struct.unpack_from('<III',d,off(exr)+i) for i in range(0,exn,12)]
    out['pe']={'ruta':str(EXE),'base':hex(imagebase),'timestamp':hex(timestamp),'secciones':sections,'pdata_entradas':len(pdata)}
    cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    points=[0x2792A7B,0x2A78CA0,0x281CEE0,0x186E450,0x186F570,0x1871930,0x20688A0,0x287EF60,0x88F832,0x1831598,0x235A800,0x2358EF0,0x2A827B0,0x1434880,0x2068340]
    for r in points:
        raw=off(r); blob=d[raw:raw+32]; runtime=next((x for x in pdata if x[0]<=r<x[1]),None)
        start=runtime[0] if runtime else r
        # La pertenencia al límite se comprueba decodificando desde inicio de runtime_function.
        boundary=False
        for ins in cs.disasm(d[off(start):raw+32],start):
            if ins.address==r: boundary=True; break
            if ins.address>r: break
        exact_count=0; pos=0
        while True:
            pos=d.find(blob,pos)
            if pos<0: break
            exact_count+=1;pos+=1
        out['firmas'].append({'rva':hex(r),'seccion':section(r)['nombre'],'runtime_function':[hex(x) for x in runtime] if runtime else None,'entrada_pdata':bool(runtime and runtime[0]==r),'limite_desde_pdata':boundary if runtime else None,'bytes32':blob.hex(' '),'coincidencias_bytes32':exact_count,'instrucciones':[f'{i.address:x}: {i.mnemonic} {i.op_str}' for i in cs.disasm(blob,r)]})
    # Verifica firmas exactas declaradas en el hook de acciones/nodos.
    text=(SRC/'src/mod/session2/npc_action_probe.cpp').read_text(encoding='utf-8-sig')
    checks=[]
    for name,r in [('kActionBytes',0x2A78CA0),('kNodeBytes',0x281CEE0)]:
        block=re.search(r'constexpr uint8_t '+name+r'\[\] = \{(.*?)\};',text,re.S).group(1)
        signature=bytes(int(v,16) for v in re.findall(r'0x([0-9A-Fa-f]+)',block))
        positions=[];pos=0
        while True:
            pos=d.find(signature,pos)
            if pos<0:break
            positions.append(pos);pos+=1
        checks.append({'nombre':name,'rva':hex(r),'bytes':signature.hex(' '),'coincide':d[off(r):off(r)+len(signature)]==signature,'ocurrencias':len(positions),'offsets':positions})
    out['firmas_codigo']=checks
    d.close()
(BASE/'verificacion-transversal.json').write_text(json.dumps(out,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({'logs':{k:{a:b for a,b in v['recuento'].items() if not a.startswith('key=')} for k,v in out['logs'].items()},'pe':{k:v for k,v in out['pe'].items() if k!='secciones'},'firmas':[{'rva':x['rva'],'seccion':x['seccion'],'entrada_pdata':x['entrada_pdata'],'limite_desde_pdata':x['limite_desde_pdata'],'coincidencias32':x['coincidencias_bytes32']} for x in out['firmas']],'firmas_codigo':out['firmas_codigo']},indent=2,ensure_ascii=False))
