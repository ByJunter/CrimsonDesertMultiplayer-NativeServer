"""Validate complete current-build movement/field packets in a native capture."""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import struct

REC=struct.Struct('<QBBHHHII')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('capture',type=Path)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    data=args.capture.read_bytes()
    assert struct.unpack_from('<4sII',data)==(b'CDPK',2,0x6AEA07A6)
    mapping=json.loads((Path(__file__).parent/'data/opcode_mapping_2976_3527.json').read_text())
    names={x['new']:x['name'] for x in mapping['opcode_mapping']}
    for x in mapping['added_opcodes']:
        names[x['new']]=x['name']
    pos=12
    sizes=defaultdict(Counter)
    errors=[]
    moves=[]
    player_moves={}
    actor_tails=[]
    while pos+REC.size<=len(data):
        t,d,_,size,copied,_,sess,thread=REC.unpack_from(data,pos)
        if pos+REC.size+copied>len(data): break
        pkt=data[pos+REC.size:pos+REC.size+copied]
        pos+=REC.size+copied
        if len(pkt)<5: continue
        op,slot,length=struct.unpack_from('<HbH',pkt)
        name=names.get(op,f'unknown_{op}')
        payload=pkt[5:]
        sizes[name][len(payload)]+=1
        if length!=len(payload): continue # Internal connection/logout forms are not ordinary Trocs.
        expected={'MoveActorReq':518,'MoveActorAck':239,'StartActionReq':729,'StartActionAck':729}.get(name)
        if expected and len(payload)!=expected: errors.append(f'{name}: length {len(payload)} != {expected}')
        if name in ('MoveFieldAck','LoadingFieldDataAck'):
            prefix=17 if name=='MoveFieldAck' else 20
            if len(payload)<prefix or (len(payload)-prefix)%56: errors.append(f'{name}: invalid field records {len(payload)}')
        if name in ('MoveActorReq','MoveActorAck') and len(payload)>=226:
            xyz=struct.unpack_from('<3f',payload,0xC4)
            quat=struct.unpack_from('<4f',payload,0xB4)
            if not all(math.isfinite(x) for x in xyz+quat): errors.append(f'{name}: nonfinite transform')
            key=struct.unpack_from('<I',payload)[0]
            if key >> 28 == 0xA:
                entry=player_moves.setdefault(hex(key),dict(count=0,first_position=xyz,last_position=xyz,
                    min_position=list(xyz),max_position=list(xyz)))
                entry['count']+=1
                entry['last_position']=xyz
                entry['last_rotation']=quat
                entry['min_position']=[min(a,b) for a,b in zip(entry['min_position'],xyz)]
                entry['max_position']=[max(a,b) for a,b in zip(entry['max_position'],xyz)]
            if len(moves)<8: moves.append(dict(name=name,key=hex(struct.unpack_from('<I',payload)[0]),position=xyz,rotation=quat))
        if name=='ActorLoadDataAck':
            off=0
            while off+2<=len(payload):
                n=struct.unpack_from('<H',payload,off)[0]
                off+=2
                if not n or off+n>len(payload): break
                record=payload[off:off+n]
                if n>=158 and record[0]==1:
                    actor_tails.append(dict(key=hex(struct.unpack_from('<I',record,1)[0]),record_size=n,
                        position_end158=struct.unpack_from('<3f',record,n-158),
                        rotation_end146=struct.unpack_from('<4f',record,n-146),
                        position_end154=struct.unpack_from('<3f',record,n-154),
                        rotation_end142=struct.unpack_from('<4f',record,n-142)))
                off+=n
    report=dict(capture=str(args.capture),parsed_bytes=pos,pending_tail_bytes=len(data)-pos,
                sizes={k:dict(v) for k,v in sizes.items()},errors=errors,movement_samples=moves,
                player_movement=player_moves,actor_tail_candidates=actor_tails)
    args.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(errors=errors,packet_types=len(sizes),player_movement=player_moves,
        relevant_sizes={k:dict(v) for k,v in sizes.items() if k in ('MoveActorReq','MoveActorAck','StartActionReq','StartActionAck','MoveFieldAck','LoadingFieldDataAck')}),indent=2))
    return 1 if errors else 0

if __name__=='__main__': raise SystemExit(main())
