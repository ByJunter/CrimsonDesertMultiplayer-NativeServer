# scan ranges for stores to [reg+disp] with given disp (qword), report instruction + containing func
import sys, re
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe
pe = cdpe.PE(); B=cdpe.BASE
disp = int(sys.argv[1],16)
ranges = [(int(a.split('-')[0],16), int(a.split('-')[1],16)) for a in sys.argv[2:]]
pat = re.compile(rb"[\x48\x4c\x49\x4d]\x89[\x80-\xbf]" + disp.to_bytes(4,'little'), re.S)
pat2 = re.compile(rb"[\x48\x4c\x49\x4d]\x89[\x80-\xbf]\x24" + disp.to_bytes(4,'little'), re.S)
for lo,hi in ranges:
    code = pe.read(lo, hi-lo)
    for p in (pat,pat2):
        for m in p.finditer(code):
            src = lo+m.start()
            ins = next(pe.md.disasm(pe.read(src,16), B+src), None)
            if ins and ins.mnemonic=='mov' and hex(disp) in ins.op_str and ins.op_str.startswith('qword ptr ['):
                f = cdpe.func_of(pe, src)
                print(f"{src:#x} {ins.mnemonic} {ins.op_str}  in {f:#x}" if f else f"{src:#x} {ins.op_str} nofunc")
