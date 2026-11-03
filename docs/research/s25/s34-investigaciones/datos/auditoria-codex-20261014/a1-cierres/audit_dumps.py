from pathlib import Path
import struct,json,bisect
U16=lambda b,o=0:struct.unpack_from('<H',b,o)[0]
U32=lambda b,o=0:struct.unpack_from('<I',b,o)[0]
U64=lambda b,o=0:struct.unpack_from('<Q',b,o)[0]
REG=['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15']
class PE:
 def __init__(self,path):
  self.b=Path(path).read_bytes();p=U32(self.b,0x3c);n=U16(self.b,p+6);self.timestamp=U32(self.b,p+8);opt=p+24;self.base=U64(self.b,opt+24);s=opt+U16(self.b,p+20);self.sections=[]
  for i in range(n):
   o=s+40*i;self.sections.append((self.b[o:o+8].rstrip(b'\0').decode(),U32(self.b,o+12),U32(self.b,o+8),U32(self.b,o+20),U32(self.b,o+16)))
  pd,psz=struct.unpack_from('<II',self.b,opt+112+3*8);self.pdata=[struct.unpack('<III',self.read(pd+i,12))for i in range(0,psz,12)];self.starts=[x[0]for x in self.pdata]
 def section(self,rva):return next((s for s in self.sections if s[1]<=rva<s[1]+max(s[2],s[4])),None)
 def offset(self,rva):
  s=self.section(rva)
  if not s:raise ValueError('RVA no mapeada '+hex(rva))
  return s[3]+rva-s[1]
 def read(self,rva,n):o=self.offset(rva);return self.b[o:o+n]
 def func(self,rva):
  i=bisect.bisect_right(self.starts,rva)-1
  return self.pdata[i] if i>=0 and rva<self.pdata[i][1] else None
class Dump:
 def __init__(self,path):
  self.path=str(path);self.b=Path(path).read_bytes();assert self.b[:4]==b'MDMP';n,d=U32(self.b,8),U32(self.b,12);self.dir={};self.ranges=[];self.modules=[]
  for i in range(n):
   ty,sz,rva=struct.unpack_from('<III',self.b,d+12*i);self.dir[ty]=(rva,sz)
  if 5 in self.dir:
   p,_=self.dir[5]
   for i in range(U32(self.b,p)):
    a,sz,rva=struct.unpack_from('<QII',self.b,p+4+16*i);self.ranges.append((a,a+sz,rva))
  if 9 in self.dir:
   p,_=self.dir[9];cnt,fr=U64(self.b,p),U64(self.b,p+8)
   for i in range(cnt):
    a,sz=struct.unpack_from('<QQ',self.b,p+16+16*i);self.ranges.append((a,a+sz,fr));fr+=sz
  p,_=self.dir[3];self.threads={}
  for i in range(U32(self.b,p)):
   o=p+4+48*i;tid=U32(self.b,o);a=U64(self.b,o+24);sz,rva=struct.unpack_from('<II',self.b,o+32);self.ranges.append((a,a+sz,rva));self.threads[tid]=(a,sz)
  p,_=self.dir[4]
  for i in range(U32(self.b,p)):
   o=p+4+108*i;a=U64(self.b,o);sz=U32(self.b,o+8);nr=U32(self.b,o+20);name=self.b[nr+4:nr+4+U32(self.b,nr)].decode('utf-16-le');self.modules.append((a,a+sz,name))
  p,_=self.dir[6];self.tid=U32(self.b,p);self.exception={'code':hex(U32(self.b,p+8)),'address':hex(U64(self.b,p+24)),'params':[hex(U64(self.b,p+40+8*i))for i in range(U32(self.b,p+32))]};c=U32(self.b,p+164);self.regs={name:U64(self.b,c+120+8*i)for i,name in enumerate(REG)};self.regs['rip']=U64(self.b,c+248);self.context_file_offset=c
 def pos(self,a,n):
  for s,e,p in self.ranges:
   if s<=a and a+n<=e:return p+a-s
  raise ValueError('Memoria no incluida '+hex(a)+' len '+str(n))
 def read(self,a,n):p=self.pos(a,n);return self.b[p:p+n]
 def q(self,a):return U64(self.read(a,8))
 def d(self,a):return U32(self.read(a,4))
 def module(self,a):return next((m for m in self.modules if m[0]<=a<m[1]),None)
 def val(self,a,n=8):
  try:return {'va':hex(a),'file_offset':hex(self.pos(a,n)),'value':hex(int.from_bytes(self.read(a,n),'little'))}
  except ValueError as e:return {'va':hex(a),'error':str(e)}
