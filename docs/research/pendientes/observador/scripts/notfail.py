import sys,os,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
keys=[int(x,16) for x in sys.argv[2].split(',')]
seen={k:[] for k in keys}
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if d==2 and op==2005:
        for k in keys:
            kb=struct.pack('<I',k)
            i=data.find(kb)
            if i>=0: seen[k].append((n,round(t,3)))
for k,v in seen.items(): print(f'0x{k:08X}: 2005 records {len(v)} first {v[:2]}')
