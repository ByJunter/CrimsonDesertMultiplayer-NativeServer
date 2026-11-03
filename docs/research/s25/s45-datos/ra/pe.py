import struct, sys, bisect
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
PE_PATH = r"D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe"
BASE = 0x140000000
_d = open(PE_PATH, 'rb').read()
_pe = struct.unpack_from('<I', _d, 0x3C)[0]
_nsec = struct.unpack_from('<H', _d, _pe + 6)[0]
_opt = struct.unpack_from('<H', _d, _pe + 20)[0]
_sh = _pe + 24 + _opt
SECS = []
for i in range(_nsec):
    name, vs, va, rs, ro = struct.unpack_from('<8sIIII', _d, _sh + 40 * i)
    SECS.append((name.rstrip(b'\0').decode(), va, vs, ro, rs))
_ddir = _pe + 24 + 112
pdata_rva, pdata_sz = struct.unpack_from('<II', _d, _ddir + 8 * 3)
def off(rva):
    for n, va, vs, ro, rs in SECS:
        if va <= rva < va + max(vs, rs):
            return ro + rva - va
    return None
def sec(rva):
    for n, va, vs, ro, rs in SECS:
        if va <= rva < va + max(vs, rs): return n
def rd(rva, n):
    o = off(rva); return _d[o:o + n]
def u32(rva): return struct.unpack('<I', rd(rva, 4))[0]
def u64(rva): return struct.unpack('<Q', rd(rva, 8))[0]
FUNCS = []
_o = off(pdata_rva)
for i in range(pdata_sz // 12):
    b, e, u = struct.unpack_from('<III', _d, _o + 12 * i)
    if b: FUNCS.append((b, e))
FUNCS.sort()
_starts = [f[0] for f in FUNCS]
def func_of(rva):
    i = bisect.bisect_right(_starts, rva) - 1
    if i >= 0 and FUNCS[i][0] <= rva < FUNCS[i][1]: return FUNCS[i]
    return None
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = False
def dis(start, end):
    code = rd(start, end - start)
    return list(md.disasm(code, BASE + start))
def pr(start, end, out=sys.stdout):
    for i in dis(start, end):
        print(f"{i.address - BASE:#09x}: {i.bytes.hex():<24} {i.mnemonic} {i.op_str}", file=out)
def prf(rva):
    f = func_of(rva); pr(f[0], f[1]); return f
if __name__ == '__main__':
    a = int(sys.argv[1], 16)
    if len(sys.argv) > 2: pr(a, int(sys.argv[2], 16))
    else:
        f = func_of(a); print('func', hex(f[0]), hex(f[1]), sec(a)); pr(f[0], f[1])
