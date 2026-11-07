#!/usr/bin/env python3
"""Fail-closed, PROCESS_VM_READ-only co-op state inspector for build 3527.

The host's native ServerUserActor/main-character topology supplies the two
player keys.  Each key is then resolved independently in both clients' native
actor hash table.  Repeated samples provide exact position/movement evidence.
No hooks, injection, process writes, UI actions, or game calls are used.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import struct
import time

from live_reader_check import Reader


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT.parent / "bin64" / "CrimsonDesert.exe"
DEFAULT_OUT = Path(__file__).parent / "data" / "native_coop_state_3527.json"
BASE = 0x140000000
# 3527 and 3549 share every address this reads (docs/ESTADO-3527.md).
KNOWN_BUILDS = {
    (0x6AEA07A6, 0x13EB2000, "dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841"),
    (0x6AEB7D59, 0x13CFF000, "25f24e06a1f886e9310fd290f9462f66b480a5e7763d1fe156bfddaa8d1992b9"),
}

# Live/static verified 3527 globals and types.
WORLD_SLOT = 0x71BDEF0
SERVER_NET_SLOT = 0x71BE3E0
SAM_SLOT = 0x71FC598
VT_CLIENT_MANAGER = 0x5944B38
VT_CLIENT_STATUS = 0x591A710
VT_SERVER_USER = 0x5EECCA8
VT_SERVER_STATUS = 0x5EE7610
VT_SERVER_FIELD = 0x5EF0908

# Client actor hash layout, independently validated against manager+0x50.
WORLD_MANAGER = 0x30
MANAGER_TABLE = 0x08
MANAGER_LOCAL = 0x50
TABLE_BUCKET_COUNT = 0x88
TABLE_BUCKETS = 0x98
TABLE_NODES = 0xA0
BUCKET_STRIDE = 0x100
BUCKET_MAX = (BUCKET_STRIDE - 8) // 8

# Actor/component layout.  TransformSync +0xB4 includes local xyz and i16
# sector x/z; +0xD0 is the optional parent transform used for mounted actors.
ACTOR_KEY = 0x60
ACTOR_COMPONENTS = 0x68
ACTOR_CONTROLLER = 0x90
COMP_STATUS = 0x20
COMP_TS = 0x1A0
STATUS_ACTOR = 0x08
STATUS_NUMBER_3527 = 0x90
TS_LOCAL = 0xB4
TS_PARENT = 0xD0

# Server topology.  The shifted 3527 field tail and TS FieldMover were
# verified in the live run and the current MoveField/AddActor paths.
SERVER_NET_SESSION = 0x18
SESSION_USER = 0x90
USER_KEY = 0x60
USER_MAIN = 0xD0
USER_CURRENT = 0xD8
USER_SESSION = 0x1F8
TS_FIELD = 0x508 + 0x10
FIELD_OPEN = 0x4A       # verified unchanged in the 3527 field-share branch
FIELD_OWNER = 0x180
FIELD_USERS = 0x2A8
FIELD_USER_COUNT = 0x2B0
FIELD_USER_ACTORS = 0x2B8
FIELD_KEY = 0x2D0
FIELD_LEVEL_ENTRY = 0x2D8
MAX_FIELD_USERS = 64


def hx(value: int | None) -> str | None:
    return None if value is None else f"0x{value:X}"


def finite(values) -> list[float]:
    out = [float(v) for v in values]
    if not all(math.isfinite(v) for v in out):
        raise ValueError(f"non-finite vector {out}")
    return out


def dist(a, b):
    return math.dist(a, b) if a is not None and b is not None else None


def pointer(r: Reader, address: int, name: str, nullable: bool = False) -> int:
    value = r.ptr(address)
    if not value and not nullable:
        raise ValueError(f"{name}: null pointer at {hx(address)}")
    return value


def vtable(r: Reader, obj: int, expected: int, name: str) -> int:
    observed = r.ptr(obj) - BASE
    if observed != expected:
        raise ValueError(f"{name}: vtable {hx(observed)}, expected {hx(expected)}")
    return observed


def verify_identity(r: Reader, pid: int, exe: Path, file_hash: str) -> dict:
    dos = r.read(BASE, 0x40)
    if dos[:2] != b"MZ":
        raise ValueError(f"PID {pid}: live image has no MZ header")
    nt_at = struct.unpack_from("<I", dos, 0x3C)[0]
    nt = r.read(BASE + nt_at, 0x108)
    if nt[:4] != b"PE\0\0":
        raise ValueError(f"PID {pid}: live image has no PE header")
    stamp = struct.unpack_from("<I", nt, 8)[0]
    image_size = struct.unpack_from("<I", nt, 0x50)[0]
    result = dict(pid=pid, base=hx(BASE), timestamp=hx(stamp),
                  image_size=hx(image_size), disk_exe=str(exe), disk_sha256=file_hash)
    if (stamp, image_size, file_hash) not in KNOWN_BUILDS:
        raise ValueError(f"PID {pid}: build identity mismatch: {result}")
    return result


def transform(r: Reader, ts: int) -> dict:
    # SectorPos: float xyz, i16 sx, i16 sz.
    x, y, z, sx, sz = r.unpack(ts + TS_LOCAL, "<3fhh")
    local = finite((x, y, z))
    # ParentXf: scale3, quat4, translation3, i16 sector x/z.
    raw = r.unpack(ts + TS_PARENT, "<10fhh")
    scale, quat, translation = finite(raw[:3]), finite(raw[3:7]), finite(raw[7:10])
    psx, psz = int(raw[10]), int(raw[11])
    lx, ly, lz = sx * 1000.0 + x, y, sz * 1000.0 + z
    identity = (scale == [1.0, 1.0, 1.0] and quat == [0.0, 0.0, 0.0, 1.0]
                and translation == [0.0, 0.0, 0.0] and psx == 0 and psz == 0)
    if identity:
        world = [lx, ly, lz]
    else:
        vx, vy, vz = lx * scale[0], ly * scale[1], lz * scale[2]
        qx, qy, qz, qw = quat
        cx, cy, cz = qy * vz - qz * vy, qz * vx - qx * vz, qx * vy - qy * vx
        ccx, ccy, ccz = qy * cz - qz * cy, qz * cx - qx * cz, qx * cy - qy * cx
        world = [psx * 1000.0 + translation[0] + vx + 2 * (qw * cx + ccx),
                 translation[1] + vy + 2 * (qw * cy + ccy),
                 psz * 1000.0 + translation[2] + vz + 2 * (qw * cz + ccz)]
        finite(world)
    return dict(address=hx(ts), local_position=local, sector=[sx, sz],
                world_position=world, parent_identity=identity,
                parent_translation=translation, parent_sector=[psx, psz])


def actor(r: Reader, address: int, expected_key: int, server: bool) -> dict:
    key = r.unpack(address + ACTOR_KEY, "<I")[0]
    if key != expected_key:
        raise ValueError(f"actor {hx(address)} key {hx(key)}, expected {hx(expected_key)}")
    components = pointer(r, address + ACTOR_COMPONENTS, "actor component table")
    ts = pointer(r, components + COMP_TS, "TransformSync")
    status = pointer(r, components + COMP_STATUS, "status component")
    status_vt = vtable(r, status, VT_SERVER_STATUS if server else VT_CLIENT_STATUS,
                       "server status" if server else "client status")
    back = r.ptr(status + STATUS_ACTOR)
    if back != address:
        raise ValueError(f"status actor back-pointer {hx(back)}, expected {hx(address)}")
    status_out = dict(address=hx(status), vtable_rva=hx(status_vt),
                      actor_back_pointer_matches=True)
    if server:
        number_ptr = pointer(r, status + STATUS_NUMBER_3527,
                             "server status character-number pointer")
        status_out.update(character_number_pointer=hx(number_ptr),
                          character_number=hx(r.unpack(number_ptr, "<Q")[0]))
    # Native first-action marker, verified by the 3527 writer at 0x251905A.
    # Missing state is distinct from a readable zero during actor creation.
    try:
        control = pointer(r, components + 0x40, "CharacterControl")
        if r.ptr(control + 8) != address:
            raise ValueError("CharacterControl actor back-pointer mismatch")
        restore = pointer(r, control + 0x40, "CharacterControl restore state")
        action_state = dict(control=hx(control), restore_state=hx(restore),
                            restored_byte=r.unpack(restore + 0x30, "<B")[0])
    except (OSError, ValueError, RuntimeError) as error:
        action_state = dict(unavailable=str(error))
    return dict(address=hx(address), vtable_rva=hx(r.ptr(address) - BASE), key=hx(key),
                controller=hx(r.unpack(address + ACTOR_CONTROLLER, "<I")[0]),
                components=hx(components), transform=transform(r, ts), status=status_out,
                action_state=action_state)


def client_context(r: Reader) -> dict:
    world = pointer(r, BASE + WORLD_SLOT, "WorldSystem")
    manager = pointer(r, world + WORLD_MANAGER, "ClientActorManager")
    vtable(r, manager, VT_CLIENT_MANAGER, "ClientActorManager")
    table = pointer(r, manager + MANAGER_TABLE, "client actor table")
    count = r.unpack(table + TABLE_BUCKET_COUNT, "<I")[0]
    if not 0 < count <= 1 << 24:
        raise ValueError(f"client actor table bucket count {count}")
    local = pointer(r, manager + MANAGER_LOCAL, "controlled client actor", True)
    return dict(world=world, manager=manager, table=table, bucket_count=count,
                buckets=pointer(r, table + TABLE_BUCKETS, "client actor buckets"),
                nodes=pointer(r, table + TABLE_NODES, "client actor nodes"), local=local,
                local_key=r.unpack(local + ACTOR_KEY, "<I")[0] if local else 0)


def client_find(r: Reader, c: dict, key: int) -> tuple[int, dict]:
    bucket_index = key % c["bucket_count"]
    bucket = c["buckets"] + bucket_index * BUCKET_STRIDE
    count = r.unpack(bucket, "<I")[0]
    if count > BUCKET_MAX:
        raise ValueError(f"client bucket {bucket_index} invalid count {count}")
    evidence = dict(bucket_index=bucket_index, bucket_entry_count=count,
                    bucket_entry=None, node_index=None)
    for i in range(count):
        entry_key, node_index = r.unpack(bucket + 8 + i * 8, "<II")
        if entry_key != key:
            continue
        node = pointer(r, c["nodes"] + node_index * 8, "client actor node")
        node_key = r.unpack(node + 4, "<I")[0]
        if node_key != key:
            raise ValueError(f"client actor node key {hx(node_key)}, expected {hx(key)}")
        found = pointer(r, node + 8, "client actor value")
        if r.unpack(found + ACTOR_KEY, "<I")[0] != key:
            raise ValueError(f"client actor value does not have exact key {hx(key)}")
        evidence.update(bucket_entry=i, node_index=node_index)
        return found, evidence
    return 0, evidence


def client_snapshot(r: Reader, keys: list[int]) -> dict:
    c = client_context(r)
    rows = {}
    for key in keys:
        address, lookup = client_find(r, c, key)
        rows[hx(key)] = dict(lookup=lookup, present=bool(address),
                             actor=actor(r, address, key, False) if address else None)
    return dict(world=hx(c["world"]), manager=hx(c["manager"]),
                manager_vtable_rva=hx(r.ptr(c["manager"]) - BASE),
                bucket_count=c["bucket_count"], local_actor=hx(c["local"]),
                local_key=hx(c["local_key"]), actors=rows)


def field_snapshot(r: Reader, address: int) -> dict:
    vtable(r, address, VT_SERVER_FIELD, "ServerField")
    count = r.unpack(address + FIELD_USER_COUNT, "<I")[0]
    if count > MAX_FIELD_USERS:
        raise ValueError(f"ServerField invalid user count {count}")
    data = pointer(r, address + FIELD_USERS, "ServerField user list", count == 0)
    entries, actor_sum = [], 0
    for i in range(count):
        user, actors, pad = r.unpack(data + i * 0x10, "<QII")
        if not user:
            raise ValueError(f"ServerField null user entry {i}")
        entries.append(dict(index=i, user=hx(user),
                            key=hx(r.unpack(user + USER_KEY, "<I")[0]),
                            actors=actors, pad=pad))
        actor_sum += actors
    stored = r.unpack(address + FIELD_USER_ACTORS, "<I")[0]
    return dict(address=hx(address), vtable_rva=hx(r.ptr(address) - BASE),
                open=r.unpack(address + FIELD_OPEN, "<B")[0],
                owner=hx(r.unpack(address + FIELD_OWNER, "<I")[0]),
                key=hx(r.unpack(address + FIELD_KEY, "<I")[0]),
                level_entry=hx(r.ptr(address + FIELD_LEVEL_ENTRY)), user_list=hx(data),
                user_count=count, user_actor_count=stored, listed_actor_sum=actor_sum,
                listed_actor_sum_matches=actor_sum == stored, users=entries)


def server_user(r: Reader, address: int, role: str) -> dict:
    vtable(r, address, VT_SERVER_USER, f"{role} ServerUserActor")
    key = r.unpack(address + USER_KEY, "<I")[0]
    main = pointer(r, address + USER_MAIN, f"{role} main character", True)
    current = pointer(r, address + USER_CURRENT, f"{role} current character", True)
    out = dict(role=role, address=hx(address), vtable_rva=hx(r.ptr(address) - BASE),
               key=hx(key), session=hx(r.ptr(address + USER_SESSION)),
               main_character=hx(main), current_character=hx(current),
               main_is_current=bool(main) and main == current, main=None, field=None)
    if main:
        main_key = r.unpack(main + ACTOR_KEY, "<I")[0]
        out["main"] = actor(r, main, main_key, True)
        ts = int(out["main"]["transform"]["address"], 16)
        out["field"] = hx(pointer(r, ts + TS_FIELD, f"{role} ServerField"))
    return out


def host_topology(r: Reader) -> dict:
    net = pointer(r, BASE + SERVER_NET_SLOT, "ServerNet")
    session1 = pointer(r, net + SERVER_NET_SESSION, "host session")
    host_ptr = pointer(r, session1 + SESSION_USER, "host ServerUserActor")
    host = server_user(r, host_ptr, "host")
    if not host["field"]:
        raise ValueError("host main character has no ServerField")
    field = field_snapshot(r, int(host["field"], 16))
    users = []
    for item in field["users"]:
        p = int(item["user"], 16)
        users.append(host if p == host_ptr else server_user(r, p, "guest"))
    if sum(u["role"] == "host" for u in users) != 1 or sum(u["role"] == "guest" for u in users) > 1:
        raise ValueError("field list is not one host and at most one guest")
    sam = pointer(r, BASE + SAM_SLOT, "ServerActorManager")
    return dict(server_net=hx(net), host_session=hx(session1),
                server_actor_manager=hx(sam), server_actor_count=r.unpack(sam + 0x9C, "<I")[0],
                field=field, users=users)


def keys_of(topology: dict) -> list[int]:
    return [int(u["main"]["key"], 16) for u in topology["users"] if u["main"]]


def take_sample(readers: dict[str, Reader], index: int) -> dict:
    server = host_topology(readers["host"])
    keys = keys_of(server)
    if len(keys) != len(set(keys)):
        raise ValueError(f"duplicate main-character keys {[hx(k) for k in keys]}")
    return dict(index=index, captured_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                host_server=server, host_client=client_snapshot(readers["host"], keys),
                guest_client=client_snapshot(readers["guest"], keys))


def client_position(sample: dict, side: str, key: int):
    row = sample[side]["actors"].get(hx(key))
    return row["actor"]["transform"]["world_position"] if row and row["actor"] else None


def server_position(sample: dict, key: int):
    for user in sample["host_server"]["users"]:
        if user["main"] and int(user["main"]["key"], 16) == key:
            return user["main"]["transform"]["world_position"]
    return None


def proof(samples: list[dict]) -> dict:
    first, last = samples[0], samples[-1]
    server, field = last["host_server"], last["host_server"]["field"]
    users, keys = server["users"], keys_of(server)
    user_addresses = {u["address"] for u in users}
    listed_addresses = {u["user"] for u in field["users"]}
    host = next((u for u in users if u["role"] == "host"), None)
    fields = {u["field"] for u in users if u["main"]}
    checks = {
        "exactly_two_server_users": len(users) == 2,
        "two_distinct_player_character_keys": (len(keys) == 2 and len(set(keys)) == 2
                                                and all(k >> 28 == 0xA for k in keys)),
        "field_list_contains_both_users": len(users) == 2 and user_addresses == listed_addresses,
        "field_actor_sum_matches": field["listed_actor_sum_matches"],
        "both_main_characters_share_field": len(users) == 2 and len(fields) == 1,
        "field_owner_is_host": bool(host) and field["owner"] == host["key"],
        "host_client_resolves_both": len(keys) == 2 and all(last["host_client"]["actors"][hx(k)]["present"] for k in keys),
        "guest_client_resolves_both": len(keys) == 2 and all(last["guest_client"]["actors"][hx(k)]["present"] for k in keys),
    }
    characters, moved_any = {}, False
    for key in keys:
        hs0, hs1 = server_position(first, key), server_position(last, key)
        hc0, hc1 = client_position(first, "host_client", key), client_position(last, "host_client", key)
        gc0, gc1 = client_position(first, "guest_client", key), client_position(last, "guest_client", key)
        moves = [dist(hs0, hs1), dist(hc0, hc1), dist(gc0, gc1)]
        moved = all(value is not None and value >= 0.05 for value in moves)
        moved_any |= moved
        characters[hx(key)] = dict(server_to_host_client_metres=dist(hs1, hc1),
                                   server_to_guest_client_metres=dist(hs1, gc1),
                                   host_to_guest_client_metres=dist(hc1, gc1),
                                   server_displacement_metres=moves[0],
                                   host_client_displacement_metres=moves[1],
                                   guest_client_displacement_metres=moves[2],
                                   movement_seen_in_all_three_views=moved)
    return dict(structural_checks=checks,
                native_visibility_proven=checks["host_client_resolves_both"] and checks["guest_client_resolves_both"],
                shared_server_field_proven=all(checks[k] for k in (
                    "exactly_two_server_users", "field_list_contains_both_users", "field_actor_sum_matches",
                    "both_main_characters_share_field", "field_owner_is_host")),
                movement_crossproof_observed=moved_any,
                movement_note=("At least one character moved >= 0.05 m in all three native views."
                               if moved_any else "No character crossed 0.05 m in all three views during this window; not a failure."),
                characters=characters)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--host-pid", type=int, required=True)
    p.add_argument("--guest-pid", type=int, required=True)
    p.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    p.add_argument("--out", type=Path, default=DEFAULT_OUT)
    p.add_argument("--samples", type=int, default=3)
    p.add_argument("--interval", type=float, default=1.0)
    args = p.parse_args()
    if not 1 <= args.samples <= 120:
        p.error("--samples must be 1..120")
    if not 0 <= args.interval <= 30:
        p.error("--interval must be 0..30 seconds")
    if args.host_pid == args.guest_pid:
        p.error("host and guest PIDs must differ")
    report = dict(status="fail", captured_utc=dt.datetime.now(dt.timezone.utc).isoformat(),
                  access="PROCESS_VM_READ only", build="1.0.0.3527",
                  host_pid=args.host_pid, guest_pid=args.guest_pid,
                  requested_samples=args.samples, sample_interval_seconds=args.interval,
                  identities={}, samples=[], proof=None, errors=[])
    readers = {}
    try:
        file_hash = hashlib.sha256(args.exe.read_bytes()).hexdigest()
        readers = {"host": Reader(args.host_pid), "guest": Reader(args.guest_pid)}
        report["identities"] = {
            "host": verify_identity(readers["host"], args.host_pid, args.exe, file_hash),
            "guest": verify_identity(readers["guest"], args.guest_pid, args.exe, file_hash),
        }
        for i in range(args.samples):
            report["samples"].append(take_sample(readers, i))
            if i + 1 < args.samples and args.interval:
                time.sleep(args.interval)
        report["proof"] = proof(report["samples"])
        report["status"] = "pass" if all(report["proof"]["structural_checks"].values()) else "fail"
    except (OSError, ValueError, struct.error) as error:
        report["errors"].append(str(error))
    finally:
        for reader in readers.values():
            reader.close()
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(status=report["status"], samples=len(report["samples"]),
                          proof=report["proof"], errors=report["errors"], out=str(args.out)), indent=2))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
