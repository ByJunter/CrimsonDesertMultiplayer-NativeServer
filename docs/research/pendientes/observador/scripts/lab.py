import sys,re
lines=open(sys.argv[1],encoding='utf8',errors='replace').read().splitlines()
want=[int(x,16) for x in sys.argv[2].split(',')]
label=None; found={}
addr_re=re.compile(r'^\s+([0-9A-F]{16}):')
for l in lines:
    if l.endswith(':') and not l.startswith(' '): label=l; continue
    m=addr_re.match(l)
    if m:
        a=int(m.group(1),16)
        if a in want and a not in found: found[a]=label
for a in want: print(hex(a), found.get(a))
