from pathlib import Path
import struct,json,re,collections,statistics
ROOT=Path(r'C:\Emulador CD-enemigos\build\research\s42\enemigos-capturas')
configs=[('local-host',r'C:\Emulador CD\backups\p8-enemigos-invitado-20261015\host.log',r'C:\Emulador CD\backups\p8-enemigos-invitado-20261015\cdmp_packets.host_20261015-123008.bin',0),('local-guest',r'C:\Emulador CD\backups\p8-enemigos-invitado-20261015\guest.log',r'C:\Emulador CD\backups\p8-enemigos-invitado-20261015\cdmp_packets.guest_20261015-123024.bin',0),('s38-host',r'C:\Emulador CD\backups\p8-sesion38\cdmp_20261015-125527.log',r'C:\Emulador CD\backups\p8-sesion38\cdmp_packets_20261015-125539.bin',0),('s38-guest',r'C:\Emulador CD\backups\p8-sesion38\shadow\cdmp_20261015-115835.log',r'C:\Emulador CD\backups\p8-sesion38\shadow\cdmp_packets_20261015-115854.bin',3600)]
def wall(s):
 h,m,v=s.split(':');return 3600*int(h)+60*int(m)+float(v)
def clock(v):return '%02d:%02d:%06.3f'%(int(v//3600),int(v//60)%60,v%60)
def u32(p,o=0): return struct.unpack_from('<I',p,o)[0]
def hx(v):return '0x%08X'%v
def fam(r):
 for n in range(33):
  if len(r)<6+4*n:continue
  if struct.unpack_from('<H',r,len(r)-2-4*n)[0]!=n:continue
  par=u32(r,len(r)-6-4*n);children=[u32(r,len(r)-4*n+4*i) for i in range(n)]
  if (not par or par>>28 in (10,11)) and all(k>>28 in (10,11) for k in children):return hx(par),[hx(k)for k in children]
 return None,[]
selected={2147,2922,2509,2846,2530,2422,2740,2882,2567,2211,3040,2285,2798,2383,2597,2756,2208,2307}
for label,log,bin,shift in configs:
 anchors=[];marks=[]
 for line,txt in enumerate(Path(log).read_text(encoding='utf-8-sig',errors='replace').splitlines(),1):
  m=re.search(r'\(~(\d\d:\d\d:\d\d\.\d+)\) capture t=(\d+\.\d+)',txt)
  if m:anchors.append(wall(m[1])+shift-float(m[2]))
  if '=== MARK' in txt:marks.append({'line':line,'wall':clock(wall(txt[1:13])+shift),'text':txt})
 base=statistics.median(anchors); raw_first=None; counts=collections.Counter(); actors=[];events=[];actions=collections.defaultdict(collections.Counter);firsts={};stats=collections.Counter()
 with open(bin,'rb')as f:
  head=f.read(12);assert head[:4]==b'CDPK';version,pe=struct.unpack('<II',head[4:])
  while True:
   off=f.tell();h=f.read(24)
   if not h:break
   if len(h)!=24:raise Exception('truncated header')
   t,d,pad,size,n,pad2,session,thread=struct.unpack('<QBBHHHII',h);b=f.read(n);assert len(b)==n;stats['records']+=1
   if raw_first is None:raw_first=t
   if not n and session==0xffffffff:stats['gap_records']+=1;stats['lost_records']+=thread;continue
   if n<size:stats['truncated']+=1
   if len(b)<5:continue
   op=struct.unpack_from('<H',b)[0];plen=struct.unpack_from('<H',b,3)[0];p=b[5:];counts[(d,op)]+=1
   if plen!=len(p):stats['frame_length_mismatch']+=1
   stamp={'offset':off,'t_us':t,'wall':clock(base+(t-raw_first)/1e6),'dir':d,'op':op,'session':session,'thread':thread}
   if op==2005:
    at=0
    while at+2<=len(p):
     z=struct.unpack_from('<H',p,at)[0]
     if z==0 or at+2+z>len(p):stats['bad_2005']+=1;break
     r=p[at+2:at+2+z]
     if len(r)>=34:
      nn=u32(r,30);end=34+2*nn;name=r[34:end].decode('utf-16-le',errors='replace') if end<=len(r) else '?'
      a=dict(stamp,record_at=at+2,type=r[0],key=hx(u32(r,1)),controller=hx(u32(r,18)),name=name,record_hex=r.hex())
      a['parent'],a['children']=fam(r)
      if end+100<=len(r):a.update(requester=hx(u32(r,end+88)),road_sub=hx(u32(r,end+92)),road=hx(u32(r,end+96)),after_name=end)
      actors.append(a)
     at+=2+z
   if op in selected:events.append(dict(stamp,payload_hex=p.hex(),keys=[{'at':i,'key':hx(u32(p,i))} for i in range(len(p)-3) if u32(p,i)>>28 in(10,11) and (u32(p,i)&0xfff00000) in(0xa0100000,0xb0100000)]))
   if op in(2223,2991,2556,2697,2734,2405,2291,2828) and len(p)>=4:
    k=hx(u32(p));actions[k][str(d)+'/'+str(op)]+=1
    if (k,d,op) not in firsts:firsts[(k,d,op)]=dict(stamp,payload_hex=p.hex())
   if op==2516 and len(p)>=6:
    for i in range(6,len(p)-3,4):events.append(dict(stamp,key=hx(u32(p,i)),reason=p[5],payload_hex=p.hex()))
 result={'label':label,'capture':bin,'log':log,'version':version,'pe':hex(pe),'base_wall_seconds':base,'raw_first_us':raw_first,'anchor_count':len(anchors),'anchor_range':max(anchors)-min(anchors),'marks':marks,'stats':dict(stats),'counts':{str(d)+'/'+str(op):n for (d,op),n in counts.items()},'actors':actors,'events':events,'actions':dict(actions),'firsts':{str(k)+'/'+str(d)+'/'+str(op):v for (k,d,op),v in firsts.items()}}
 (ROOT/(label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
 print(label,'base',clock(base),'stats',dict(stats),'actors',len(actors),'events',len(events),'attacks',counts[(2,2922)],'crime',counts[(1,2422)],counts[(2,2740)],'dead',counts[(2,2530)],flush=True)
