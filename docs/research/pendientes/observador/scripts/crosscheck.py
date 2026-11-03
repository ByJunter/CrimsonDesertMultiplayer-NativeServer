# Cross-check: NPCs whose restore failed on the guest (its cdmp.log) vs the node->actor bindings
# of the PrepareSequencerAck 2661 the host's own client received (host capture), and the opcodes
# the guest received. Read only; python -I.
import sys,os,re,struct; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cap import read,names
host_bin, guest_bin, guest_log = sys.argv[1:4]
failed=sorted(set(int(m.group(1),16) for line in open(guest_log,encoding='utf8',errors='replace')
                  if 'RestartLayerAction 0x863880 FAILED' in line for m in [re.search(r'actor key (0x[0-9A-F]+)',line)] if m))
bound={}
for n,t,d,op,size,data,sess,tid in read(host_bin):
    if op==2661 and d==2:
        p=data[5:]; seq=struct.unpack_from('<Q',p,9)[0]
        for i in range((len(p)-91)//11):
            node,actor=struct.unpack_from('<II',p,91+11*i)
            bound.setdefault(actor,[]).append((seq,node,round(t,3)))
print(f'guest failed restores: {len(failed)}')
for k in failed:
    print(f'  0x{k:08X} -> ' + (', '.join(f'seq 0x{s:X} node 0x{nd:08X} (host t={tt})' for s,nd,tt in bound[k]) if k in bound else 'NOT BOUND in any host 2661'))
print('bound in host 2661 but no failure line on the guest:', ' '.join(f'0x{k:08X}' for k in sorted(bound) if k not in failed and (k>>28)==0xB))
cnt={}
for n,t,d,op,size,data,sess,tid in read(guest_bin):
    if d==2 and op in (2661,2810,2322,2981,2524,2111,2724,2264): cnt[op]=cnt.get(op,0)+1
print('guest S>C counts of stage/sequencer opcodes:', {f'{k} {names[k]}':v for k,v in sorted(cnt.items())})
