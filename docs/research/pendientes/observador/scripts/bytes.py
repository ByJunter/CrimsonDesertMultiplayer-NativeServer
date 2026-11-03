import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
for a in sys.argv[1:]:
    r,n=a.split(':'); r=int(r,16); n=int(n)
    b=D.rd(r,n)
    print(f"0x{r:X} ({n}): {{" + ", ".join(f"0x{x:02X}" for x in b) + "}")
    for l in D.dis(r,n,stop_ret=False): print("    //",l)
