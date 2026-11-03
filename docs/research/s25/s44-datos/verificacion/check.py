import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe
from capstone import CS_OP_IMM, CS_OP_MEM
from capstone.x86 import X86_REG_RIP

pe.md.detail = True
_rsrc = [s for s in pe.SECS if s[0] == '.rsrc'][0]
RS = pe._d[_rsrc[3]:_rsrc[3] + _rsrc[4]]


def report(rva, patch, sig_len=40, func=None):
    f = func or pe.func_of(rva)
    print(f"== {rva:#x}: pdata {f[0]:#x}-{f[1]:#x} sec {pe.sec(rva)} entry={f[0] == rva}")
    sig = pe.rd(rva, sig_len)
    n = RS.count(sig)
    print(f"  sig{sig_len} = {sig.hex()} ; occurrences in .rsrc: {n}")
    ins = pe.dis(f[0], f[1])
    # instructions covering the patch
    cov = []
    for i in ins:
        a = i.address - pe.BASE
        if rva <= a < rva + patch or (a < rva + patch and a + i.size > rva and a >= rva):
            cov.append(i)
    total = 0
    for i in ins:
        a = i.address - pe.BASE
        if a >= rva and total < patch:
            ripr = any(op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP for op in i.operands)
            print(f"  {a:#x} {i.bytes.hex():<22} {i.mnemonic} {i.op_str}  len={i.size} rip={ripr}")
            total += i.size
    print(f"  whole instructions cover {total} bytes (patch {patch})")
    # branch targets into (rva, rva+total)
    bad = []
    for i in ins:
        if i.group(1) or i.group(7) or i.mnemonic.startswith('j') or i.mnemonic == 'call':
            for op in i.operands:
                if op.type == CS_OP_IMM:
                    t = op.imm - pe.BASE
                    if rva < t < rva + total:
                        bad.append((i.address - pe.BASE, t))
    print(f"  direct branches into the patched bytes (inside the function): {bad}")


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        r, p = arg.split(':')
        report(int(r, 16), int(p))
