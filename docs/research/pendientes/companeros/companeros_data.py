import sys,struct,json,pathlib,importlib.util
ROOT=pathlib.Path(r'<scratch>\pendientes')
OUT=ROOT/'companeros'; OUT.mkdir(exist_ok=True)
sys.path.insert(0,r'<scratch>\rpt0')
from pe import PE
import capstone
p=PE(r'D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe')
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
chunks=[]
for r,n in [(0x8AF580,0x220),(0x206B2D0,0x410),(0x4F8FF0,9),(0x1436A90,0x130),(0x898D890,0x3b0),(0x8BDC17,0x90),(0x8BDF3D,0x40),(0x8646D0,0x100),(0x35CD50,0x365),(0x35D0C0,0x1d5),(0x2329700,0x2ad)]:
    chunks.append(f'RVA {r:X}, section {p.sec(r)[0]}')
    for i in md.disasm(p.read(r,n),p.base+r): chunks.append(f'{i.address-p.base:08X} {i.bytes.hex():24s} {i.mnemonic} {i.op_str}')
(OUT/'capstone.txt').write_text('\n'.join(chunks),encoding='utf-8')
def read(path):
    b=path.read_bytes(); assert b[:4]==b'CDPK'; o=12; out=[]
    while o+24<=len(b):
        t,d,_,size,cop,_,s,th=struct.unpack_from('<QBBHHHII',b,o); o+=24
        data=b[o:o+cop]; assert len(data)==cop; o+=cop
        out.append((t,d,size,s,th,data))
    assert o==len(b)
    return out
def recs(b):
    o=5; out=[]
    while o<len(b):
        assert o+2<=len(b)
        n=struct.unpack_from('<H',b,o)[0]; o+=2
        assert n>0 and o+n<=len(b)
        out.append(b[o:o+n]); o+=n
    return out
def cc(r):
    hits=[]
    for m in range(0x20,len(r)-60):
      try:
        o=m; ls=[]
        for k in range(2):
            f=r[o]; o+=1
            if f==0:
                a,h,t=struct.unpack_from('<HIQ',r,o);o+=14
                if h in (0,0xffffffff) or a>0x2000: raise ValueError()
                ls.append([a,hex(h),t])
            elif f==1: ls.append(None)
            else: raise ValueError()
        if ls==[None,None]: continue
        flag=r[o];o+=1
        if flag>1: continue
        o+=flag+16+1
        n=struct.unpack_from('<I',r,o)[0];o+=4
        if n>1024: continue
        s=r[o:o+n];o+=n
        if not s.startswith(b'character') or not s.endswith(b'app_xml') or not all(32<=x<127 for x in s):continue
        o+=8
        if r[o] not in (0,1):continue
        hits.append({'offset':hex(m),'restore':r[o],'layers':ls,'app':s.decode()})
      except (ValueError,IndexError,struct.error):pass
    return hits
lines=['Indexado CDPK desde cero; tiempos relativos al primer registro; CC por gramática serializada y ruta app_xml, no offsets fijos.']
for fn in [r'D:\cdmp-grabaciones\c1g.bin',r'D:\cdmp-grabaciones\e3b.bin',r'C:\Emulador CD\captures\cdmp_packets_20261003-112627.bin',r'C:\Emulador CD\captures\cdmp_packets_20261003-125936.bin']:
    R=read(pathlib.Path(fn));t0=R[0][0]; births={};events=[]
    for j,(t,d,size,s,th,b) in enumerate(R):
        if len(b)<3:continue
        op=struct.unpack_from('<H',b)[0]
        if op==2005 and d==2 and len(b)==size:
          try: rs=recs(b)
          except AssertionError:continue
          for r in rs:
            if len(r)<18 or r[0]==0:continue
            ty=r[0];key,owner=struct.unpack_from('<II',r,1);idx=r[9]
            x=cc(r)
            if owner>>28==9 and ((t-t0)<65000000):events.append([j,round((t-t0)/1e6,6),ty,hex(key),hex(owner),idx,[(a['offset'],a['restore']) for a in x]])
            if key in births:continue
            births[key]={'packet':j,'t':round((t-t0)/1e6,6),'type':ty,'key':hex(key),'owner':hex(owner),'slot':idx,'cc':x}
        if op in (2223,2723,2991) and len(b)>=9:
            key=struct.unpack_from('<I',b,5)[0]
            if key in births and 'first_action' not in births[key]:
                births[key]['first_action']={'packet':j,'t':round((t-t0)/1e6,6),'op':op,'dir':d,'delta':round((t-t0)/1e6-births[key]['t'],6)}
    selected=[x for x in births.values() if int(x['owner'],16)>>28==9 or x['type']==1]
    lines.append(fn+'\nTODAS_CARGAS_INICIALES='+json.dumps(events,ensure_ascii=False)+'\n'+json.dumps(selected,ensure_ascii=False,indent=2))
    zero=[x for x in births.values() if x['type']==3 and len(x['cc'])==1 and x['cc'][0]['restore']==0]
    lines.append('Ejemplos NPC0='+json.dumps(zero[:8],ensure_ascii=False,indent=2))
    vals=[x['first_action']['delta'] for x in zero if 'first_action'in x and x['first_action']['op']==2223]
    lines.append('NPC0 conteo='+str(len(zero))+' firstReq='+str(len(vals))+' mediana='+str(sorted(vals)[len(vals)//2] if vals else None))
(OUT/'capturas.txt').write_text('\n'.join(lines),encoding='utf-8')
print('Guardados capstone.txt y capturas.txt en',OUT)
