import sys,os,struct,collections; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
S2C={2264,2661,2322,2981,2810,2195,2165,2335,2724,2524,2111,2144,2549}
C2S={2436,2767,2873,2276,2173,2170,2113,2591,2704,2105,2138,2197,2241,2547,2549}
for path in sys.argv[1:]:
    cnt=collections.Counter(); firsts={}
    for n,t,d,op,size,data,sess,tid in read(path):
        if (d==2 and op in S2C) or (d==1 and op in C2S):
            k=(d,op); cnt[k]+=1
            firsts.setdefault(k,[]).append(round(t,3)) if len(firsts.get(k,[]))<6 else None
    print('==',path)
    for (d,op),c in sorted(cnt.items()):
        print(f"  {'S>C' if d==2 else 'C>S'} {op} {names.get(op,'?')}: {c}  first {firsts[(d,op)]}")
