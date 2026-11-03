import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
want={int(x) for x in sys.argv[2].split(',')}
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    if n in want:
        print(f"#{n} op {op} size {size} copied {len(data)}: {data.hex()}")
    if n>max(want): break
