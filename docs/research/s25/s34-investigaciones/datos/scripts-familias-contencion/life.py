import re,sys,collections
G=sys.argv[1]; OFF=-68241.56537; lo,hi=68722.2,68753.4
alta={}; life=[]; m=collections.Counter(); n_alta=0; first=set()
keyscene={}
for ln in open(G,encoding='utf-8',errors='replace'):
    r=re.search(r'NpcProbe \[invitado (alta|baja)\] qpc ([\d.]+) s .*?clave (0x[0-9A-F]+)(.*)$',ln)
    if not r: continue
    t=float(r.group(2))-OFF; k=r.group(3)
    if r.group(1)=='alta':
        tp=re.search(r'tipo (\d+)',r.group(4)).group(1)
        if lo<=t<hi and tp=='3': alta[k]=t; n_alta+=1
    else:
        mo=re.search(r'motivo (\d+)',r.group(4)).group(1)
        if k in alta and lo<=t<hi:
            m[mo]+=1
            if mo=='42': life.append(t-alta[k])
life.sort()
print('type3 altas in window',n_alta,'bajas by motivo of those',dict(m))
if life: print('life to m42 n',len(life),'p10 %.1f p50 %.1f p90 %.1f'%(life[len(life)//10],life[len(life)//2],life[int(len(life)*.9)]))
