#!/usr/bin/env python3
"""Recover named Troc singleton metadata from a PE without executing the game.

The vptrs are set by dynamic initializers, so scanning static data for vtable
pointers cannot recover them. Instead, this tool follows RIP-relative LEAs of
known Troc vtables and the subsequent RIP-relative store of that same register.
Only the exact adjacent LEA/MOV idiom is admitted; uncertain matches stay absent.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'scripts'))
from rtti_dump import Pe

LEA_RIP = re.compile(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....', re.S)


def hx(v):
    return None if v is None else f'0x{v:X}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT.parent / 'bin64' / 'CrimsonDesert.exe')
    parser.add_argument('--mapping', type=Path, default=Path(__file__).parent / 'data' / 'rtti_mapping_2976_3527.json')
    parser.add_argument('--out', type=Path, default=Path(__file__).parent / 'data' / 'troc_mapping_2976_3527.json')
    parser.add_argument('--debug', action='store_true')
    args = parser.parse_args()
    pe = Pe(args.exe.read_bytes())
    prior = json.loads(args.mapping.read_text(encoding='utf-8'))
    if int(prior['timestamp'], 16) != pe.timestamp or prior['sha256'] != hashlib.sha256(pe.data).hexdigest():
        raise ValueError('Executable does not match RTTI mapping identity')
    tables = {int(m['vtable'], 16): m for m in prior['messages']}
    found = {}
    refs = {}
    for section in pe.sections:
        if not section.exec or section.flags & 0x80000000:
            continue
        data = pe.data[section.raw_ptr:section.raw_ptr + section.raw_size]
        for match in LEA_RIP.finditer(data):
            start = match.start()
            code = match.group()
            rva = section.va + start
            target = rva + 7 + struct.unpack_from('<i', code, 3)[0]
            if target not in tables:
                continue
            refs.setdefault(target, []).append(rva)
            following = data[start + 7:start + 14]
            if args.debug and len(refs[target]) < 3:
                print(tables[target]['name'], hx(rva), data[start:start + 50].hex(' '))
            # A RIP-relative MOV with matching REX.R / ModRM.reg is an exact
            # write of the vtable address just materialised by LEA.
            if len(following) != 7 or following[:1] != code[:1] or following[1] != 0x89 or following[2] != code[2]:
                continue
            instance = rva + 14 + struct.unpack_from('<i', following, 3)[0]
            sec = pe.section_of(instance)
            pos = pe.rva_to_off(instance)
            if sec is None or sec.exec or not (sec.flags & 0x80000000) or pos is None or pos + 0x90 > len(pe.data):
                continue
            kind, opcode = struct.unpack_from('<IH', pe.data, pos + 8)
            if kind not in (1, 2, 4, 0x7FFFFFFE) or not 1000 <= opcode <= 9999:
                continue
            meta = {
                'static_instance': hx(instance), 'id': opcode, 'kind': kind,
                'flag_10': struct.unpack_from('<I', pe.data, pos + 0x10)[0],
                'flags_14': hx(struct.unpack_from('<I', pe.data, pos + 0x14)[0]),
                'min_size': struct.unpack_from('<H', pe.data, pos + 0x18)[0],
                'max_size': struct.unpack_from('<H', pe.data, pos + 0x1A)[0],
                'flag_1c': struct.unpack_from('<I', pe.data, pos + 0x1C)[0],
                'flag_88': pe.data[pos + 0x88],
                'vtable': hx(target), 'vptr_store': hx(rva + 7),
                'initializer_lea': hx(rva), 'initializer_bytes': (code + following).hex(' '),
                'evidence': 'Named RTTI vtable, adjacent RIP-relative LEA/store, initialized singleton fields in writable data',
            }
            found.setdefault(target, {})[instance] = meta
    rows = []
    for vt, source in tables.items():
        candidates = list(found.get(vt, {}).values())
        row = {'name': source['name'], 'old': source['old'], 'new': candidates[0] if len(candidates) == 1 else None,
               'candidates': candidates, 'vtable_references': [hx(r) for r in refs.get(vt, [])],
               'handler': source['functions'][2] if len(source['functions']) > 2 else None}
        rows.append(row)
    old_by_name = {r['name']: r for r in csv.DictReader((ROOT / 'docs' / 'research' / 'data' / 'troc_messages_2976.csv').open(encoding='utf-8-sig'))}
    metadata_changes = []
    for row in rows:
        if row['old'] is None or row['new'] is None:
            continue
        for key in ('kind', 'min_size', 'max_size', 'flag_10', 'flags_14', 'flag_1c', 'flag_88'):
            previous = int(row['old'][key], 0)
            current = row['new'][key]
            if isinstance(current, str):
                current = int(current, 0)
            if previous != current:
                metadata_changes.append({'name': row['name'], 'field': key, 'old': previous, 'new': current})
    opcodes = [r['new']['id'] for r in rows if r['new']]
    instance_addresses = [int(r['new']['static_instance'], 16) for r in rows if r['new']]
    if len(opcodes) != len(set(opcodes)) or len(instance_addresses) != len(set(instance_addresses)):
        raise ValueError('Troc opcode or singleton address is not unique')
    opcode_mapping = [{'name': r['name'], 'old': int(r['old']['id']), 'new': r['new']['id'], 'kind': r['new']['kind']}
                      for r in rows if r['new'] and r['old']]
    current_names = {r['name'] for r in rows}
    missing = [{'name': n, 'old': int(v['id']), 'kind': int(v['kind'])}
               for n, v in old_by_name.items() if n not in current_names]
    addresses = [{'name': r['name'], 'old': r['old']['static_instance'], 'new': r['new']['static_instance'],
                  'kind': 'static_troc_instance', 'evidence': r['new']['evidence']}
                 for r in rows if r['old'] and r['new']]
    result = {
        'source_build': prior['source_build'], 'target_build': prior['target_build'],
        'exe': str(args.exe.resolve()), 'sha256': prior['sha256'], 'timestamp': prior['timestamp'],
        'stats': {'classes': len(rows), 'resolved': sum(r['new'] is not None for r in rows),
                  'unresolved': sum(r['new'] is None for r in rows),
                  'changed_opcodes': sum(r['new'] is not None and r['old'] is not None and r['new']['id'] != int(r['old']['id']) for r in rows)},
        'instance_range': {'begin': hx(min(instance_addresses)), 'end_exclusive': hx(max(instance_addresses) + 0x90), 'stride': 0x90},
        'metadata_changes': metadata_changes,
        'opcode_mapping': opcode_mapping, 'unmapped_old_opcodes': missing,
        'added_opcodes': [{'name': r['name'], 'new': r['new']['id'], 'kind': r['new']['kind']}
                          for r in rows if r['new'] and not r['old']],
        'new_names': sorted(current_names - set(old_by_name)),
        'address_candidates': addresses, 'messages': rows,
        'limitations': [
            'Matching RTTI names identify message semantics, not binary payload compatibility.',
            'Variable-size payload field layouts must be verified independently before native injection.',
            'ScopeSenderAck may fragment nested packets: preserve its payload as opaque native-build bytes unless a complete reassembler is implemented.',
            'Missing old names are not automatically aliased to similar new names.',
            'This is static evidence; it does not launch, modify, or validate a live game session.',
        ],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2), encoding='utf-8')
    compact = {key: result[key] for key in ('source_build', 'target_build', 'sha256', 'timestamp', 'stats',
                                          'instance_range', 'opcode_mapping', 'unmapped_old_opcodes',
                                          'added_opcodes', 'new_names', 'metadata_changes', 'limitations')}
    # The compact file is named after the output (troc_mapping_X_Y.json -> opcode_mapping_X_Y.json): a run for another
    # build pair must not overwrite 2976 -> 3527's, which generate_native_opcode.py reads.
    compact_path = args.out.with_name(args.out.name.replace('troc_mapping', 'opcode_mapping'))
    compact_path.write_text(json.dumps(compact, indent=2), encoding='utf-8')
    print(json.dumps(result['stats'], indent=2))
    print('Unresolved:', [r['name'] for r in rows if r['new'] is None])
    print(f'Wrote {args.out}')
    print(f'Wrote {compact_path}')


if __name__ == '__main__':
    main()
