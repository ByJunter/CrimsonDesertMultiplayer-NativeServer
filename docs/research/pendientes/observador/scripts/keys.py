import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sys
from cap import read,names
KEY={3093,2549,2144,2322,2873,2661,2810,3066,2445,2762,2027,2043,2174,2437,2490,2468,2879,2724,2264,2981,2140,2111,2226,2736,2524,2797,2452,2675,2266,2253,2411}
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if op in KEY:
        print(f"#{n} t={t:.6f} {'C>S' if d==1 else 'S>C'} {op} {names.get(op)} size={size} {data[5:37].hex()}")
