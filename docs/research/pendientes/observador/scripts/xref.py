import sys,os,re,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
def funcstart(rva):
    # walk back to a CC/C3 boundary followed by code (padding run >=1 CC)
    r=rva
    while r>0:
        b=D.rd(r-1,1)[0]
        if b==0xCC:
            return r
        r-=1
def callers(target, secnames=('.rsrc',)):
    res=[]
    for name,va,vs,ro,rs in D.secs:
        if name not in secnames: continue
        data=D.mm[ro:ro+rs]
        for m in re.finditer(b'[\xE8\xE9]',data):
            o=m.start()
            if o+5>len(data): continue
            rel=struct.unpack_from('<i',data,o+1)[0]
            if va+o+5+rel==target: res.append((name,va+o,data[o]))
    return res
if __name__=='__main__':
    for a in sys.argv[1:]:
        r=int(a,16); fs=funcstart(r)
        print(f"{r:08X} in func {fs:08X}")
        for c in callers(fs, ('.rsrc','.xtls')): 
            cf=funcstart(c[1]) if c[0]=='.rsrc' else None
            print(f"   {'call' if c[2]==0xE8 else 'jmp '} from {c[0]} {c[1]:08X} (func {cf:08X})" if cf else f"   from {c[0]} {c[1]:08X}")
