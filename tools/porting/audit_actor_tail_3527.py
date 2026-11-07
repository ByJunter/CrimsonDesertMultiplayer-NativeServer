"""Read complete native capture records and test actor-load transform tail candidates."""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import struct

REC = struct.Struct('<QBBHHHII')


def json_finite(value):
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {k: json_finite(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_finite(v) for v in value]
    return value


def parse_transform_tail(record, start):
    """Validate the audited TS Path -> Navigation -> Hierarchy -> actor suffix.

    This is a bounded candidate parser, not a universal component-order proof.
    start is the first Transform of 0x1491240 (NOT the separate 136-byte MoveData Path).
    """
    at = start
    result = {}

    def take(size):
        nonlocal at
        if size < 0 or at + size > len(record):
            raise ValueError('past record end')
        data = record[at:at + size]
        at += size
        return data

    def vector(name, width, count_width=4):
        count = int.from_bytes(take(count_width), 'little')
        take(count * width)
        result[name] = count

    try:
        take(40)
        result['path_parent'] = hex(int.from_bytes(take(8), 'little'))
        take(40 + 16)
        vector('ts_u16_count', 2)
        result['ts_u32'] = int.from_bytes(take(4), 'little')
        result['ts_vector'] = struct.unpack('<3f', take(12))
        vector('ts_u32_count', 4)
        vector('ts_vec3_count', 12)
        result['navigation'] = struct.unpack('<3f', take(12))
        result['hierarchy_u8'] = take(1)[0]
        result['hierarchy_u32'] = int.from_bytes(take(4), 'little')
        result['hierarchy_flag'] = take(1)[0]
        vector('hierarchy_children', 4, 2)
        result['actor_parent'] = hex(int.from_bytes(take(4), 'little'))
        vector('actor_children', 4, 2)
        result['exact_record_end'] = at == len(record)
        if not result['exact_record_end']:
            return None
        return result
    except (ValueError, struct.error):
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('capture', type=Path)
    ap.add_argument('--out', type=Path, default=Path(__file__).parent / 'data/actor_tail_capture_3527.json')
    args = ap.parse_args()
    data = args.capture.read_bytes()
    assert struct.unpack_from('<4sII', data) == (b'CDPK', 2, 0x6AEA07A6)
    at = 12
    loads, moves = [], defaultdict(list)
    while at + REC.size <= len(data):
        t, direction, flags, size, copied, pad, session, thread = REC.unpack_from(data, at)
        if at + REC.size + copied > len(data):
            break
        packet = data[at + REC.size:at + REC.size + copied]
        offset = at
        at += REC.size + copied
        if copied != size or len(packet) < 5:
            continue
        op, slot, length = struct.unpack_from('<HbH', packet)
        payload = packet[5:]
        if length != len(payload):
            continue
        if op in (2833, 3094) and length in (518, 239):
            key = struct.unpack_from('<I', payload)[0]
            moves[key].append((t, struct.unpack_from('<3f', payload, 196)))
        if op != 2005:
            continue
        pos = 0
        while pos + 2 <= len(payload):
            count = struct.unpack_from('<H', payload, pos)[0]
            if count < 5 or pos + 2 + count > len(payload):
                break
            record = payload[pos + 2:pos + 2 + count]
            entry = dict(t_us=t, capture_offset=offset, payload_record_offset=pos, type=record[0],
                         key=hex(struct.unpack_from('<I', record, 1)[0]), size=count)
            entry['_record'] = record
            if count >= 158:
                values = struct.unpack_from('<10f', record, count - 158)
                qnorm = sum(v*v for v in values[3:7])
                entry.update(position_end158=values[:3], rotation_end146=values[3:7], scale_end130=values[7:],
                             quaternion_norm2=qnorm,
                             valid_transform=all(math.isfinite(v) for v in values) and abs(qnorm - 1) < 0.02 and
                                 all(abs(v - 1) < 0.01 for v in values[7:]),
                             tail_hex=record[-180:].hex())
            loads.append(entry)
            pos += 2 + count
    for entry in loads:
        record = entry.pop('_record')
        candidates = moves.get(int(entry['key'], 16), [])
        if candidates and 'position_end158' in entry:
            t, xyz = min(candidates, key=lambda m: abs(m[0] - entry['t_us']))
            entry.update(nearest_move_dt_ms=(t-entry['t_us'])/1000, nearest_move_position=xyz,
                         nearest_move_distance=math.dist(entry['position_end158'], xyz))
            matches = []
            for off in range(max(5, len(record) - 512), len(record) - 39):
                v = struct.unpack_from('<10f', record, off)
                if not all(math.isfinite(x) for x in v):
                    continue
                if abs(sum(x*x for x in v[3:7]) - 1) > .02 or any(abs(x-1) > .01 for x in v[7:]):
                    continue
                distance = math.dist(v[:3], xyz)
                if distance < 2:
                    matches.append(dict(position_from_end=len(record)-off, position=v[:3],
                                        rotation=v[3:7], distance_to_nearest_move=distance,
                                        parsed_suffix=parse_transform_tail(record, off)))
            entry['near_movement_transform_candidates'] = matches
    summary = {}
    for kind in sorted({r['type'] for r in loads}):
        group = [r for r in loads if r['type'] == kind]
        summary[kind] = dict(count=len(group), valid_transform=sum(r.get('valid_transform', False) for r in group),
                             sizes=dict(Counter(r['size'] for r in group)),
                             matched_movement=sum('nearest_move_distance' in r for r in group),
                             candidate_end_offsets=dict(Counter(c['position_from_end'] for r in group
                                 for c in r.get('near_movement_transform_candidates', []))),
                             exact_suffix_end_offsets=dict(Counter(c['position_from_end'] for r in group
                                 for c in r.get('near_movement_transform_candidates', []) if c['parsed_suffix'])))
    result = dict(capture=str(args.capture), captured_bytes=len(data), parsed_bytes=at,
                  snapshot_sha256=hashlib.sha256(data).hexdigest(), types=summary, records=loads,
                  limitation='Candidate validation is evidence, not permission to assume every actor type has this tail layout.')
    args.out.write_text(json.dumps(json_finite(result), indent=2, allow_nan=False) + '\n')
    print(json.dumps(summary, indent=2))
    for entry in loads:
        if entry['key'] == '0xa0100001':
            print(json.dumps({k:v for k,v in entry.items() if k != 'tail_hex'}))
    print('Wrote', args.out)


if __name__ == '__main__':
    main()
