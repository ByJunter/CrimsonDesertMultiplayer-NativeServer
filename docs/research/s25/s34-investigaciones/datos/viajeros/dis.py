import sys, struct
import capstone
exe = r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe'
data = open(exe, 'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
ts = struct.unpack_from('<I', data, pe + 8)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]; opt = struct.unpack_from('<H', data, pe + 20)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + 40 * i
    name = data[o:o+8].rstrip(b'\0').decode(); vs, va, rs, ra = struct.unpack_from('<IIII', data, o + 8)
    secs.append((name, va, vs, ra, rs))
def off(rva):
    for n, va, vs, ra, rs in secs:
        if va <= rva < va + max(vs, rs): return ra + rva - va, n
print('timestamp', hex(ts))
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
for a in sys.argv[1:]:
    lo, hi = [int(x, 16) for x in a.split('-')]
    o, n = off(lo)
    print('--', n, hex(lo))
    for i in md.disasm(data[o:o + (hi - lo)], 0x140000000 + lo):
        print(hex(i.address - 0x140000000), i.mnemonic, i.op_str)
