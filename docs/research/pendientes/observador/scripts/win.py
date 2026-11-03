import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read
lo,hi=float(sys.argv[2]),float(sys.argv[3])
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if t<lo: continue
    if t>hi: break
    if d==2 and op in (2981,2524,2111,2810,2661,2322,2144):
        print(f"#{n} t={t:.3f} {op} size {size} {data[5:].hex()}")
