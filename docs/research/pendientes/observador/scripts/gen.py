import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
out=[]
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if op in (2661,2810,2322) and d==2:
        out.append((n,t,op,data.hex()))
for n,t,op,h in out:
    # split hex into 100-char chunks
    parts=[h[i:i+96] for i in range(0,len(h),96)]
    s='\n         '.join(f'"{p}"' for p in parts)
    print(f'    {{{n}, {t:.6f}, {op},\n         {s}}},')
