import pathlib,struct,json,capstone
out=pathlib.Path(__file__).parent
b=pathlib.Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe').read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0];ns=struct.unpack_from('<H',b,pe+6)[0];sz=struct.unpack_from('<H',b,pe+20)[0];opt=pe+24;secs=[]
for j in range(ns):
 o=opt+sz+j*40;n=b[o:o+8].split(b'\0')[0].decode();vs,va,rs,rp=struct.unpack_from('<4I',b,o+8);secs.append((n,va,vs,rp,rs))
def loc(rva):
 for n,va,vs,rp,rs in secs:
  if va<=rva<va+max(vs,rs):return n,rp+rva-va
 raise ValueError(hex(rva))
def read(r,n):return b[loc(r)[1]:loc(r)[1]+n]
ex,exsz=struct.unpack_from('<II',b,opt+112+3*8);pd=[struct.unpack('<III',read(ex+i,12)) for i in range(0,exsz,12)]
def fn(r):return next(((a,z,u) for a,z,u in pd if a<=r<z),None)
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);md.detail=True
res={'timestamp':hex(struct.unpack_from('<I',b,pe+8)[0]),'sections':secs,'functions':{}}
for r in [0x2792A7B,0x27EA160,0x278EA70,0x2A78CA0,0x2068340,0x2A827B0,0x1434880,0x28AD170,0x28ADE40,0x288F0D0]:
 f=fn(r);res['functions'][hex(r)]={'section':loc(r)[0],'pdata':[hex(x) for x in f] if f else None,'entry':bool(f and f[0]==r),'bytes':read(r,32).hex(' ')}
f=fn(0x2792A7B);ins=list(md.disasm(read(f[0],f[1]-f[0]),0x140000000+f[0]));patchstart=0x142792A7B;patchend=patchstart+7
res['hook_instructions']=[{'rva':hex(i.address-0x140000000),'bytes':i.bytes.hex(' '),'asm':i.mnemonic+' '+i.op_str} for i in ins if 0x142792A00<=i.address<=0x142792AA0]
res['incoming_branches_in_patch']=[{'from':hex(i.address-0x140000000),'to':hex(i.operands[0].imm-0x140000000)} for i in ins if i.group(capstone.CS_GRP_JUMP) and i.operands and i.operands[0].type==capstone.x86.X86_OP_IMM and patchstart<i.operands[0].imm<patchend]
sig=read(0x2792A7B,20);res['aob']=sig.hex(' ');res['aob_count_file']=b.count(sig)
for r in [0x278EA70,0x27EA160]:
 f=fn(r);ins=list(md.disasm(read(f[0],f[1]-f[0]),0x140000000+f[0]));res['functions'][hex(r)]['stores']=[]
 for i in ins:
  if not i.operands:continue
  a=i.operands[0]
  if a.type==capstone.x86.X86_OP_MEM and a.mem.disp in (0x3b8,0x3c8,0x3cc,0x3cd):res['functions'][hex(r)]['stores'].append({'rva':hex(i.address-0x140000000),'bytes':i.bytes.hex(' '),'asm':i.mnemonic+' '+i.op_str})
(out/'estatico.json').write_text(json.dumps(res,indent=2),encoding='utf-8');print(json.dumps(res,indent=2))
