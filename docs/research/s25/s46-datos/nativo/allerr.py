import sys, struct, re, json
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe
pe = cdpe.PE(); B = cdpe.BASE
regs = [(0x21FA720, 0x2209C20), (0x2209C20, 0x2220000)]
def s(r):
    b = pe.read(r, 200)
    if b is None: return None
    return b.split(b'\0')[0].decode('latin-1')
out = {}
for lo, hi in regs:
    code = pe.read(lo, hi - lo)
    for m in re.finditer(rb"\x48\x8d\x0d", code):
        o = m.start(); src = lo + o
        var = src + 7 + struct.unpack_from("<i", code, o + 3)[0]
        if not (0x6C00000 <= var < 0x6E00000): continue
        ins = list(pe.md.disasm(pe.read(src, 60), B + src))[:6]
        for i in ins:
            mm = re.search(r"\[rip \+ 0x([0-9a-f]+)\]", i.op_str)
            if i.mnemonic == 'lea' and mm and i.op_str[:2] in ('r8', 'rd'):
                t = i.address + i.size + int(mm.group(1), 16) - B
                n = s(t)
                if n and n.startswith('eErrNo'): out[var] = n
print(len(out))
json.dump({hex(k): v for k, v in sorted(out.items())}, open(sys.argv[1], 'w'), indent=0)