def unwind(d,pe,regs):
 r=dict(regs);f=pe.func(r['rip']-pe.base);ops=[]
 if f is None:raise ValueError('Sin pdata; no se supone hoja')
 seen=set()
 while f:
  begin,end,uw=f
  if uw in seen:raise ValueError('Ciclo unwind')
  seen.add(uw);h=pe.read(uw,4);flags=h[0]>>3;ver=h[0]&7;prolog=h[1];cnt=h[2];fr=h[3]&15;fo=h[3]>>4
  if ver!=1:raise ValueError('Unwind version '+str(ver))
  c=pe.read(uw+4,cnt*2);i=0;delta=regs['rip']-pe.base-begin
  while i<cnt:
   off,b=c[2*i:2*i+2];op=b&15;info=b>>4;i+=1;arg=None
   if op in (1,4,8):
    if op==1 and info==1:arg=U32(c,2*i);i+=2
    else:arg=U16(c,2*i);i+=1
   elif op in (5,9):arg=U32(c,2*i);i+=2
   if delta<prolog and off>delta:continue
   ops.append({'unwind':hex(uw),'op':op,'info':info,'arg':arg})
   if op==0:r[REG[info]]=d.q(r['rsp']);r['rsp']+=8
   elif op==1:r['rsp']+=arg*8 if info==0 else arg
   elif op==2:r['rsp']+=info*8+8
   elif op==3:r['rsp']=r[REG[fr]]-fo*16
   elif op in (4,5):r[REG[info]]=d.q(r['rsp']+(arg*8 if op==4 else arg))
   elif op in (8,9):pass
   else:raise ValueError('Unwind op no implementado '+str(op))
  if flags&4:f=struct.unpack('<III',pe.read(uw+4+((cnt+1)//2)*4,12));continue
  break
 ret_slot=r['rsp'];r['rip']=d.q(ret_slot);r['rsp']+=8
 return r,ops,ret_slot
def main():
 pe=PE(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe');out={'pe_timestamp':hex(pe.timestamp),'base':hex(pe.base),'dumps':[]}
 paths=[r'C:\Emulador CD\backups\p8-sesion34\shadow\cdmp_crash_20261014-170638_1704_1.dmp',r'C:\Emulador CD\backups\p8-sesion34\shadow-p1b-p2b\cdmp_crash_20261014-180710_6364_1.dmp']
 for path in paths:
  d=Dump(path);x={'file':path,'tid':d.tid,'context_file_offset':hex(d.context_file_offset),'exception':d.exception,'frames':[]};r=d.regs
  for i in range(30):
   m=d.module(r['rip']);f=pe.func(r['rip']-pe.base) if m and m[0]==pe.base else None
   row={'index':i,'rip':hex(r['rip']),'rsp':hex(r['rsp']),'registers':{k:hex(v)for k,v in r.items()},'module':m[2]if m else None,'pdata':list(map(hex,f))if f else None,'section':pe.section(r['rip']-pe.base)[0]if f else None};x['frames'].append(row)
   if not f:row['stop']='No se desenrolla modulo sin PE disponible';break
   try:r,ops,slot=unwind(d,pe,r);row['unwind_ops']=ops;row['return_slot']=d.val(slot)
   except Exception as e:row['stop']=str(e);break
  if '170638' in path:
   actor=int(x['frames'][2]['registers']['rbx'],16)
   vals={'actor':hex(actor),'actor_fields':{hex(o):d.val(actor+o,4)for o in (0x60,0x90,0xac)}}
   ctx=d.regs['r14'];vals['movement_context']=hex(ctx);vals['ctx_plus10']=d.val(ctx+0x10)
   cc=d.q(d.regs['rsi']+0x60);vals['cc']=hex(cc);vals['cc_actor']=d.val(cc+8);vals['cc_vtable']=d.val(cc);vals['cc_layer']=d.val(cc+0x88)
   try:vals['ctx_plus10_indirect']=d.val(d.q(ctx+0x10))
   except ValueError:pass
  else:
   actor=d.regs['r14'];cc=d.regs['rdx'];layer=d.q(cc+0x88);hier=d.regs['rsi'];other=d.q(hier+8);rest=d.regs['rbx']
   vals={'actor':hex(actor),'cc':hex(cc),'layer':hex(layer),'hierarchy':hex(hier),'other_actor':hex(other),'actor_fields':{hex(o):d.val(actor+o,4)for o in (0x60,0x90,0xac)},'cc_actor':d.val(cc+8),'cc_vtable':d.val(cc),'layer_fields':{hex(o):d.val(layer+o)for o in (0x10,0x18,0x110)},'restore':d.val(rest+0x30,1),'hierarchy_parent':d.val(hier+0x1c,4),'hierarchy_vtable':d.val(hier),'other_fields':{hex(o):d.val(other+o,4)for o in (0x60,0x90,0xac)}}
  x['memory']=vals;out['dumps'].append(x)
 Path(__file__).with_name('dumps-independientes.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
 for x in out['dumps']:
  print(x['file'],x['exception']);print([(f['index'],f['rip'],f.get('stop'))for f in x['frames']]);print(json.dumps(x['memory'],indent=2))
if __name__=='__main__':main()
