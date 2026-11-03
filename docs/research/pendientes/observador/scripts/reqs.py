import sys,os,struct,collections; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read
c=collections.Counter()
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if d==1 and op in (2767,2436,2591) and c[op]<3:
        c[op]+=1; print(op, f"t={t:.3f}", size, data[5:].hex())
    if d==2 and op in (2111,2524) and c[op]<2:
        c[op]+=1; print(op, f"t={t:.3f}", size, data[5:].hex())
    if t>100: break
