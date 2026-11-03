import json, sys, struct
seen=set()
for line in open('caps.jsonl'):
    r=json.loads(line)
    if r['op']!=2661 or r['d']!=2: continue
    if 's43-activo' not in r['f']: continue
    p=bytes.fromhex(r['p'])
    seq=struct.unpack_from('<Q',p,9)[0]
    print('%8.3f size %d seq 0x%X' % (r['t'], len(p), seq))
    pd=p[5:91]
    print('  kind %02x actor %08X | pd:' % (p[0], struct.unpack_from('<I',p,1)[0]), ' '.join('%02x'%b for b in pd))
    for i in range(91, len(p), 11):
        b=p[i:i+11]; print('    bind role %08X actor %08X t %d %d %d' % (struct.unpack_from('<I',b,0)[0], struct.unpack_from('<I',b,4)[0], b[8], b[9], b[10]))
