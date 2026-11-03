import pe, re, struct, sys
tg=[int(x,16) for x in sys.argv[1:]]
d=pe._d
pat=re.compile(rb'[\xE8\xE9]',re.S)
for n,va,vs,ro,rs in pe.SECS:
    if n not in('.rsrc','.xtls','.text','.xtext'): continue
    blob=d[ro:ro+rs]
    for t in tg:
        # search for rel32 values: for each position p with E8/E9, check; faster: compute needed disp per position impossible; brute via finditer
        pass
    for m in pat.finditer(blob):
        p=m.start()
        if p+5>len(blob): break
        disp=struct.unpack_from('<i',blob,p+1)[0]
        r=va+p+5+disp
        if r in tg:
            f=pe.func_of(va+p)
            print(n,hex(va+p),'E8' if blob[p]==0xE8 else 'E9','->',hex(r),f and (hex(f[0]),hex(f[1])))
