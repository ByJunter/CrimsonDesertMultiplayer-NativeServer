import sys,re
lines=open(sys.argv[1],encoding='utf8',errors='replace').read().splitlines()
names=sys.argv[2].split(',')
# index of function labels
labels=[(i,l[:-1]) for i,l in enumerate(lines) if l.endswith(':') and not l.startswith(' ')]
for nm in names:
    hit=[(i,l) for i,l in labels if l.endswith("::"+nm) and 'session2' in l]
    for i,l in hit:
        j=next((k for k,_ in labels if k>i),len(lines))
        body=lines[i+1:j]
        calls=set(); vex=[]; rep=[]
        for b in body:
            m=re.search(r'\s(call|jmp)\s+(.*)$',b)
            if m and ('call' in m.group(1) or '?' in m.group(2) or '::' in m.group(2)): calls.add(m.group(2).strip())
            parts=b.split()
            mn=[p for p in parts if re.match(r'^[a-z]{2,}[a-z0-9]*$',p)]
            if re.search(r'\s(v[a-z0-9]+)\s',b): vex.append(b.strip())
            if 'rep ' in b or 'stos' in b or 'movs' in b and 'movsx' not in b and 'movsd' not in b: rep.append(b.strip())
        print(f"== {l}  ({len(body)} lines)")
        for c in sorted(calls): print("   call", c)
        for v in vex[:5]: print("   VEX?", v)
        for r in rep[:5]: print("   REP?", r)
