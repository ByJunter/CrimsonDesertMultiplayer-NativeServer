import csv, re, pe, collections
rows=[r for r in csv.DictReader(open(r'D:\RE\CrimsonDesert_2976\exports\rtti_classes.csv',encoding='utf-8')) if r['class'].startswith('pa::StageChart_Function_') and r['vtable_rva']]
byoff=collections.defaultdict(set); pats=collections.Counter()
for r in rows:
    sl=[int(x,16) for x in r['slots'].split(';') if x]
    for vf in (8,9):
        if vf>=len(sl): continue
        ex=sl[vf]
        b=pe.rd(ex,24)
        ins=pe.dis(ex,ex+24)
        off=None
        for x in ins:
            m=re.match(r'qword ptr \[rax \+ (0x[0-9a-f]+)\]',x.op_str)
            if x.mnemonic=='jmp' and m: off=int(m.group(1),16); break
            if x.mnemonic in('ret','call','jmp'): break
        if off is None: continue
        n=sum(1 for x in ins[:[i for i,x in enumerate(ins) if x.mnemonic=='jmp'][0]+1])
        L=sum(x.size for x in ins[:n])
        pats[b[:L-4].hex()]+=1
        byoff[off].add((ex,vf,r['class'][24:],b[:L].hex()))
print(pats)
for o in sorted(byoff):
    print(hex(o),sorted((hex(e),vf,c) for e,vf,c,_ in byoff[o]))
