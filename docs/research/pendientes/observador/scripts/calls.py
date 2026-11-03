import sys,os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
for a in sys.argv[1:]:
    f=D.func_of(int(a,16)); s,e=f[0],f[1]
    print(f"=== {s:X}-{e:X}")
    for ins in D.md.disasm(D.rd(s,e-s),0x140000000+s):
        r=ins.address-0x140000000
        txt=f"{ins.mnemonic} {ins.op_str}"
        if ins.mnemonic=='call' or ('rip' in ins.op_str and ins.mnemonic in('lea','mov')) or ins.mnemonic=='cmp' and ('bl' in ins.op_str or ', 4' in ins.op_str):
            extra=''
            if ins.mnemonic=='call' and ins.op_str.startswith('0x'):
                t=int(ins.op_str,16)-0x140000000; sc=D.sec(t); extra=f" ; {t:X} {sc[0] if sc else '?'}"
            import re
            m=re.search(r'rip \+ (0x[0-9a-f]+)',ins.op_str)
            if m: extra=f" ; -> {r+ins.size+int(m.group(1),16):X}"
            if 'rip' in ins.op_str and '0x5721c' in extra: continue
            print(f"  {r:08X} {txt}{extra}")
