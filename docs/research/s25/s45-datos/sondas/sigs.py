import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
D=pe._d
rs=[s for s in pe.SECS if s[0]=='.rsrc'][0]
seg=D[rs[3]:rs[3]+rs[4]]
def cnt(b):
    n=0; i=seg.find(b)
    while i!=-1: n+=1; i=seg.find(b,i+1)
    return n
def carr(b): return ', '.join(f'0x{x:02X}' for x in b)
for a,n in [(0x34CFC20,16),(0xB27880,40),(0xC38F30,16),(0xC3A440,40),(0xC4D6B0,40),(0xC45747,24),(0xC45740,7)]:
    b=pe.rd(a,n); pd=pe.func_of(a)
    print(f"{a:#x} n={n} unique={cnt(b)} pdata={pd and (hex(pd[0]),hex(pd[1]))}\n  {{{carr(b)}}}")
for vt in [0x558C080,0x590DF78,0x5B1FE10,0x558B4A8,0x55F71B8,0x562E0B0,0x562E420,0x5629BF0,0x5630920,0x55C9720]:
    print(f"vt {vt:#x} col {pe.u64(vt-8)-pe.BASE:#x} s99 {pe.u64(vt+99*8)-pe.BASE:#x}")
# context bytes
for a,n in [(0xA9D8F6,25),(0x34DC175,6),(0x34DBB03,6),(0xB27DEE,27),(0xA98D3E,7),(0x5E4E6E,7)]:
    b=pe.rd(a,n); print(f"ctx {a:#x} unique={cnt(b)} {{{carr(b)}}}"); pe.pr(a,a+n)
# R-A
RET=0x359B50
def slots(vt,n=105): return [pe.u64(vt+8*k)-pe.BASE for k in range(n)]
c,m,s=slots(0x558C080),slots(0x590DF78),slots(0x5B1FE10)
co=[k for k in range(105) if c[k]!=RET and m[k]==RET and s[k]==RET]
cs=[k for k in range(105) if c[k]!=RET and m[k]==RET and s[k]!=RET]
print('client-only', len(co), [hex(8*k) for k in co])
print('client+server', len(cs), [hex(8*k) for k in cs])
# check the slot after 104 (end of vtable?)
print('slot105', hex(pe.u64(0x558C080+8*105)-pe.BASE), hex(pe.u64(0x590DF78+8*105)-pe.BASE), hex(pe.u64(0x5B1FE10+8*105)-pe.BASE))
