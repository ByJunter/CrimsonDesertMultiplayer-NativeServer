import json,sys
d=json.load(open('rtti.json'))
cls=d['classes']
vt={}
for m,c in cls.items():
    for v in c['vtables']:
        vt[int(v['rva'],16)]=(c['name'],v['offset'],v['length'])
json.dump({hex(k):v for k,v in vt.items()},open('vtmap.json','w'))
for k,(n,o,l) in sorted(vt.items()):
    if any(t in n for t in sys.argv[1:]): print(hex(k),n,o,l)
