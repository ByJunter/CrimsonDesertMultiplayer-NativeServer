from pathlib import Path
import struct,json,capstone
b=Path(r'D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe').read_bytes();pe=struct.unpack_from('<I',b,60)[0];n=struct.unpack_from('<H',b,pe+6)[0];os=struct.unpack_from('<H',b,pe+20)[0];secs=[]
for i in range(n):
 o=pe+24+os+40*i;name,vs,rva,rs,raw=struct.unpack_from('<8sIIII',b,o);secs.append((name.rstrip(b'\0').decode(),vs,rva,rs,raw))
def pos(a):
 for name,vs,rva,rs,raw in secs:
  if rva<=a<rva+rs:return raw+a-rva
 raise Exception(a)
a=0x282dc80;head=b[pos(a):pos(a)+48];print('HEAD',head.hex(' '));print('SECT',[s for s in secs if s[2]<=a<s[2]+s[1]])
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);ins=[(hex(i.address),i.bytes.hex(' '),i.mnemonic,i.op_str) for i in md.disasm(head,0x140000000+a)];print('ASM',ins)
rt,size=struct.unpack_from('<II',b,pe+24+112+3*8);pd=b[pos(rt):pos(rt)+size];entries=[struct.unpack_from('<III',pd,o) for o in range(0,len(pd),12) if struct.unpack_from('<I',pd,o)[0]==a];print('PDATA',entries)
sig=head[:47];matches=[]
for sn,vs,va,rs,rp in secs:
 start=0
 while True:
  x=b.find(sig,rp+start,rp+rs)
  if x<0:break
  matches.append((sn,hex(va+x-rp)));start=x-rp+1
print('AOB',sig.hex(' '),'matches',matches)
Path('build/research/s42/puertas/hook-verificado.json').write_text(json.dumps({'rva':hex(a),'head':head.hex(),'asm':ins,'pdata':entries,'signature':sig.hex(),'matches':matches},indent=2),encoding='utf8')
