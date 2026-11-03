import pe
C=0x558C080;K=0x590DF78;S=0x5B1FE10
for n,va,vs,ro,rs in pe.SECS: print(n,hex(va),hex(vs))
print('ret stub', pe.rd(0x359B50,8).hex())
for v in (C,K,S):
    print(hex(v),'col',hex(pe.u64(v-8)-pe.BASE),'slot105',hex(pe.u64(v+8*105)),'slot104',hex(pe.u64(v+8*104)-pe.BASE))
c=[pe.u64(C+8*k)-pe.BASE for k in range(105)]
k_=[pe.u64(K+8*k)-pe.BASE for k in range(105)]
s=[pe.u64(S+8*k)-pe.BASE for k in range(105)]
print('C=['+','.join('0x%X'%x for x in c)+']')
print('K=['+','.join('0x%X'%x for x in k_)+']')
print('S=['+','.join('0x%X'%x for x in s)+']')
for i in range(105):
    o=8*i
    if 0x100<=o<0x2A8 and (c[i]!=0x359B50 or k_[i]!=0x359B50 or s[i]!=0x359B50):
        print('+0x%03X C %07X K %07X S %07X'%(o,c[i],k_[i],s[i]))
