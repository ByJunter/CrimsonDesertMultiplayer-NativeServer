#!/usr/bin/env python3
"""Is the installed exe still the layout the mod knows? Read-only; never starts or changes the game.

Compares the exe on disk with what is saved of 3527 in tools/porting/data (the old exe itself is gone: Steam replaces it)
and prints what points into the protected RWX section, which is what moves between builds. It is evidence, not a port: a
new build still needs its PE identity and these values in the source, then a live test (docs/ESTADO-3527.md).

  python tools/porting/build_delta.py [--exe PATH] [--rtti DUMP] [--out FILE]
"""
from __future__ import annotations

import argparse
import bisect
import collections
import glob
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'scripts'))
sys.path.insert(0, str(Path(__file__).parent))
from rtti_dump import Pe
from signature_map import exception_ranges

DATA = Path(__file__).parent / 'data'
EXECUTE, WRITE = 0x20000000, 0x80000000
LINE = re.compile(r'^\s*(?:0x)?([0-9A-Fa-f]{6,9})[:\s]+([0-9a-fA-F]{2}(?: ?[0-9a-fA-F]{2})*)\s{2,}\S')
# name, thunk in the real code (its address does not move), the target it had on 3527
THUNKS = [('GetOrCreate', 0x2548E40, 0xD43ED30), ('SaveMgr refresh', 0x19B2080, 0xB7449C0),
          ('owner user stub', 0x22A2880, 0xCA26A10), ('ClientVehicle vf53', 0xB4D8D0, 0x997A230),
          ('CommonVehicle RideOff', 0x23FDCC0, 0xCD8D560), ('CameraSelector', 0x37A0620, 0x1068DEB0),
          ('stage processor ctor', 0x695740, 0x8DF2F60), ('stage processor dtor thunk', 0x6959B0, 0x8DF4480),
          ('stage processor dtor', 0x695A10, 0x8DF44E0), ('manager bind', 0xB9FD80, 0x9A7F860),
          ('2215 challenge discover', 0x2CD6910, 0xE4FFF10)]
