"""Read-only disassembly and direct-call cross references for porting evidence."""
import argparse
import struct
from signature_map import ROOT, Pe, CS, ImageScanner


def main():
    p = argparse.ArgumentParser()
    p.add_argument('rva', type=lambda s:int(s,0))
    p.add_argument('--size', type=lambda s:int(s,0), default=0x200)
    p.add_argument('--callers', action='store_true')
    args = p.parse_args()
    pe = Pe((ROOT.parent/'bin64'/'CrimsonDesert.exe').read_bytes())
    scanner = ImageScanner(pe)
    print('timestamp',hex(pe.timestamp),'function',scanner.function(args.rva))
    off = pe.rva_to_off(args.rva)
    for i in CS.disasm(pe.data[off:off+args.size], args.rva):
        print(f'{i.address:08X}  {i.bytes.hex(" "):32s} {i.mnemonic} {i.op_str}')
    if args.callers:
        for sec, buf in scanner.regions:
            at = buf.find(b'\xE8')
            while at >= 0:
                site = sec.va+at
                if at+5 <= len(buf) and site+5+struct.unpack_from('<i',buf,at+1)[0] == args.rva:
                    f = scanner.function(site)
                    if f:
                        start = f['begin']
                        foff = pe.rva_to_off(start)
                        insns = list(CS.disasm(pe.data[foff:foff+site+5-start],start))
                        if insns and insns[-1].address == site and insns[-1].mnemonic == 'call':
                            print('CALLER',hex(site),'function',f)
                at = buf.find(b'\xE8',at+1)


if __name__ == '__main__':
    main()
