import re, sys, collections, datetime
G=sys.argv[1]; OFF=float(sys.argv[2]); ANQ=float(sys.argv[3]); ANW=sys.argv[4]; T0=sys.argv[5]; T1=sys.argv[6]
def hw(q):
    t=datetime.datetime.strptime(ANW,'%H:%M:%S.%f')+datetime.timedelta(seconds=q-ANQ); return t.strftime('%H:%M:%S.%f')[:-3]
def wq(w):
    return ANQ+(datetime.datetime.strptime(w,'%H:%M:%S')-datetime.datetime.strptime(ANW,'%H:%M:%S.%f')).total_seconds()
scene_owner={}; scene_del={}
ev=[]; keyscene={}
for ln in open(G,encoding='utf-8',errors='replace'):
    m=re.search(r'GuestSchedRoute \[(2126|2335)[^\]]*\] qpc ([\d.]+) s .*?scene (0x[0-9A-F]+) of character (0x[0-9A-F]+)',ln)
    if m:
        op,q,sc,ch=m.groups(); q=float(q)-OFF
        if op=='2335': scene_owner[sc]=('u2' if ch=='0xA01001D5' else 'host', q)
        else: scene_del[sc]=q
        ev.append((q,op,sc,ch)); continue
    m=re.search(r'NpcProbe \[invitado (alta|baja|estado)\] qpc ([\d.]+) s .*?clave (0x[0-9A-F]+)(.*)$',ln)
    if not m: continue
    tag,q,k,rest=m.groups(); q=float(q)-OFF
    if tag=='estado':
        s=re.search(r'escena (0x[0-9A-F]+)/',rest)
        if s and s.group(1)!='0x0' and k not in keyscene: keyscene[k]=s.group(1)
        continue
    if tag=='alta':
        p=re.search(r'controlador (0x[0-9A-F]+) padre (0x[0-9A-F]+)',rest); ev.append((q,'alta',k,p.groups()))
    else:
        mo=re.search(r'motivo (\d+)',rest); ev.append((q,'baja',k,mo.group(1)))
ev.sort()
# parent scene inheritance
def cat(k, info):
    sc=keyscene.get(k)
    if not sc and info and info[1]!='0x00000000': sc=keyscene.get(info[1])
    if not sc: return 'nosc'
    o=scene_owner.get(sc)
    if not o: return 'sc?'
    if o[0]=='u2': return 'u2'
    return 'hostHernand' if sc in scene_del and 'HERN' else 'host'
# decide host scenes deleted at 18:55:51 as Hernand
q51=wq('18:55:51'); hern={sc for sc,q in scene_del.items() if q51-1<=q<=q51+2 and scene_owner.get(sc,('',))[0]=='host'}
print('host Hernand scenes deleted ~18:55:51:',len(hern))
def cat2(k,info):
    sc=keyscene.get(k)
    if not sc and info and info[1]!='0x00000000': sc=keyscene.get(info[1])
    if not sc: return 'nosc'
    o=scene_owner.get(sc)
    if not o: return 'sc?'
    if o[0]=='u2': return 'u2'
    return 'hostHern' if sc in hern else 'hostOther'
live={}; q0=wq(T0); q1=wq(T1); out=collections.OrderedDict(); per=collections.defaultdict(collections.Counter)
for q,kind,k,info in ev:
    if kind=='alta': live[k]=(cat2(k,info), info)
    elif kind=='baja':
        c=live.pop(k,None)
        if q0<=q<q1: per[int(q-q0)]['baja m'+info+' '+(c[0] if c else '?')]+=1
        continue
    else:
        if q0<=q<q1: per[int(q-q0)][kind+' '+('u2' if info=='0xA01001D5' else 'host')]+=1
        continue
    if q0<=q<q1:
        per[int(q-q0)]['alta '+live[k][0]+' ctl'+info[0][-4:]]+=1
    if q0<=q<q1: out[int(q-q0)]=collections.Counter(v[0] for v in live.values())
last=None
for s in range(int(q1-q0)):
    if s in out: last=out[s]
    if per.get(s): print(hw(q0+s), 'live', dict(last) if last else '-', '|', dict(per[s]))
