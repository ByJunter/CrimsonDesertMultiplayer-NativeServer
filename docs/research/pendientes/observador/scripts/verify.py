import sys,os,re,struct,hashlib; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pedis as D
from capstone import *
from capstone.x86 import *
md=Cs(CS_ARCH_X86,CS_MODE_64); md.detail=True
pts=[(int(a.split(':')[0],16),int(a.split(':')[1]),int(a.split(':')[2]) if a.count(':')>1 else 16) for a in sys.argv[1:]]
# rel32 index over sections
rel_targets={}
for name,va,vs,ro,rs in D.secs:
    if name not in ('.rsrc','.xtls','.text'): continue
    data=D.mm[ro:ro+min(rs,vs if vs else rs)]
    for m in re.finditer(b'\xE8|\xE9|\x0F[\x80-\x8F]',data):
        o=m.start(); L=len(m.group(0))
        if o+L+4>len(data): continue
        t=va+o+L+4+struct.unpack_from('<i',data,o+L)[0]
        rel_targets.setdefault(t,[]).append((name,va+o))
for rva,patch,n in pts:
    s=D.sec(rva); f=D.func_of(rva)
    print(f"== {rva:08X} patch {patch} section {s[0]} func {f[0]:08X}-{f[1]:08X}")
    code=D.rd(f[0],f[1]-f[0]); bounds=set(); tg=[]
    for ins in md.disasm(code,0x140000000+f[0]):
        a=ins.address-0x140000000; bounds.add(a)
        if ins.group(X86_GRP_JUMP) or ins.group(X86_GRP_CALL):
            for op in ins.operands:
                if op.type==X86_OP_IMM: tg.append((a,op.imm-0x140000000))
        if rva<=a<rva+patch:
            print(f"   {a:08X} {ins.bytes.hex(' '):<24} {ins.mnemonic} {ins.op_str}" + ("  RIP-REL!" if 'rip' in ins.op_str else ''))
    print("   boundary at rva:",rva in bounds," at rva+patch:",(rva+patch) in bounds)
    bad=[(a,t) for a,t in tg if rva<t<rva+patch]
    print("   function branches into patch:",bad)
    inside=[(t,v) for t,v in rel_targets.items() if rva<t<rva+patch]
    print("   rel32 (.rsrc/.xtls/.text) into patch:",inside)
    print("   expected bytes:", D.rd(rva,n).hex(' ').upper())
