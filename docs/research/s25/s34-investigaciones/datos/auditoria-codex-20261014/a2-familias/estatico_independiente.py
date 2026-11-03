import pathlib,struct,mmap,json,capstone
P=pathlib.Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe'); O=pathlib.Path(__file__).parent
f=P.open('rb'); b=mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ)
def u16(o):return struct.unpack_from('<H',b,o)[0]
def u32(o):return struct.unpack_from('<I',b,o)[0]
def u64(o):return struct.unpack_from('<Q',b,o)[0]
pe=u32(0x3c); n=u16(pe+6); opt=pe+24; sh=opt+u16(pe+20); base=u64(opt+24)
sec=[]
for i in range(n):
 q=sh+40*i;sec.append(dict(name=b[q:q+8].rstrip(b'\0').decode(),vs=u32(q+8),rva=u32(q+12),size=u32(q+16),raw=u32(q+20)))
def se(r):return next((s for s in sec if s['rva']<=r<s['rva']+max(s['vs'],s['size'])),None)
def off(r):s=se(r);return s['raw']+r-s['rva']
pd_rva=u32(opt+112+3*8);pd_size=u32(opt+116+3*8);pd_raw=off(pd_rva); funcs=[struct.unpack_from('<III',b,q) for q in range(pd_raw,pd_raw+pd_size-11,12)]; funcs=[x for x in funcs if x[0] and x[1]>x[0]]
def containing(r):return next((x for x in funcs if x[0]<=r<x[1]),None)
cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);cs.detail=True
TARGETS=[0x278ea70,0x281cee0,0x281dbd0,0x2820d80,0x2799c30,0x278bbb0,0x27a0db0,0x27a1317,0x279d910,0x279fad0,0x279fca0,0x27a013d,0x2797510,0x27979ac,0x288cfd0,0x2791cd0,0x2792a7b,0x27a4a40,0x27a1430,0x27a6160,0x27a7110,0x2af9ae0,0x100f8790,0x1010eec0]
summary=[];out=[]
for r in TARGETS:
 p=containing(r);summary.append(dict(rva=hex(r),section=se(r)['name'],pdata=[hex(x) for x in p] if p else None,entry=p and p[0]==r,bytes=b[off(r):off(r)+32].hex(' ')))
 start,end=(p[0],p[1]) if p else (r,r+0x300)
 instr=list(cs.disasm(b[off(start):off(start)+end-start],base+start));wanted=set()
 for i,x in enumerate(instr):
  if any(op.type==capstone.x86_const.X86_OP_MEM and op.mem.disp in (0x2f0,0x2f8) for op in x.operands) or (x.mnemonic=='call' and x.op_str=='0x14043db70') or 0x2792a00<=x.address-base<0x2792ad0 or (r==0x100f8790 and i<30):
   wanted.update(range(max(0,i-5),min(len(instr),i+9)))
 out.append('\nFUNCTION '+hex(r)+' '+str(summary[-1]))
 for i in sorted(wanted):
  x=instr[i];out.append(f'{x.address-base:08X}: {x.bytes.hex(" "):36} {x.mnemonic} {x.op_str}')
# unique patch pattern and all relative branches inside containing function
r=0x2792a7b;p=containing(r);pat=b[off(r):off(r)+16];hits=[]
for s in sec:
 hay=b[s['raw']:s['raw']+s['size']];q=hay.find(pat)
 while q>=0:hits.append(hex(s['rva']+q));q=hay.find(pat,q+1)
ins=list(cs.disasm(b[off(p[0]):off(p[0])+p[1]-p[0]],base+p[0]));incoming=[]
for x in ins:
 if x.group(capstone.CS_GRP_JUMP) or x.group(capstone.CS_GRP_CALL):
  for op in x.operands:
   if op.type==capstone.x86_const.X86_OP_IMM and base+r<op.imm<base+r+7:incoming.append([hex(x.address-base),hex(op.imm-base)])
meta=dict(timestamp=hex(u32(pe+8)),base=hex(base),sections=sec,targets=summary,hook_pattern=pat.hex(' '),hook_hits=hits,interior_direct_branches=incoming,pdata_boundary_match=any(x.address==base+r for x in ins))
(O/'estatico.json').write_text(json.dumps(meta,indent=2),encoding='utf8');(O/'estatico-disasm.txt').write_text('\n'.join(out),encoding='utf8');print(json.dumps(meta,indent=2))


