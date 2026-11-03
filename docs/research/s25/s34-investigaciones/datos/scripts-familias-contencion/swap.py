import re,sys
G=sys.argv[1]; OFF=-68241.56537
def hq(q): return float(q)-OFF
pts=[]
for ln in open(G,encoding='utf-8',errors='replace'):
    m=re.search(r'NpcProbe \[invitado (alta|baja)\] qpc ([\d.]+) s .*?clave (0x[0-9A-F]+)(.*)$',ln)
    if m:
        t=hq(m.group(2))
        if 68640<t<68650 or 68698<t<68725: pts.append((t,m.group(1),m.group(3),(re.search(r'motivo (\d+)',m.group(4)) or re.search(r'controlador (0x[0-9A-F]+)',m.group(4))).group(1)))
    m=re.search(r'GuestSchedRoute \[(2126|2335)[^\]]*\] qpc ([\d.]+) s .*?scene (0x[0-9A-F]+) of character (0x[0-9A-F]+)',ln)
    if m:
        t=hq(m.group(2))
        if 68640<t<68650 or 68698<t<68725: pts.append((t,m.group(1),m.group(3),m.group(4)))
pts.sort()
import itertools
def grp(lo,hi):
    sel=[p for p in pts if lo<=p[0]<hi]
    out=[]
    for (k),g in itertools.groupby(sel,key=lambda p:(p[1],p[3])):
        g=list(g); out.append((g[0][0],g[-1][0],k,len(g)))
    return out
for a,b,k,n in grp(68646.5,68648.5): print(f'{a:.3f}-{b:.3f} {k} x{n}')
print('---')
for a,b,k,n in grp(68700.5,68702): print(f'{a:.3f}-{b:.3f} {k} x{n}')
print('---')
for a,b,k,n in grp(68721.5,68723.5): print(f'{a:.3f}-{b:.3f} {k} x{n}')
