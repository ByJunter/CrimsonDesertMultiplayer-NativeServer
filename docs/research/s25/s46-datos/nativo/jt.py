# list branch targets in function containing rva that land in [lo,hi)
import sys,re
sys.path.insert(0, r"C:\Emulador CD\build\research")
import cdpe
pe=cdpe.PE();B=cdpe.BASE
f=int(sys.argv[1],16); lo=int(sys.argv[2],16); hi=int(sys.argv[3],16)
fs=cdpe.func_of(pe,f); end=[e for b,e,u in cdpe._pd if b==fs][0]
# also scan whole pdata chunks of the function (chained) -> scan +-0x2000 around
cnt=0
for start,stop in [(fs,end)]:
  for ins in pe.md.disasm(pe.read(start,stop-start),B+start):
    if ins.mnemonic.startswith('j') or ins.mnemonic=='call':
      m=re.match(r"0x([0-9a-f]+)$",ins.op_str)
      if m:
        t=int(m.group(1),16)-B
        if lo<=t<hi: print(f"{ins.address-B:#x} {ins.mnemonic} -> {t:#x}"); cnt+=1
print("func",hex(fs),hex(end),"hits",cnt)
