import pe, re, struct, sys
targets={0x558C080:'client',0x590DF78:'common',0x5B1FE10:'server',0x558BD80:'ClientSequencerStage',0x558BFB0:'ClientSequencerStage(2)'}
d=pe._d
pat=re.compile(rb'[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]',re.S)
for n,va,vs,ro,rs in pe.SECS:
    if n not in('.rsrc','.xtls','.text','.xtext'): continue
    blob=d[ro:ro+rs]
    for m in pat.finditer(blob):
        p=m.start()
        if p+7>len(blob): continue
        disp=struct.unpack_from('<i',blob,p+3)[0]
        rva=va+p+7+disp
        if rva in targets:
            print(n,hex(va+p),targets[rva],pe.func_of(va+p) and tuple(hex(x) for x in pe.func_of(va+p)))
# absolute 64-bit pointers
for t,name in targets.items():
    needle=struct.pack('<Q',pe.BASE+t)
    i=d.find(needle)
    while i!=-1:
        # map file offset to rva
        for n,va,vs,ro,rs in pe.SECS:
            if ro<=i<ro+rs: print('abs',name,n,hex(va+i-ro))
        i=d.find(needle,i+1)
