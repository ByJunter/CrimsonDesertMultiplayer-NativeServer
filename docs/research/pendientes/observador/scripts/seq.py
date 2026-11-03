# PrepareSequencerAck 2661 / DeleteSequencerAck 2810 of a capture: addressed actor, owner, sequencer
# id and the node -> actor bindings (2661 payload: u8 kind | u32 actor | 86-byte play data (u32 owner,
# u64 seq, ...) | n x (u32 node, u32 actor, 3 x u8); 2810 payload: u32 actor | u64 seq | u32 | u8).
import sys,os,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
for n,t,d,op,size,data,sess,tid in read(sys.argv[1]):
    p=data[5:]
    if op==2661:
        actor,owner=struct.unpack_from('<II',p,1); seq=struct.unpack_from('<Q',p,9)[0]
        nb=(len(p)-91)//11; rem=(len(p)-91)%11
        nodes=[struct.unpack_from('<II',p,91+11*i) for i in range(nb)]
        print(f"#{n} t={t:.3f} 2661 size {size} (5+91+11x{nb}{' REM '+str(rem) if rem else ''}) actor {actor:08X} owner {owner:08X} seq {seq:#x} bindings {[(hex(a),hex(b)) for a,b in nodes]}")
    elif op==2810:
        actor=struct.unpack_from('<I',p,0)[0]; seq=struct.unpack_from('<Q',p,4)[0]
        print(f"#{n} t={t:.3f} 2810 size {size} actor {actor:08X} seq {seq:#x} tail {p[12:].hex()}")
