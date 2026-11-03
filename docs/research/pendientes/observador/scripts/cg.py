import sys,os,re,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
def calls_in(f):
    s,e,_=D.func_of(f)
    out=[]
    for ins in D.md.disasm(D.rd(s,e-s),0x140000000+s):
        if ins.mnemonic in ('call','jmp') and ins.op_str.startswith('0x'):
            t=int(ins.op_str,16)-0x140000000
            sc=D.sec(t)
            if ins.mnemonic=='jmp':
                fs=D.func_of(t)
                if fs and fs[0]==s: continue
            out.append((ins.address-0x140000000,ins.mnemonic,t,sc[0] if sc else '?'))
    return s,e,out
# BFS from roots to find path to targets
roots=[int(x,16) for x in sys.argv[1].split(',')]; targets={int(x,16) for x in sys.argv[2].split(',')}
depth=int(sys.argv[3]) if len(sys.argv)>3 else 4
from collections import deque
seen={}; q=deque([(r,0,[r]) for r in roots])
while q:
    f,d,path=q.popleft()
    fo=D.func_of(f)
    if not fo: continue
    if fo[0] in seen: continue
    seen[fo[0]]=path
    if d>=depth: continue
    s,e,cs=calls_in(fo[0])
    for a,m,t,sc in cs:
        if t in targets:
            print('PATH', ' -> '.join(f'{p:X}' for p in path), f'-> [{a:X}] {t:X}')
        if sc=='.rsrc': q.append((t,d+1,path+[t]))
print('visited',len(seen))
