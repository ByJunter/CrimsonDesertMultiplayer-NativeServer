import pe, struct
d=pe._d
for name,v in (('vtable 0x558C080',0x558C080),('COL 0x5DB7A20',0x5DB7A20),('TD 0x6AACB00',0x6AACB00)):
    nd=struct.pack('<I',v); i=d.find(nd); hits=[]
    while i!=-1:
        for n,va,vs,ro,rs in pe.SECS:
            if ro<=i<ro+rs: hits.append((n,hex(va+i-ro)))
        i=d.find(nd,i+1)
    print(name,'RVA32 hits:',hits)
    # rip-relative lea/mov to TD
    import re
    if v!=0x558C080:
        pat=re.compile(rb'[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]')
        for n,va,vs,ro,rs in pe.SECS:
            if n not in('.rsrc','.xtls','.text','.xtext'): continue
            blob=d[ro:ro+rs]
            for m in pat.finditer(blob):
                p=m.start()
                if p+7>len(blob): continue
                if va+p+7+struct.unpack_from('<i',blob,p+3)[0]==v: print('  lea',n,hex(va+p))
