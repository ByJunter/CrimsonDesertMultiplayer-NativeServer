import pe, re
C=[pe.u64(0x558C080+8*i)-pe.BASE for i in range(105)]
K=[pe.u64(0x590DF78+8*i)-pe.BASE for i in range(105)]
S=[pe.u64(0x5B1FE10+8*i)-pe.BASE for i in range(105)]
NAMED={0x2049B90:'bind2049B90',0x205DC30:'resolve205DC30',0x205E520:'byNode205E520',0x8B44E0:'GetLocalPlayer',0x15A77B0:'stage15A77B0',0x8AE1A0:'actorMgr8AE1A0',0x8AE940:'FindActor8AE940',0x204F730:'gate204F730'}
def body(a):
    f=pe.func_of(a)
    if not f:
        ins=pe.dis(a,a+16)
        if ins and ins[0].mnemonic=='jmp' and ins[0].op_str.startswith('0x'):
            t=int(ins[0].op_str,16)-pe.BASE
            return ('thunk',t)
        return ('?',None)
    return ('fn',f)
def scan(a,depth,seen):
    out=set()
    if a in seen: return out
    seen.add(a)
    k,f=body(a)
    if k=='thunk':
        if pe.sec(f)!='.rsrc': return {'->xtls'}
        return scan(f,depth,seen)
    if k!='fn': return {'?'}
    ins=pe.dis(f[0],f[1])
    cwreg=set()
    for i,x in enumerate(ins):
        m=re.search(r'\[rip \+ (0x[0-9a-f]+)\]',x.op_str)
        if m:
            t=x.address-pe.BASE+x.size+int(m.group(1),16)
            if t==0x6D691E0 and x.mnemonic=='mov':
                r=x.op_str.split(',')[0].strip()
                # next use of r with offset
                for y in ins[i+1:i+6]:
                    mm=re.search(r'\[%s \+ (0x[0-9a-f]+)\]'%r,y.op_str)
                    if mm: out.add('cw+%s'%mm.group(1)+('' if depth==0 else '(d)')); break
                else: out.add('cw'+('' if depth==0 else '(d)'))
        if x.mnemonic in('call','jmp') and x.op_str.startswith('0x'):
            t=int(x.op_str,16)-pe.BASE
            if t in NAMED: out.add(NAMED[t]+('' if depth==0 else '(d)'))
            elif pe.sec(t)!='.rsrc': out.add('xtls-call'+('' if depth==0 else '(d)'))
            elif depth<1 and x.mnemonic=='call': out|=scan(t,depth+1,seen)
    return out
for k in range(0x108//8,0x2A8//8):
    if C[k]==0x359B50: continue
    print('+0x%03X %07X K=%07X S=%07X'%(8*k,C[k],K[k],S[k]),sorted(scan(C[k],0,set())))
