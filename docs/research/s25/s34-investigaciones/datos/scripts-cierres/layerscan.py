import re, struct, sys
d = open(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe', 'rb').read()
pe = struct.unpack_from('<I', d, 0x3C)[0]; n = struct.unpack_from('<H', d, pe + 6)[0]; osz = struct.unpack_from('<H', d, pe + 20)[0]
pat = re.compile(rb'[\x48-\x4F]\x8B[\x84\x8C\x94\x9C\xA4\xAC\xB4\xBC][\xC0-\xFF]\x88\x00\x00\x00', re.S)
tot = 0
for i in range(n):
    o = pe + 24 + osz + 40 * i
    name = d[o:o+8].rstrip(b'\0').decode(); vsz, va, rsz, raw, _, _, _, _, ch = struct.unpack_from('<IIIIIIHHI', d, o + 8)
    if not ch & 0x20000000: continue
    hits = [va + m.start() - 0 for m in pat.finditer(d, raw, raw + rsz)]
    hits = [h - raw + 0 for h in hits]
    print(name, hex(va), 'mov r64,[r+r*8+0x88] sites:', len(hits))
    tot += len(hits)
print('total', tot)
