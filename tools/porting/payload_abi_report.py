"""Emit the bounded, manually reviewed native payload ABI evidence report.

Addresses and field names below were reviewed from payload_*_3527.txt using
payload_disasm.py. This script checks executable identity and key evidence
 instructions; it does not execute the game. An optional capture audit corroborates
 the narrowly specified player actor tail, not the complete multiplayer ABI.
"""
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/scripts'))
from rtti_dump import Pe


def fields(sequence):
    offset = 0
    result = []
    for name, size in sequence:
        result.append({'name': name, 'offset': offset, 'size': size})
        offset += size
    return result


def main():
    pe = Pe((ROOT.parent / 'bin64/CrimsonDesert.exe').read_bytes())
    digest = hashlib.sha256(pe.data).hexdigest()
    assert digest == 'dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841'
    assert pe.u64(0x5ABF6D8 + 0x28) - pe.image_base == 0x1602740
    proof = []
    for address, expected, meaning in [
        (0x1602760, '498b4120488d52010fb60c01884aff41ff412c', 'Read source[stream.data + cursor], store one byte, increment cursor'),
        (0x2639535, 'e8d626e5fe', 'MoveData first consumes Path via 0x148BC10'),
        (0x263954C, 'e83f9cfcfe', 'MoveData next consumes Transform via 0x1603190'),
        (0x2639563, 'e8489bfcfe', 'MoveData next consumes quaternion via 0x16030B0'),
        (0x26395CC, '41b804000000', 'Additional four-byte field after timestamp in MoveData'),
        (0x263BE75, 'ff5028', 'MoveField entry reads a u32 after its Transform'),
        (0x263BE94, 'ff5028', 'MoveField entry reads another u32 after its Transform'),
        (0xC9E5A5, '41b804000000488d55c0', 'LoadingFieldDataAck fourth prefix field is u32 at wire offset9'),
        (0xC9E6C2, '486bd038', 'LoadingFieldDataAck entry destination stride56'),
        (0xC9E6D0, 'e81bd79901', 'LoadingFieldDataAck consumes shared56-byte entry reader0x263BDF0'),
        (0xC9E6D9, '0fb746038b4dac3bc872dc', 'Continue entries while stream cursor is below payload length'),
        (0x236C0B4, 'e8875112ff', 'TransformSync consumes its separate104-byte path via0x1491240'),
        (0x236C2C2, 'e8697f3cff', 'Final TransformSync list consumes u32 count then12*n via0x1734230'),
        (0x1734259, 'ff5028', 'TransformSync final list reads its four-byte count'),
        (0x17342A8, '41b80c000000', 'TransformSync final list entries each consume12 bytes'),
        (0x2371D4B, '41b80c000000', 'Navigation component consumes12 bytes'),
        (0xB27F1B2, '41b802000000', 'Hierarchy component consumes u16 child count after1+4+1 prefix'),
    ]:
        pos = pe.rva_to_off(address)
        code = bytes.fromhex(expected)
        assert pe.data[pos:pos + len(code)] == code, hex(address)
        proof.append({'rva': hex(address), 'bytes': expected, 'meaning': meaning})

    move_data = fields([
        ('path', 136), ('secondary_transform', 40), ('rotation_xyzw', 16), ('world_position', 12),
        ('auxiliary_u32', 4), ('timestamp_u64', 8), ('additional_u32_semantics_unproven', 4),
        ('flag0_u8', 1), ('flag1_u8', 1),
    ])
    path = fields([('local_transform', 40), ('vector0', 12), ('vector1', 12), ('parent_u64', 8),
                   ('world_transform', 40), ('vector2', 12), ('vector3', 12)])
    move_req = fields([('actor_key', 4), ('move_data', 222), ('tail_vec3', 12), ('history_10x28', 280)])
    move_ack = fields([('actor_key', 4), ('move_data', 222), ('server_flag', 1), ('tail_vec3', 12)])
    action_b = fields([('move_data', 222), ('aux0', 4), ('aux1_target', 4), ('vec0', 12), ('vec1', 12),
                       ('aux2', 4), ('nine_u32', 36), ('two_u8', 2), ('history_10x28', 280),
                       ('u64', 8), ('two_final_u8', 2)])
    result = {
        'source_build': '1.0.0.2976', 'target_build': '1.0.0.3527', 'exe_sha256': digest,
        'scope': 'Critical native login/movement payloads only; outer opcode codec remains separate.',
        'method': 'Sequential stream reads in current named RTTI handlers, recursively inspected helper readers; old layouts from repository docs and source.',
        'old_executable_available': False,
        'live_capture_verified': 'Player ActorLoadData tail only, if actor_tail_capture_3527.json is present; this is not multiplayer validation.',
        'offset_convention': 'Zero-based bytes relative to payload start unless explicitly described otherwise; add5 for whole ordinary packet.',
        'stream_proof': {'vtable_rva': '0x5ABF6D8', 'read_slot': '0x28', 'read_rva': '0x1602740',
                         'data_offset': '0x20', 'length_offset': '0x28', 'cursor_offset': '0x2C',
                         'verified_instruction_bytes': proof},
        'shared_layouts': {
            'Transform': {'wire_size': 40, 'status': 'wire_order_preserved',
                          'fields': fields([('position', 12), ('rotation_xyzw', 16), ('scale', 12)]),
                          'decoder_rva': '0x1603190',
                          'destination_note': 'Current native struct stores scale at0, quat at12, position at28. Reader consumes destination+28, +12, +0 in that order. Constructor0x26392D0 copies three1.0f values from0x58F28B8 into scale; native struct ordering is NOT wire ordering.'},
            'Path': {'wire_size': 136, 'status': 'wire_order_preserved', 'fields': path,
                     'decoder_rva': '0x148BC10', 'subreader_rva': '0x148BB20'},
            'MoveData': {'old_wire_size': 218, 'new_wire_size': 222, 'status': 'confirmed_incompatible',
                         'fields': move_data, 'decoder_rva': '0x2639500',
                         'old_order': 'position12,quat16,aux4,Transform40,Path136,flag1,timestamp8,flag1',
                         'semantic_limit': 'Extra u32 meaning unknown. Preserve exactly; do not synthesize zero by assumption. Timestamp/flag semantic labels follow old analogous data and need current application-level validation.'},
            'StartActionA': {'wire_size': 69, 'status': 'read_width_sequence_preserved',
                             'decoder_rva': '0x2637790', 'nested_8byte_reader': '0x1C2B350',
                             'note': 'Read4+4+4x2+1+2x(3x2+1)+1+4x4+8+9x1+4. Semantic enum/hash compatibility not established.'},
            'StartActionB': {'old_wire_size': 582, 'new_wire_size': 586, 'status': 'confirmed_incompatible',
                             'decoder_rva': '0x2637D90', 'fields': action_b},
        },
        'messages': [
            {'name': 'MoveActorReq', 'logical': 2556, 'native': 2833, 'handler_rva': '0x2BEC5C0',
             'old_payload_size': 514, 'new_payload_size': 518, 'status': 'confirmed_incompatible', 'fields': move_req},
            {'name': 'MoveActorAck', 'logical': 2697, 'native': 3094, 'handler_rva': '0xC702F0',
             'old_payload_size': 235, 'new_payload_size': 239, 'status': 'confirmed_incompatible', 'fields': move_ack},
            {'name': 'StartActionReq', 'logical': 2223, 'native': 2202, 'handler_rva': '0x2BE08C0',
             'old_payload_size': 725, 'new_payload_size': 729, 'status': 'confirmed_incompatible',
             'fields': fields([('actor_key', 4), ('A0', 69), ('A1', 69), ('B', 586), ('flag', 1)])},
            {'name': 'StartActionAck', 'logical': 2991, 'native': 2552, 'handler_rva': '0xC64250',
             'old_payload_size': 725, 'new_payload_size': 729, 'status': 'confirmed_incompatible',
             'fields': fields([('actor_key', 4), ('A0', 69), ('A1', 69), ('flag', 1), ('B', 586)])},
            {'name': 'LoginUserToFieldServerReq', 'logical': 2490, 'native': 2535, 'handler_rva': '0x2CAA830',
             'status': 'audited_prefix_preserved', 'fields': fields([('account', 8), ('u32', 4), ('u8', 1), ('wstring_char_count', 4)]),
             'variable_tail': 'UTF16 count*2+2, including null terminator; count<=1500. Empty-name payload19bytes matchesold.',
             'evidence_sites': ['0x2CAA8D2', '0x2CAA917', '0x2CAA947', '0x2CAA974', '0x2CAA99B', '0x2CAA9F8'],
             'mod_use': 'server.cpp account rewrite atpayload0 remains correctly placed.'},
            {'name': 'LoginCharacterToFieldServerReq', 'logical': 3093, 'native': 2733, 'handler_rva': '0x2C047B0',
             'status': 'audited_prefix_preserved',
             'fields': fields([('unused_prefix', 1), ('character_number', 8), ('save_slot', 4), ('unknown_u32', 4)]),
             'variable_tail': 'Remaining payload consumed as UTF16 without count prefix. Field semantics based on old documentation and corresponding call argument flow.',
             'evidence_sites': ['0x2C04870', '0x2C048BF', '0x2C048F1', '0x2C0491B', '0x2C0493D', '0x2C04B37'],
             'mod_use': 'character_number@1 and save_slot@9 matchexisting rewriter.'},
            {'name': 'MainPlayerDataStartAck', 'logical': 2756, 'native': 2524, 'handler_rva': '0xC7EB20',
             'status': 'audited_prefix_preserved', 'payload_size': 25,
             'fields': fields([('actor_key', 4), ('u32', 4), ('u8', 1), ('character_number', 8), ('u64', 8)]),
             'mod_use': 'Guest local-character lookup atpayload0 remains correct; final8bytes semantics unproven.'},
            {'name': 'MoveFieldAck', 'logical': 2174, 'native': 2653, 'handler_rva': '0xC9C2D0',
             'status': 'confirmed_incompatible_entry_extension', 'old_single_entry_payload_size': 65,
             'new_single_entry_payload_size': 73,
             'prefix_fields': fields([('u8', 1), ('actor_key', 4), ('move_type', 4), ('user_key', 4), ('opaque_u32', 4)]),
             'entry_fields': fields([('u32', 4), ('actor_key', 4), ('transform', 40), ('new_u32_0', 4), ('new_u32_1', 4)]),
             'entry_decoder_rva': '0x263BDF0',
             'entry_count_rule': 'No encoded count: read entries until stream cursor reaches payload length, requiring exact end. Count=(payload_size-17)/56.',
             'mod_use': 'Firstentryposition@25 unchanged; exact65-byte assumptions fail. Extra8bytes occur perentry, not one global trailer.'},
            {'name': 'LoadingFieldDataAck', 'logical': 2643, 'native': 2399, 'handler_rva': '0xC9E360',
             'status': 'confirmed_incompatible_entry_extension',
             'old_payload_size_formula': '20+48*n', 'new_payload_size_formula': '20+56*n',
             'prefix_fields': fields([('u8', 1), ('actor_key', 4), ('opaque_u32_0', 4), ('owner_user_key', 4),
                                      ('flag0', 1), ('flag1', 1), ('opaque_u32_1', 4), ('flag2', 1)]),
             'prefix_read_sites': ['0xC9E43E', '0xC9E4CB', '0xC9E56E', '0xC9E5A5',
                                   '0xC9E5D6', '0xC9E607', '0xC9E638', '0xC9E66F'],
             'entry_fields': fields([('u32', 4), ('actor_key', 4), ('transform', 40), ('new_u32_0', 4), ('new_u32_1', 4)]),
             'entry_decoder_rva': '0x263BDF0', 'entry_decoder_call_site': '0xC9E6D0',
             'entry_count_rule': 'No encoded count. Reserve capacity from remaining size divided by56; read entries until cursor==payload_len. Exact-end check0xC9E6E7. Count=(payload_size-20)/56.',
             'owner_argument_flow': 'Read4 into[rbp-0x40] at0xC9E5B3; pass its address asR8 at0xC9E818 to client field loader0x85B440 at0xC9E82B.',
             'mod_use': 'alias_owner writes payload+9 (whole ordinary packet+14), unchanged. Validate20+56*n and preserve all other bytes, especially both newu32 fields in each entry.'},
            {'name': 'LoadingCompleteAck', 'logical': 2762, 'native': 2190, 'handler_rva': '0xCA1350',
             'status': 'audited_field_widths_preserved', 'payload_size': 13,
             'fields': fields([('actor_key', 4), ('u8', 1), ('u64', 8)])},
            {'name': 'ActorLoadDataAck', 'logical': 2005, 'native': 2005, 'handler_rva': '0xDE8E60',
             'status': 'prefix_and_captured_type1_tail_confirmed_other_variants_unproven',
             'handler_jump_rva': '0xA072110', 'create_record_rva': '0x9A6BC0',
             'record_prefix': 'Payload has repeated u16recordLength; record reads type_u8 thenactor_key_u32. Non-user record continues owner_key_u32,index_u8,u64 before componentinformation.',
             'first_record_payload_offsets': {'type': 2, 'actor_key': 3, 'non_user_owner': 7, 'non_user_index': 11, 'non_user_u64': 12},
             'captured_type1_tail': {
                 'origin': 'End of the individual type1 record, excluding its preceding u16 length. NOT end of a multi-record payload.',
                 'position_from_end': 158, 'quaternion_from_end': 146, 'scale_from_end': 130,
                 'old_offsets_wrong_in_current_samples': {'position': 154, 'quaternion': 142},
                 'sample_count': 4, 'record_sizes': [16789, 16652],
                 'conditions': 'Observed type1 player records have zero entries in all three TS lists, hierarchy children and actor children. Do not extrapolate fixed offsets to nonzero lists or other actor types.',
                 'world_sample_position': [-5089.48388671875, 404.027099609375, -4711.26318359375],
                 'nearest_native_movement_distance_m': 0.15762033672093773,
                 'suffix_read_sequence': 'Transform40,parent_u64,Transform40,quat16; u32 n2 + n2*u16; u32; vec3; u32 n4 + n4*u32; u32 n12 + n12*vec3; Navigation vec3; Hierarchy u8,u32,u8,u16 nh,nh*u32; actor parent_u32,u16 na,na*u32.',
                 'distance_formula_under_this_component_order': '158+2*n2+4*n4+12*n12+4*nh+4*na',
                 'transform_sync_reader': '0x236BF20', 'transform_sync_path_reader': '0x1491240->0xAA1E950',
                 'path_note': 'This104-byte TS path differs from the136-byte MoveData Path. Captured position is its FIRST Transform.',
                 'final_ts_field': {'reader': '0x1734230', 'count_size': 4, 'entry_size': 12,
                                    'empty_count_from_record_end': 30},
                 'following_components': {'Navigation_reader': '0x2371D20', 'Hierarchy_reader': '0x1815120->0xB27F140'},
                 'evidence': 'actor_tail_capture_3527.json includes per-record exact suffix consumption and nearest same-actor movement. payload_actor_tail_helpers2_3527.txt and payload_hierarchy_3527.txt record native read widths.',
                 'historical_limit': 'Old executable absent; current final TS list is proven, but attributing the entire four-byte difference specifically to a newly introduced list is not proven.'},
             'unproven': ['all per-type nested lengths and owner/control/local flags used by load rewriting',
                          'complete CreateUser and field initialization semantics'],
             'reason': 'Actor information0x1928790->0xB4B7F80 iterates runtime component list and calls each vf+0x58. Current transform component0x236BF20 has variable lists; captures prove end158 only for the specified empty-list player samples.',
             'mod_use': 'Opaque native relay is unaffected. Legacy local_capture/remote_puppet end154/end142 rewriting is wrong for current player samples and needs a type/record/shape-aware parser before enabling.'},
        ],
        'movement_offset_comparison': {
            'origin': 'MoveActorReq/Ack payload start',
            'actor_key': {'old': 0, 'new': 0}, 'position': {'old': 4, 'new': 196},
            'quaternion': {'old': 16, 'new': 180}, 'auxiliary_u32': {'old': 32, 'new': 208},
            'secondary_transform': {'old': 36, 'new': 140}, 'path': {'old': 76, 'new': 4},
            'parent_u64': {'old': 140, 'new': 68}, 'path_world_position': {'old': 148, 'new': 76},
            'timestamp_u64': {'old': 213, 'new': 212}, 'extra_u32': {'old': None, 'new': 220},
            'first_flag': {'old': 212, 'new': 224}, 'second_flag': {'old': 221, 'new': 225},
            'req_tail': {'old': 222, 'new': 226}, 'req_history': {'old': 234, 'new': 238},
            'ack_server_flag': {'old': 222, 'new': 226}, 'ack_tail': {'old': 223, 'new': 227},
        },
        'affected_existing_code_examples': [
            'src/mod/session2/attach_follow_logic.h: fixed514/235/725 and move/action offsets',
            'src/mod/session2/horse_follow_logic.h: rider-derived MoveActorAck usesoldparent/timestamp',
            'src/mod/session2/combat_follow_logic.h: synthesizes235-byteMoveActorAck',
            'src/mod/session2/ride_filter_logic.h: exact725 andoldBparent/target offsets',
            'src/mod/session2/scene_diag_logic.h: exact725 andoldBtailflag',
            'src/mod/net/local_capture.cpp and remote_puppet.cpp: ActorLoadData end-relative offsets unverified',
        ],
        'adapter_recommendation': [
            'Keep same-build native payload bytes opaque across the tunnel and preserve outer opcode mapping.',
            'Introduce explicit build-aware typed movement views/builders for filters and synthetic packets; old2976fixtures stayold.',
            'Preserve the new MoveDatau32 and MoveFieldentryu32s verbatim when deriving packets; their default values are not verified.',
            'Do not globally shrink222-byteMoveData to218 or729-byteactions to725; that discards current fields and would require a separatelyversioned lossless envelope.',
            'Treat Scope fragments as opaque. This report does not authorize partial nested rewrites.',
        ],
        'source_references': ['docs/research/mirror-ack-formats.md sections1-2',
                              'docs/research/opcion-d-login.md sections4.2-4.4',
                              'docs/research/apariencia-estado-coop.md section3',
                              'src/mod/game/offsets.h kLoadPositionFromEnd/kLoadRotationFromEnd'],
        'limitations': ['Static payload read layout is not live multiplayer validation.',
                        'Only captured empty-list type1 actor-load tails are verified. Other actor-type component payloads and semantic enum/local-control flags need further evidence.',
                        'No game memory or runtime source was modified.'],
    }
    capture_path = ROOT / 'tools/porting/data/actor_tail_capture_3527.json'
    if capture_path.exists():
        capture = json.loads(capture_path.read_text())
        result['actor_tail_capture_evidence'] = {k: capture[k] for k in
            ('capture', 'captured_bytes', 'parsed_bytes', 'snapshot_sha256', 'types')}
    output = ROOT / 'tools/porting/data/payload_abi_3527.json'
    output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('Wrote ' + str(output))


if __name__ == '__main__':
    main()
