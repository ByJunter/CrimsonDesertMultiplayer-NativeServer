import sys,re
lines=open(sys.argv[1],encoding='utf8',errors='replace').read().splitlines()
lo,hi=int(sys.argv[2],16),int(sys.argv[3],16)
addr_re=re.compile(r'^\s+([0-9A-F]{16}):')
label=None; cur=[]
out={}
for l in lines:
    if l.endswith(':') and not l.startswith(' '): label=l; continue
    m=addr_re.match(l)
    if not m: continue
    a=int(m.group(1),16)
    if lo<=a<hi:
        out.setdefault(label,[]).append(l)
for lab,body in out.items():
    calls=[b.strip() for b in body if re.search(r'\scall\s',b)]
    vex=[b.strip() for b in body if re.search(r'\s(v[a-z]+[a-z0-9]*)\s',b)]
    rep=[b.strip() for b in body if re.search(r'\srep\s|stos[bdqw]?\b|movs[bdqw]\b',b)]
    print(lab, len(body), 'calls', len(calls), 'vex', len(vex), 'rep', len(rep))
    for c in calls: print('    ',c[-90:])
    for v in vex[:3]: print('   VEX',v)