# Functions in the real code whose direct callers in the RWX section are return addresses the mod compares.
CALLED = [('LoadSlot', 0x19B09D0), ('SavePlayerToSlot', 0x19AF6D0), ('FlushPendingSaveData', 0x19B3020)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', type=Path, default=ROOT.parent / 'bin64' / 'CrimsonDesert.exe')
    ap.add_argument('--rtti', type=Path, help='an rtti_dump.py dump of --exe; made in a temp folder if absent')
    ap.add_argument('--out', type=Path, help='also write the result as JSON')
    args = ap.parse_args()
    data = args.exe.read_bytes()
    pe = Pe(data)
    nt = struct.unpack_from('<I', data, 0x3C)[0]
    size_of_image = struct.unpack_from('<I', data, nt + 24 + 56)[0]
    real = [s for s in pe.sections if s.flags & EXECUTE and not s.flags & WRITE]
    rwx = [s for s in pe.sections if s.flags & EXECUTE and s.flags & WRITE]
    if len(real) != 1 or len(rwx) != 1:
        raise SystemExit(f'expected one real code section and one RWX one: {len(real)}, {len(rwx)}')
    real, rwx = real[0], rwx[0]
    result = {'file_size': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'timestamp': hex(pe.timestamp),
              'size_of_image': hex(size_of_image), 'real_code': [real.name, hex(real.va), hex(real.raw_size)],
              'rwx': [rwx.name, hex(rwx.va), hex(rwx.raw_size)], 'functions': len(exception_ranges(pe))}
    print(f"PE {result['timestamp']}, image {result['size_of_image']}, file {len(data)}, {result['functions']} functions")
    print(f"real code {real.name} {real.va:#x}+{real.raw_size:#x} | RWX {rwx.name} {rwx.va:#x}+{rwx.raw_size:#x}")

    # Vtables, against the saved 3527 dump.
    dump = args.rtti
    tmp = None
    if not dump:
        tmp = tempfile.TemporaryDirectory()
        dump = Path(tmp.name) / 'rtti.json'
        subprocess.run([sys.executable, str(ROOT / 'tools' / 'scripts' / 'rtti_dump.py'), str(args.exe), '-o', str(dump)],
                       check=True, capture_output=True)
    old = json.loads((DATA / 'rtti_3527.json').read_text(encoding='utf-8'))['classes']
    new = json.loads(dump.read_text(encoding='utf-8'))['classes']
    moved = collections.Counter()
    for name, cls in old.items():
        if name not in new:
            continue
        a = {v['offset']: v['rva'] for v in cls['vtables'] if v['rva']}
        b = {v['offset']: v['rva'] for v in new[name]['vtables'] if v['rva']}
        for off in a.keys() & b.keys():
            moved[int(b[off], 16) - int(a[off], 16)] += 1
    result['vtables'] = {'compared': sum(moved.values()), 'same_address': moved[0], 'classes_only_in_3527': len(set(old) - set(new))}
    print('vtables:', result['vtables'])

    # Instruction bytes of the saved 3527 listings, at the same address.
    stats = collections.Counter()
    samples = []
    seen = set()
    for f in sorted(glob.glob(str(DATA / '*_3527.txt'))):
        for line in open(f, errors='replace'):
            m = LINE.match(line)
            if not m:
                continue
            rva, code = int(m[1], 16), bytes.fromhex(m[2].replace(' ', ''))
            if (rva, code) in seen:
                continue
            seen.add((rva, code))
            off = pe.rva_to_off(rva)
            if off is None or off + len(code) > len(data):
                continue
            region = 'real' if real.va <= rva < real.va + real.raw_size else 'rwx' if rwx.va <= rva < rwx.va + rwx.raw_size else 'other'
            same = data[off:off + len(code)] == code
            stats[region, same] += 1
            stats[region + '_bytes', same] += len(code)
            if region == 'real' and not same and len(samples) < 200:
                samples.append(f'{rva:#x} {code.hex(" ")} -> {data[off:off + len(code)].hex(" ")} ({os.path.basename(f)})')
    result['listing_instructions'] = {f'{r} same' if s else f'{r} different': n for (r, s), n in sorted(stats.items())}
    result['real_code_differences'] = samples
    print('saved 3527 instructions:', result['listing_instructions'])
    for s in samples:
        print('   ', s)

    # What points into the RWX section.
    thunks = {}
    for name, at, was in THUNKS:
        code = data[pe.rva_to_off(at):pe.rva_to_off(at) + 5]
        now = at + 5 + struct.unpack('<i', code[1:])[0] if code[0] == 0xE9 else None
        thunks[name] = {'at': hex(at), 'bytes': code.hex(' '), 'target': hex(now) if now else None, 'target_3527': hex(was)}
        print(f"  {name:28s} {at:#x}: {code.hex(' ')} -> {thunks[name]['target']} (3527: {was:#x})")
    result['thunks'] = thunks

    section = data[rwx.raw_ptr:rwx.raw_ptr + rwx.raw_size]
    functions = exception_ranges(pe)
    starts = [f[0] for f in functions]
    callers = {}
    for name, target in CALLED:
        rows = []
        at = section.find(b'\xE8')
        while at >= 0:
            site = rwx.va + at
            if at + 5 <= len(section) and site + 5 + struct.unpack_from('<i', section, at + 1)[0] == target:
                i = bisect.bisect_right(starts, site) - 1
                begin = functions[i][0] if i >= 0 and functions[i][0] <= site < functions[i][1] else None
                rows.append({'call': hex(site), 'ret': hex(site + 5), 'function': hex(begin) if begin else None})
            at = section.find(b'\xE8', at + 1)
        callers[name] = rows
        print(f'  callers of {name} in the RWX section:', [(r['ret'], r['function']) for r in rows])
    result['rwx_callers'] = callers

    if args.out:
        args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('wrote', args.out)
    if tmp:
        tmp.cleanup()


if __name__ == '__main__':
    main()
