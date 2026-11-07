"""PROCESS_VM_READ-only inspection of the verified 3527 loading gates."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path

from live_reader_check import Reader
import read_coop_state_3527 as coop


def snapshot(reader):
    c = coop.client_context(reader)
    p = reader.ptr(c['world'] + 0x60)
    if not p:
        raise ValueError('No field-change controller')
    state = reader.unpack(p + 0x15C, '<B')[0]
    field_holder = reader.ptr(p + 0xC0)
    q = reader.ptr(field_holder) if field_holder else 0  # 856090 -> 856097
    if q:
        coop.vtable(reader, q, 0x594BA50, 'ClientField')
    owner = reader.unpack(q + 0x180, '<I')[0] if q else 0
    actor = c['local']
    controller = reader.unpack(actor + 0x90, '<I')[0] if actor else 0
    # AAA7A0 resolves Q.owner -> that user's main actor -> sequencer manager.
    # The guest's field owner alias is checked before using its local actor.
    manager = 0
    if actor and owner == controller:
        component = reader.ptr(reader.ptr(actor + 0x68) + 0xA8)
        manager = reader.ptr(component + 0x18) if component else 0
    wait_manager = reader.ptr(p + 0xB8)
    table = reader.ptr(wait_manager + 8)
    if table != c['table']:
        raise ValueError('Loading and live client actor tables differ')
    count = reader.unpack(table + 0x80, '<I')[0]
    if count > 4096:
        raise ValueError(f'Implausible loading wait count {count}')
    keys = reader.ptr(table + 0x78)
    entries = []
    for i in range(count):
        key = reader.unpack(keys + i * 4, '<I')[0]
        address, _ = coop.client_find(reader, c, key)
        item = dict(key=coop.hx(key), present=bool(address), ready=True)
        if address:
            method = reader.ptr(reader.ptr(address) + 0x1D8) - coop.BASE
            if method != 0x5B3420:
                raise ValueError(f'Unknown readiness method {coop.hx(method)} for {coop.hx(key)}')
            c2 = reader.unpack(address + 0xC2, '<B')[0]
            cf = reader.unpack(address + 0xCF, '<B')[0]
            item.update(c2=c2, cf=cf, ready=bool(c2 and cf))
        entries.append(item)
    return dict(state=coop.hx(state), idle=state == 0x18, field=coop.hx(q),
                owner=coop.hx(owner), local_controller=coop.hx(controller), local_actor=coop.hx(c['local_key']),
                owner_matches_local=bool(owner and owner == controller), sequencer_manager=coop.hx(manager),
                preload_marker=reader.unpack(manager + 0x259, '<B')[0] if manager else None,
                pending_preload=reader.unpack(manager + 0x1C0, '<I')[0] if manager else None,
                wait_count=count, wait_entries=entries,
                wait_blockers=[e['key'] for e in entries if not e['ready']])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pid', type=int, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    sha = hashlib.sha256(coop.DEFAULT_EXE.read_bytes()).hexdigest()
    reader = Reader(args.pid)
    try:
        identity = coop.verify_identity(reader, args.pid, coop.DEFAULT_EXE, sha)
        report = dict(pid=args.pid, captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                      access='PROCESS_VM_READ only', identity=identity, loading=snapshot(reader))
    finally:
        reader.close()
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report['loading'].items() if k != 'wait_entries'}, indent=2))


if __name__ == '__main__':
    main()
