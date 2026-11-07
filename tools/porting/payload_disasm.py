"""Bounded static disassembly for the payload ABI audit (never runs game code)."""
import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'deps'))
sys.path.insert(0, str(HERE.parents[1] / 'build/porting-python-deps'))
from signature_map import ROOT, Pe, CS, ImageScanner


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('targets', nargs='+', help='Troc name or hexadecimal RVA')
    ap.add_argument('--out', type=Path, default=HERE / 'data/payload_disasm_3527.txt')
    ap.add_argument('--limit', type=int, default=0x5000)
    args = ap.parse_args()
    pe = Pe((ROOT.parent / 'bin64/CrimsonDesert.exe').read_bytes())
    assert pe.timestamp == 0x6AEA07A6
    scan = ImageScanner(pe)
    rows = {r['name']: r for r in json.loads((HERE / 'data/troc_mapping_2976_3527.json').read_text())['messages']}
    lines = []
    for target in args.targets:
        rva = int(rows[target]['handler'], 16) if target in rows else int(target, 16)
        fn = scan.function(rva)
        stop = min(fn['end'], rva + args.limit) if fn else rva + 0x300
        off = pe.rva_to_off(rva)
        lines.append(f'\n=== {target}: 0x{rva:X} range={fn} section={pe.section_of(rva).name} ===')
        for ins in CS.disasm(pe.data[off:off + stop - rva], rva):
            lines.append(f'{ins.address:08X} {ins.bytes.hex():24} {ins.mnemonic:8} {ins.op_str}')
    args.out.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(f'Wrote {len(lines)} lines to {args.out}')


if __name__ == '__main__':
    main()
