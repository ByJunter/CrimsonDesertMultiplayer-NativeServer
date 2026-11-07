"""Reproduce static build3527 title/save-refresh ABI evidence; never calls game code."""
import hashlib
import json
import struct

import payload_disasm
from signature_map import Pe, ROOT


def main():
    pe = Pe((ROOT.parent / 'bin64/CrimsonDesert.exe').read_bytes())
    digest = hashlib.sha256(pe.data).hexdigest()
    assert digest == 'dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841'

    def raw(rva, n):
        off = pe.rva_to_off(rva)
        return pe.data[off:off+n]

    proof = []
    for rva, expected, meaning in [
        (0x72B3F8, '488b0df12aa906488b8988000000', 'Play loads WorldSystem then save manager at+0x88 into RCX'),
        (0x72B406, 'e8757a2801', 'Play calls latest-slot helper0x19B2E80'),
        (0x72B40E, '48b9ffffffffffffff7f', 'Play uses character number0x7FFFFFFFFFFFFFFF'),
        (0x72B42E, '488b89d0000000e8f6742800', 'Play sends character login using WorldSystem+0xD0'),
        (0x19B2080, 'e93b29d909', 'Refresh entry thunk jumps to0xB7449C0'),
        (0xB7449DB, '4889ce488b59504883c140', 'Refresh takes manager as sole RCX argument, reads storage+0x50 and clears table+0x40'),
        (0xB744A24, '488b7b388b4340488d2cc7', 'Refresh reads storage folders pointer+0x38,count+0x40, pointer stride8'),
        (0xB7509C2, '486bc878', 'Native table clearing uses entry stride0x78'),
        (0x8F494BA, '488b8888000000488b51408b41484c6bc878', 'Title availability predicate reads manager+0x40 slots,+0x48 count, stride0x78'),
        (0xB747FBF, '498b42084839c5', 'Latest-slot selection reads signed timestamp atentry+8'),
        (0xB747FCD, '4983c278', 'Latest-slot selection advances0x78 bytes'),
        (0xB747F80, '4d8b4268418b4270', 'Latest-slot selection checks appended u32 vector atentry+0x68 pointer,+0x70 count'),
        (0x2CABDBB, '488b05362651044c8b7848', 'Native refresh request uses ServerActorLookup0x71BE3F8+0x48'),
        (0x2CABDDE, 'ff5008', 'Lock vf1 before busy check'),
        (0x2CABDE2, '45387760750c41c6476001', 'Busy byte atmanager+0x60; set only if clear'),
        (0x2CABDFA, 'ff5010', 'Unlock vf2 before refresh'),
        (0x2CABE0F, '488b4948e86862d0fe', 'Refresh sole RCX argument is save manager'),
        (0x2CABE26, 'ff50089041c6476000', 'Relock then clear busy byte'),
        (0x2CABE35, 'ff5010', 'Unlock after clearing busy'),
    ]:
        code = bytes.fromhex(expected)
        assert raw(rva, len(code)) == code, hex(rva)
        proof.append(dict(rva=hex(rva), bytes=expected, meaning=meaning))
    assert 0x72B3F8 + 7 + struct.unpack('<i', raw(0x72B3FB, 4))[0] == 0x71BDEF0
    result = dict(build='1.0.0.3527', exe_sha256=digest,
        title_play=dict(old_function='0x65DB90', function='0x72B350', old_read='0x65DC3E', read='0x72B3F8',
                        signature=raw(0x72B3F8, 14).hex(' '), world_system_slot='0x71BDEF0', save_manager_offset='0x88',
                        latest_slot_helper='0x19B2E80->0xB747F20', no_valid_slots_predicate='0x8F494B0'),
        refresh=dict(old_entry='0x1870C20', entry='0x19B2080', body='0xB7449C0', head=raw(0x19B2080,5).hex(' '),
                     abi='void __fastcall(void* save_manager), sole argument in RCX',
                     native_sequence='0x2CABDBB', native_sequence_bytes=raw(0x2CABDBB,0x2CABE38-0x2CABDBB).hex(' '),
                     sequence='Lock vf1; if manager+0x60 nonzero, skip; else set1; unlock vf2; refresh(manager); lock vf1; set0; unlock vf2.',
                     lock_pointer_offset=0, busy_offset='0x60', storage_offset='0x50', folder_pointer_offset='0x38', folder_count_offset='0x40',
                     table_pointer_offset='0x40', table_count_offset='0x48',
                     behavior='Clear current table, iterate slot-prefixed folders, load lobby via0x19B3470 or fallback0x19B3DD0, finalize table via0xB754CA0.'),
        entries=dict(old_stride=0x68, stride=0x78, slot_offset=0, timestamp_offset=8, timestamp_type='signed i64',
                     eligible_slot_ranges=[[0,2],[100,108]],
                     appended_vector=dict(pointer_offset='0x68', count_offset='0x70', entry_width=4,
                                          semantics='Latest-slot helper additionally requires every appended ID to appear in a current global list; identifier meaning unproven.'),
                     recommendation='Build-aware diagnostic parser uses120-byte stride; preserve full native entries and leave selection to game.'),
        assertions=proof,
        limitations=['Static ABI proof and parent-provided live table snapshot do not establish complete save-file compatibility.',
                     'No refresh was called, no live process memory was modified, no runtime sources edited.'])
    live = ROOT / 'tools/porting/data/native_title_slots_3527.json'
    if live.exists():
        result['parent_live_evidence'] = json.loads(live.read_text())
    out = ROOT / 'tools/porting/data/title_refresh_3527.json'
    out.write_text(json.dumps(result, indent=2)+'\n')
    print(f'Wrote {out}; {len(proof)} byte assertions and RIP target passed')


if __name__ == '__main__':
    main()
