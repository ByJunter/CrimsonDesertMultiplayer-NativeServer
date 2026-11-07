"""Read-only, build-pinned audit of native field loading cycles in a capture.

A player object existing in the world is not proof that loading has finished.
Track the addressed preload milestone and the subsequent completion request
and reply separately for every MoveFieldAck, including the initial park field.
"""
import argparse
import json
from pathlib import Path
import struct

from check_capture_3527 import REC


FOLLOW = {
    'MoveFieldAck', 'MoveFieldCompleteReq', 'MoveFieldCompleteAck',
    'MoveFieldCompletePostAck', 'PlayAllPreLoadSequencerAck',
    'LoadingCompleteReq', 'LoadingCompleteAck', 'LoadingFieldDataAck',
    'GameLoadingStartReq', 'PreLoadSequencerReq',
}


def audit(data, names):
    if len(data) < 12 or struct.unpack_from('<4sII', data) != (b'CDPK', 2, 0x6AEA07A6):
        raise ValueError('Expected a complete CDPK v2 header from game build 3527')
    pos, trips, events = 12, [], []
    while pos + REC.size <= len(data):
        t, direction, _, size, copied, _, session, thread = REC.unpack_from(data, pos)
        end = pos + REC.size + copied
        if end > len(data):
            break  # A live recorder may be appending its final record.
        packet = data[pos + REC.size:end]
        pos = end
        if len(packet) < 5:
            continue
        opcode, _, length = struct.unpack_from('<HbH', packet)
        name = names.get(opcode)
        if name not in FOLLOW or copied != size or length != len(packet) - 5:
            continue
        payload = packet[5:]
        event = dict(time_us=t, direction=direction, name=name,
                     payload_hex=payload.hex(), thread=thread, session=session)
        events.append(event)
        if name == 'MoveFieldAck' and direction == 2:
            if len(payload) < 17 or (len(payload) - 17) % 56:
                raise ValueError('Unknown 3527 MoveFieldAck layout')
            actor, field, owner = struct.unpack_from('<III', payload, 1)
            trips.append(dict(actor=f'0x{actor:X}', field=field,
                              client_visible_owner=f'0x{owner:X}',
                              events=[], preload_milestone=False,
                              completion_requested=False, completion_replied=False))
        if not trips:
            continue
        trip = trips[-1]
        trip['events'].append(event)
        if name in ('PlayAllPreLoadSequencerAck', 'LoadingCompleteAck') and direction == 2:
            if len(payload) >= 4 and struct.unpack_from('<I', payload)[0] == int(trip['actor'], 16):
                trip['preload_milestone' if name == 'PlayAllPreLoadSequencerAck'
                     else 'completion_replied'] = True
        if name == 'LoadingCompleteReq' and direction == 1:
            trip['completion_requested'] = True
    for trip in trips:
        trip['protocol_complete'] = all(trip[k] for k in (
            'preload_milestone', 'completion_requested', 'completion_replied'))
        trip['elapsed_observed_us'] = trip['events'][-1]['time_us'] - trip['events'][0]['time_us']
    return dict(parsed_bytes=pos, partial_tail_bytes=len(data) - pos,
                trips=trips, events=events,
                limitation='Protocol completion does not prove UI control or movement.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    mapping = json.loads((Path(__file__).parent / 'data/opcode_mapping_2976_3527.json').read_text())
    names = {row['new']: row['name'] for row in mapping['opcode_mapping']}
    names.update({row['new']: row['name'] for row in mapping['added_opcodes']})
    report = audit(args.capture.read_bytes(), names)
    report['capture'] = str(args.capture.resolve())
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps([{k: v for k, v in trip.items() if k != 'events'}
                      for trip in report['trips']], indent=2))


if __name__ == '__main__':
    main()
