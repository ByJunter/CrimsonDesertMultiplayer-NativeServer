import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
ops={int(x) for x in sys.argv[2].split(',')}; t0=float(sys.argv[3]); t1=float(sys.argv[4]); mx=int(sys.argv[5]) if len(sys.argv)>5 else 64
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if t<t0: continue
    if t>t1: break
    if op in ops:
        print(f"#{n} t={t:.6f} {op} {names.get(op)} size={size}: {data[5:5+mx].hex(' ')}")
