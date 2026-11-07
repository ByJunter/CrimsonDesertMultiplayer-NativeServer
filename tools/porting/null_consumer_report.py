"""Verify build3527's guest null-consumer mapping without executing game code."""
import hashlib
import json
import re
import struct
from pathlib import Path

import payload_disasm
from signature_map import Pe, ROOT, ImageScanner


def main():
    pe = Pe((ROOT.parent / 'bin64/CrimsonDesert.exe').read_bytes())
    digest = hashlib.sha256(pe.data).hexdigest()
    assert digest == 'dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841'
    start, patch = 0x251DFC0, 5
    proof = []
    for rva, expected, meaning in [
        (start, '4c894c24204c894424188854241048894c2408', 'Unchanged entry argument home stores; first instruction is exactly5 bytes'),
        (0x251DFFB, '498b40100fb780e8000000', 'Read current action at snapshot+0x10, then u16 count at action+0xE8'),
        (0x251E00B, '6685c00f84050b0000', 'Zero count branches directly to pure epilogue0x251EB19'),
        (0x251EB19, '4881c438030000415f415e415d415c5f5e5b5dc3', 'Restore stack/registers and return; no game effects'),
        (0x251DF0C, 'c5f81011c5f811542430c5fb104110c5fb11442440', 'Snapshot copies layer offsets0/8/0x10 to stack+0x30'),
        (0x251DFA3, '4c8d4424300fb6d3488bcfe80d000000', 'Caller sets R8=snapshot,DL=layer,RCX=CC and calls entry'),
        (0x251DFB3, 'e9d1fdffff', 'Caller immediately continues layer loop; does not consume return value'),
        (0x944F64, 'e8e78cbd01', 'CharacterControl Update first calls parent0x251DC50'),
        (0x9453F8, 'e85388bd01', 'CharacterControl Update second calls parent0x251DC50'),
    ]:
        off = pe.rva_to_off(rva)
        code = bytes.fromhex(expected)
        assert pe.data[off:off + len(code)] == code, hex(rva)
        proof.append(dict(rva=hex(rva), bytes=expected, meaning=meaning))
    fn = ImageScanner(pe).function(start)
    assert fn['begin'] == start and fn['offset'] == 0
    incoming = []
    # Conservative raw scan: a potential branch within data is included too.
    for section in pe.sections:
        if not section.exec:
            continue
        raw = pe.data[section.raw_ptr:section.raw_ptr + section.raw_size]
        for m in re.finditer(rb'[\xe8\xe9]|\x0f[\x80-\x8f]', raw):
            pos = m.start()
            size = len(m.group()) + 4
            if pos + size > len(raw):
                continue
            target = section.va + pos + size + struct.unpack_from('<i', raw, pos + size - 4)[0]
            if start <= target < start + patch:
                incoming.append(dict(source=hex(section.va + pos), target=hex(target), kind='rel32'))
        lo, hi = max(0, start-section.va-130), min(len(raw)-1, start-section.va+patch+130)
        for pos in range(lo, hi):
            if raw[pos] not in (0xEB, 0xE0, 0xE1, 0xE2, 0xE3) and not 0x70 <= raw[pos] <= 0x7F:
                continue
            target = section.va + pos + 2 + struct.unpack_from('<b', raw, pos+1)[0]
            if start <= target < start+patch:
                incoming.append(dict(source=hex(section.va+pos), target=hex(target), kind='rel8_potential'))
    interior = [r for r in incoming if int(r['target'], 16) != start]
    assert not interior, interior
    result = dict(build='1.0.0.3527', exe_sha256=digest, old_entry='0x232D5C0', entry=hex(start),
                  entry_patch_bytes=5, entry_head=proof[0]['bytes'], function_range=fn,
                  old_read='0x232D5FB', read='0x251DFFB', old_crash='0x232D5FF', crash='0x251DFFF',
                  old_action_count_offset='0xE4', action_count_offset='0xE8',
                  calling_convention={'RCX': 'CharacterControl*', 'DL': 'layer index',
                                      'R8': 'stack snapshot of layer+0/+8/+0x10', 'R9': 'second snapshot'},
                  fields={'cc_actor': '0x08', 'cc_layer0': '0x88', 'cc_layer1': '0x90',
                          'layer_current': '0x10', 'layer_playback': '0x18', 'layer_chart': '0x118'},
                  entry_return_semantics='No game effects precede count==0 branch. Zero count goes directly to epilogue; caller ignores return value. At untouched entry a bare ret skips the same event-processing body.',
                  direct_incoming=incoming, interior_incoming=interior,
                  evidence=proof, runtime_validated=False,
                  limitation='Static mapping only. Live bytes still require existing runtime signature checks. Does not prove every diagnostic actor flag offset.')
    out = ROOT / 'tools/porting/data/null_consumer_3527.json'
    out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(output=str(out), direct_incoming=incoming, assertions=len(proof))))


if __name__ == '__main__':
    main()
