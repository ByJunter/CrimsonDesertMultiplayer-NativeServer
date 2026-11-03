# Read-only: timeline of sequencer packets with names.
import sys, json, struct, os
S = os.path.dirname(os.path.abspath(__file__))
NAMES = os.path.join(S, '..', '..', 's44', 'inventario', 'names.json')
OPN = {2661:'2661 Prepare',2981:'2981 Branch',2524:'2524 OnBranchEv',2810:'2810 Delete',2322:'2322 PlayAllPreLoad',2475:'2475 ReviveAck',
       2993:'2993 ReviveLoadingAck',2643:'2643 LoadingFieldData',2344:'2344 ChangeGameLevelState',2295:'2295 ChangeWantedState',2543:'2543 ClearWanted',
       2958:'2958 NPCSchedMiseenscene',2395:'2395 AutoSpawnCloseFilter',2909:'2909 DeactMiseensceneEvade',2314:'2314 EvadeProjectileStage',3009:'3009 MissionList'}
def rows(fn):
    return [json.loads(l) for l in open(fn)]
cache_fn = os.path.join(S, 'names_cache.json')
def names_for(hs):
    cache = json.load(open(cache_fn)) if os.path.exists(cache_fn) else {}
    miss = [h for h in hs if h not in cache]
    if miss:
        alln = json.load(open(NAMES))
        for h in miss:
            e = alln.get(h)
            if e:
                bases = sorted(e.keys())
                seqb = [b for b in bases if any('sequencer/' in p for p in e[b])]
                b = (seqb or bases)[0]
                cache[h] = b + ('' if len(bases)==1 else ' (+%d)'%(len(bases)-1)) + ' | ' + e[b][0]
            else:
                cache[h] = '?'
        json.dump(cache, open(cache_fn,'w'))
    return cache
def main():
    allr = {k: rows(os.path.join(S, k + '_ops.jsonl')) for k in ('g','h')}
    hs = set()
    for k, rr in allr.items():
        for r in rr:
            p = bytes.fromhex(r['p'])
            if r['op']==2661 and len(p)>=31: hs.add('%08X'%struct.unpack_from('<I',p,27)[0])
    nm = names_for(sorted(hs))
    off = {'g': float(sys.argv[1]), 'h': float(sys.argv[2])}  # wall seconds-of-day = t + off
    for k, rr in allr.items():
        with open(os.path.join(S, k + '_timeline.txt'), 'w') as o:
            for r in rr:
                p = bytes.fromhex(r['p']); op = r['op']; t = r['t']
                w = t + off[k]; hh=int(w//3600); mi=int(w%3600//60); se=w%60
                ws = '%02d:%02d:%06.3f' % (hh, mi, se)
                dr = 'S>C' if r['d']==2 else 'C>S'
                s = ''
                if op==2661 and len(p)>=91:
                    owner=struct.unpack_from('<I',p,5)[0]; seq=struct.unpack_from('<Q',p,9)[0]; res=struct.unpack_from('<I',p,27)[0]
                    stage=struct.unpack_from('<Q',p,19)[0]; u17=struct.unpack_from('<H',p,17)[0]
                    pos=struct.unpack_from('<3f',p,35)
                    binds=[(struct.unpack_from('<I',p,i)[0],struct.unpack_from('<I',p,i+4)[0],p[i+8]) for i in range(91,len(p),11)]
                    s='kind %d actor %08X owner %08X seq 0x%X u17 %04X stage 0x%X res %08X %s | pos (%.1f,%.1f,%.1f) | binds %s'%(p[0],struct.unpack_from('<I',p,1)[0],owner,seq,u17,stage,res,nm.get('%08X'%res,'?'),pos[0],pos[1],pos[2],' '.join('%08X->%08X/t%d'%b for b in binds))
                elif op==2981 and len(p)>=17:
                    s='kind %d actor %08X seq 0x%X state %d | %s'%(p[0],struct.unpack_from('<I',p,1)[0],struct.unpack_from('<Q',p,5)[0],struct.unpack_from('<I',p,13)[0],p[17:].hex())
                elif op==2524 and len(p)>=13:
                    s='kind %d actor %08X seq 0x%X | %s'%(p[0],struct.unpack_from('<I',p,1)[0],struct.unpack_from('<Q',p,5)[0],p[13:].hex())
                elif op==2810 and len(p)>=17:
                    s='actor %08X seq 0x%X res/stage %08X reason %d'%(struct.unpack_from('<I',p,0)[0],struct.unpack_from('<Q',p,4)[0],struct.unpack_from('<I',p,12)[0],p[16])
                else:
                    s=p[:48].hex()
                o.write('%s t=%8.3f %s %-26s s%-3d %s\n'%(ws,t,dr,OPN.get(op,str(op)),r['s'] if r['s']<1000 else -1,s))
main()
