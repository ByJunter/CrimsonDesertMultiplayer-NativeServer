import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
import collections
bounds=[float(x) for x in sys.argv[2].split(',')]
win=[(bounds[i],bounds[i+1]) for i in range(len(bounds)-1)]
c=[collections.Counter() for w in win]
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if t>bounds[-1]: break
    for i,(a,b) in enumerate(win):
        if a<=t<b and d==2: c[i][op]+=1
for (a,b),v in zip(win,c):
    print(f'== [{a},{b}) S>C total {sum(v.values())}')
    print(', '.join(f"{op} {names.get(op)} x{n}" for op,n in v.most_common()))
