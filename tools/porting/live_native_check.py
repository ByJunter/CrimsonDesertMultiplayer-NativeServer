#!/usr/bin/env python3
"""Read-only live validation for build-3527 Troc and transport evidence.

The process handle requests PROCESS_VM_READ only.  Static evidence is tied to
the installed executable hash before any live address is trusted.
"""
from __future__ import annotations

import argparse
import ctypes as c
from ctypes import wintypes as w
import datetime as dt
import hashlib
import json
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT.parent / "bin64" / "CrimsonDesert.exe"
BASE = 0x140000000
STAMP_3527 = 0x6AEA07A6
IMAGE_SIZE_3527 = 0x13EB2000
SHA_3527 = "dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841"
# Unique live executable-image pointer to the RTTI-validated ServerActorManager.
# It is the 3527 counterpart of offsets_s2.h kSamSlot.  The distinct lookup
# global below owns a wrapper whose first field points to the same manager.
SAM_SLOT_3527 = 0x71FC598
VT_SERVER_ACTOR_MANAGER_3527 = 0x5EECE88
VT_SERVER_USER_ACTOR_3527 = 0x5EECCA8
VT_VIRTUAL_SYNC_SESSION_3527 = 0x5AC3C28


def number(value):
    if isinstance(value, int):
        return value
    return int(value, 0)


def hx(value):
    return None if value is None else f"0x{value:X}"


class Reader:
    PROCESS_VM_READ = 0x0010

    def __init__(self, pid: int):
        self.kernel = c.WinDLL("kernel32", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
        self.kernel.OpenProcess.restype = w.HANDLE
        self.kernel.ReadProcessMemory.argtypes = [
            w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)
        ]
        self.kernel.ReadProcessMemory.restype = w.BOOL
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        self.handle = self.kernel.OpenProcess(self.PROCESS_VM_READ, False, pid)
        if not self.handle:
            raise c.WinError(c.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def read(self, address: int, size: int) -> bytes:
        data = c.create_string_buffer(size)
        got = c.c_size_t()
        ok = self.kernel.ReadProcessMemory(
            self.handle, c.c_void_p(address), data, size, c.byref(got)
        )
        if not ok or got.value != size:
            raise OSError(c.get_last_error(), f"unreadable {hx(address)}+{hx(size)}")
        return data.raw

    def unpack(self, address: int, fmt: str):
        return struct.unpack(fmt, self.read(address, struct.calcsize(fmt)))

    def ptr(self, address: int) -> int:
        return self.unpack(address, "<Q")[0]


def validate_identity(reader: Reader, exe: Path, troc: dict, transport: dict) -> dict:
    file_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    dos = reader.read(BASE, 0x40)
    if dos[:2] != b"MZ":
        raise ValueError("live image has no MZ header")
    nt_off = struct.unpack_from("<I", dos, 0x3C)[0]
    nt = reader.read(BASE + nt_off, 0x108)
    if nt[:4] != b"PE\0\0":
        raise ValueError("live image has no PE header")
    stamp = struct.unpack_from("<I", nt, 8)[0]
    image_size = struct.unpack_from("<I", nt, 0x50)[0]
    expected_stamp = number(troc["timestamp"])
    identities = {
        "base": hx(BASE),
        "timestamp": hx(stamp),
        "image_size": hx(image_size),
        "file_sha256": file_hash,
        "troc_evidence_sha256": troc["sha256"],
        "transport_evidence_sha256": transport["sha256"],
    }
    if (stamp != STAMP_3527 or stamp != expected_stamp or
            image_size != IMAGE_SIZE_3527 or file_hash != SHA_3527 or
            troc["sha256"] != file_hash or transport["sha256"] != file_hash):
        raise ValueError(f"build identity mismatch: {identities}")
    return identities


def validate_singletons(reader: Reader, troc: dict) -> tuple[list[dict], list[dict]]:
    begin = number(troc["instance_range"]["begin"])
    end = number(troc["instance_range"]["end_exclusive"])
    memory = reader.read(BASE + begin, end - begin)
    checked = []
    mismatches = []
    fields = {
        "kind": (0x08, "<I"), "id": (0x0C, "<H"), "flag_10": (0x10, "<I"),
        "flags_14": (0x14, "<I"), "min_size": (0x18, "<H"),
        "max_size": (0x1A, "<H"), "flag_1c": (0x1C, "<I"),
        "flag_88": (0x88, "<B"),
    }
    for message in troc["messages"]:
        expected = message.get("new")
        if expected is None:
            mismatches.append({"name": message["name"], "error": "mapping has no unique singleton"})
            continue
        rva = number(expected["static_instance"])
        offset = rva - begin
        if offset < 0 or offset + 0x90 > len(memory):
            mismatches.append({"name": message["name"], "error": "singleton outside mapped range"})
            continue
        block = memory[offset:offset + 0x90]
        observed = {"vtable": struct.unpack_from("<Q", block)[0] - BASE}
        expected_values = {"vtable": number(expected["vtable"])}
        for name, (field_offset, fmt) in fields.items():
            observed[name] = struct.unpack_from(fmt, block, field_offset)[0]
            expected_values[name] = number(expected[name])
        # The RTTI-derived handler is vtable slot 2. This independently checks
        # that a matching-looking metadata block has the named runtime type.
        handler_expected = number(message["handler"]) if message.get("handler") else None
        handler_observed = None
        if observed["vtable"] == expected_values["vtable"] and handler_expected is not None:
            handler_observed = reader.ptr(BASE + observed["vtable"] + 16) - BASE
        bad = {key: {"expected": hx(value) if key == "vtable" else value,
                     "observed": hx(observed[key]) if key == "vtable" else observed[key]}
               for key, value in expected_values.items() if observed[key] != value}
        if handler_expected is not None and handler_observed != handler_expected:
            bad["handler_slot2"] = {"expected": hx(handler_expected), "observed": hx(handler_observed)}
        row = {
            "name": message["name"], "singleton_rva": hx(rva),
            "opcode": observed["id"], "kind": observed["kind"],
            "vtable_rva": hx(observed["vtable"]), "matches": not bad,
        }
        checked.append(row)
        if bad:
            mismatches.append({"name": message["name"], "singleton_rva": hx(rva), "fields": bad})
    return checked, mismatches


def validate_transport_code(reader: Reader, transport: dict) -> tuple[list[dict], list[dict]]:
    checked, mismatches = [], []
    for mapping in transport["mappings"]:
        expected_text = mapping.get("first_32_bytes")
        if not expected_text:
            continue
        expected = bytes.fromhex(expected_text)
        rva = number(mapping["new_rva"])
        observed = reader.read(BASE + rva, len(expected))
        match = observed == expected
        row = {"symbol": mapping["symbol"], "rva": hx(rva), "bytes": len(expected), "matches": match}
        checked.append(row)
        if not match:
            mismatches.append({**row, "expected": expected.hex(" "), "observed": observed.hex(" ")})
    return checked, mismatches


def validate_catalog_scan(reader: Reader, troc: dict) -> tuple[dict, list[dict]]:
    """Mirror build_profile.h validate_native_catalog(), including its skip."""
    begin = number(troc["instance_range"]["begin"])
    end = number(troc["instance_range"]["end_exclusive"])
    image_limit = IMAGE_SIZE_3527
    memory = reader.read(BASE + begin, end - begin)
    catalog = sorted(
        ({"native": number(m["new"]["id"]), "kind": number(m["new"]["kind"]), "name": m["name"]}
         for m in troc["messages"] if m.get("new")),
        key=lambda entry: entry["native"],
    )
    by_opcode = {entry["native"]: (index, entry) for index, entry in enumerate(catalog)}
    seen = set()
    failures = []
    rtti_candidates = 0
    troc_candidates = 0
    scanned_slots = 0
    addr = begin
    prefix = ".?AVTrocTr"
    while addr < end:
        scanned_slots += 1
        offset = addr - begin
        vt = struct.unpack_from("<Q", memory, offset)[0]
        if not (BASE + 8 <= vt < BASE + image_limit):
            addr += 8
            continue
        try:
            col = reader.ptr(vt - 8)
        except OSError:
            addr += 8
            continue
        if not (BASE <= col and col + 0x18 < BASE + image_limit):
            addr += 8
            continue
        try:
            signature = reader.unpack(col, "<I")[0]
            td = reader.unpack(col + 0x0C, "<I")[0]
        except OSError:
            addr += 8
            continue
        if signature != 1 or not td or td + 0x110 >= image_limit:
            addr += 8
            continue
        rtti_candidates += 1
        try:
            raw_name = reader.read(BASE + td + 0x10, 255)
        except OSError as error:
            failures.append({"rva": hx(addr), "error": f"RTTI name unreadable: {error}"})
            break
        decorated = raw_name.split(b"\0", 1)[0].decode("ascii", errors="replace")
        if not decorated.startswith(prefix):
            addr += 8
            continue
        troc_candidates += 1
        at = decorated.find("@", len(prefix))
        if at < 0 or decorated[at:] != "@pa@@":
            failures.append({"rva": hx(addr), "error": "malformed Troc RTTI", "name": decorated})
            break
        kind = struct.unpack_from("<I", memory, offset + 8)[0]
        opcode = struct.unpack_from("<H", memory, offset + 0x0C)[0]
        catalog_item = by_opcode.get(opcode)
        semantic_name = decorated[len(prefix):at]
        if catalog_item is None:
            failures.append({"rva": hx(addr), "error": "opcode absent from catalog", "opcode": opcode,
                             "name": semantic_name})
            break
        index, entry = catalog_item
        if entry["kind"] != kind or entry["name"] != semantic_name:
            failures.append({"rva": hx(addr), "error": "catalog semantic mismatch", "opcode": opcode,
                             "observed_kind": kind, "observed_name": semantic_name, "expected": entry})
            break
        if index in seen:
            failures.append({"rva": hx(addr), "error": "duplicate catalog entry", "opcode": opcode,
                             "name": semantic_name})
            break
        seen.add(index)
        # The C++ loop then applies its own +8 increment: +0x88 here is +0x90 total.
        addr += 0x88
        addr += 8
    missing = [catalog[index] for index in range(len(catalog)) if index not in seen]
    if missing:
        failures.append({"error": "catalog entries not seen", "count": len(missing), "sample": missing[:10]})
    result = {
        "matches_header_algorithm": not failures,
        "range_begin": hx(begin), "range_end_exclusive": hx(end),
        "catalog_size": len(catalog), "seen": len(seen),
        "scanned_8byte_slots": scanned_slots,
        "valid_rtti_candidates": rtti_candidates,
        "troc_rtti_candidates": troc_candidates,
        "missing": len(missing), "duplicates_or_false_candidates": len(failures) - (1 if missing else 0),
    }
    return result, failures


def validate_actor_lookup(reader: Reader) -> tuple[dict, list[dict]]:
    failures = []
    world = reader.ptr(BASE + 0x71BDEF0)
    manager = reader.ptr(world + 0x30)
    local_actor = reader.ptr(manager + 0x50)
    local_key = reader.unpack(local_actor + 0x60, "<I")[0]
    table = reader.ptr(manager + 0x08)
    bucket_count = reader.unpack(table + 0x88, "<I")[0]
    bucket_base = reader.ptr(table + 0x98)
    nodes = reader.ptr(table + 0xA0)
    if not 0 < bucket_count <= (1 << 24):
        failures.append({"field": "table+0x88", "error": "invalid bucket count", "observed": bucket_count})
        return {"matches_source_algorithm": False, "local_key": hx(local_key)}, failures
    bucket_index = local_key % bucket_count
    bucket = bucket_base + bucket_index * 0x100
    entry_count = reader.unpack(bucket, "<I")[0]
    found = None
    for index in range(min(entry_count, (0x100 - 8) // 8)):
        key = reader.unpack(bucket + 8 + index * 8, "<I")[0]
        if key != local_key:
            continue
        node_index = reader.unpack(bucket + 0x0C + index * 8, "<I")[0]
        node = reader.ptr(nodes + node_index * 8)
        node_key = reader.unpack(node + 4, "<I")[0]
        actor = reader.ptr(node + 8)
        found = {"bucket_entry": index, "node_index": node_index, "node_key": node_key,
                 "actor_matches": actor == local_actor}
        if node_key != local_key:
            failures.append({"field": "node+4", "expected": hx(local_key), "observed": hx(node_key)})
        if actor != local_actor:
            failures.append({"field": "node+8", "error": "actor pointer differs from manager+0x50"})
        break
    if found is None:
        failures.append({"error": "local actor key absent from expected bucket"})
    result = {
        "matches_source_algorithm": not failures,
        "manager_vtable_rva": hx(reader.ptr(manager) - BASE),
        "local_actor_vtable_rva": hx(reader.ptr(local_actor) - BASE),
        "local_key": hx(local_key), "bucket_count": bucket_count,
        "bucket_index": bucket_index, "bucket_entry_count": entry_count,
        "resolved": found,
    }
    return result, failures


def inspect_globals(reader: Reader, transport: dict) -> tuple[dict, list[dict]]:
    rvas = {m["symbol"]: number(m["new_rva"]) for m in transport["mappings"]}
    errors = []
    def slot(symbol):
        value = reader.ptr(BASE + rvas[symbol])
        if not value:
            errors.append({"symbol": symbol, "error": "null live global"})
        return value
    server = slot("kServerNetSlot")
    client = slot("kClientNetSlot")
    actor_lookup = slot("kServerActorLookupSlot")
    sam = reader.ptr(BASE + SAM_SLOT_3527)
    result = {
        "server_net": {"slot_rva": hx(rvas["kServerNetSlot"]), "present": bool(server)},
        "client_net": {"slot_rva": hx(rvas["kClientNetSlot"]), "present": bool(client)},
        "server_actor_lookup": {"slot_rva": hx(rvas["kServerActorLookupSlot"]), "present": bool(actor_lookup)},
        "server_actor_manager": {"slot_rva": hx(SAM_SLOT_3527), "present": bool(sam)},
    }
    if not (server and client and actor_lookup and sam):
        return result, errors

    session = reader.ptr(server + 0x18)
    client_session = reader.ptr(client + 0x10)
    queues = {
        "server_in_20": reader.ptr(server + 0x20),
        "server_free_c2s_78": reader.ptr(server + 0x78),
        "server_free_s2c_80": reader.ptr(server + 0x80),
        "client_in_18": reader.ptr(client + 0x18),
        "client_free_c2s_28": reader.ptr(client + 0x28),
    }
    session_vt = reader.ptr(session) - BASE if session else None
    expected_session_vt = rvas["kNwVirtualAsyncSessionVtable"]
    user = reader.ptr(session + 0x90) if session else 0
    crypto = reader.ptr(session + 0x98) if session else 0
    session_out = reader.ptr(session + 0xC8) if session else 0
    user_back = reader.ptr(user + 0x1F8) if user else 0
    user_vt = reader.ptr(user) - BASE if user else None
    result["server_net"].update({
        "session_present": bool(session), "session_vtable_rva": hx(session_vt),
        "expected_session_vtable_rva": hx(expected_session_vt),
        "session_vtable_matches": session_vt == expected_session_vt,
        "session_id_ptr_present": bool(reader.ptr(session + 0x88)) if session else False,
        "user_present": bool(user), "crypto_present": bool(crypto),
        "crypto_enabled_byte": reader.unpack(crypto + 0x24, "<B")[0] if crypto else None,
        "session_out_queue_matches_client_in": session_out == queues["client_in_18"],
        "thread_array_present": bool(reader.ptr(server + 0xF8)),
        "thread_count": reader.unpack(server + 0x100, "<I")[0],
    })
    result["client_net"].update({
        "session_present": bool(client_session),
        "session_vtable_rva": hx(reader.ptr(client_session) - BASE) if client_session else None,
        "expected_session_vtable_rva": hx(VT_VIRTUAL_SYNC_SESSION_3527),
        "session_vtable_matches": bool(client_session) and reader.ptr(client_session) - BASE == VT_VIRTUAL_SYNC_SESSION_3527,
        "connected_slot_58_present": bool(reader.ptr(client + 0x58)),
    })
    result["session_user"] = {
        "present": bool(user), "vtable_rva": hx(user_vt),
        "expected_vtable_rva": hx(VT_SERVER_USER_ACTOR_3527),
        "vtable_matches": user_vt == VT_SERVER_USER_ACTOR_3527,
        "key": reader.unpack(user + 0x60, "<I")[0] if user else None,
        "back_pointer_matches_session": user_back == session,
    }
    result["queues"] = {name: bool(value) for name, value in queues.items()}
    result["queues"].update({
        "server_free_c2s_matches_client_free_c2s": queues["server_free_c2s_78"] == queues["client_free_c2s_28"],
        "server_free_s2c_matches_client_20": queues["server_free_s2c_80"] == reader.ptr(client + 0x20),
    })
    for name, value in queues.items():
        if not value:
            errors.append({"field": name, "error": "null queue/lock field"})
    if session_vt != expected_session_vt:
        errors.append({"field": "ServerNet+0x18 vtable", "expected": hx(expected_session_vt), "observed": hx(session_vt)})
    if session_out != queues["client_in_18"]:
        errors.append({"field": "session+0xC8 / ClientNet+0x18", "error": "queue identity mismatch"})
    if user and user_back != session:
        errors.append({"field": "ServerUserActor+0x1F8", "error": "session back-pointer mismatch"})
    if user_vt != VT_SERVER_USER_ACTOR_3527:
        errors.append({"field": "ServerUserActor vtable", "expected": hx(VT_SERVER_USER_ACTOR_3527), "observed": hx(user_vt)})
    if client_session and reader.ptr(client_session) - BASE != VT_VIRTUAL_SYNC_SESSION_3527:
        errors.append({"field": "ClientNet+0x10 vtable", "expected": hx(VT_VIRTUAL_SYNC_SESSION_3527),
                       "observed": hx(reader.ptr(client_session) - BASE)})
    sam_vt = reader.ptr(sam) - BASE
    lookup_sam = reader.ptr(actor_lookup)
    result["server_actor_lookup"]["first_field_matches_manager"] = lookup_sam == sam
    result["server_actor_manager"].update({
        "vtable_rva": hx(sam_vt), "expected_vtable_rva": hx(VT_SERVER_ACTOR_MANAGER_3527),
        "vtable_matches": sam_vt == VT_SERVER_ACTOR_MANAGER_3527,
        "actor_count_9c": reader.unpack(sam + 0x9C, "<I")[0],
        "free_keys_d0": reader.unpack(sam + 0xD0, "<I")[0],
        "key_capacity_d4": reader.unpack(sam + 0xD4, "<I")[0],
    })
    if lookup_sam != sam:
        errors.append({"field": "[[kServerActorLookupSlot]] / [kSamSlot]", "error": "manager identity mismatch"})
    if sam_vt != VT_SERVER_ACTOR_MANAGER_3527:
        errors.append({"field": "ServerActorManager vtable", "expected": hx(VT_SERVER_ACTOR_MANAGER_3527), "observed": hx(sam_vt)})
    if queues["server_free_c2s_78"] != queues["client_free_c2s_28"]:
        errors.append({"field": "ServerNet+0x78 / ClientNet+0x28", "error": "free-pool identity mismatch"})
    if queues["server_free_s2c_80"] != reader.ptr(client + 0x20):
        errors.append({"field": "ServerNet+0x80 / ClientNet+0x20", "error": "free-pool identity mismatch"})
    return result, errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--troc", type=Path, default=Path(__file__).parent / "data" / "troc_mapping_2976_3527.json")
    parser.add_argument("--transport", type=Path, default=Path(__file__).parent / "data" / "transport_evidence_3527.json")
    parser.add_argument("--out", type=Path, default=Path(__file__).parent / "data" / "live_native_3527.json")
    args = parser.parse_args()
    troc = json.loads(args.troc.read_text(encoding="utf-8"))
    transport = json.loads(args.transport.read_text(encoding="utf-8"))
    reader = Reader(args.pid)
    try:
        identity = validate_identity(reader, args.exe, troc, transport)
        singleton_rows, singleton_mismatches = validate_singletons(reader, troc)
        code_rows, code_mismatches = validate_transport_code(reader, transport)
        catalog_scan, catalog_mismatches = validate_catalog_scan(reader, troc)
        actor_lookup, actor_lookup_mismatches = validate_actor_lookup(reader)
        live_globals, global_mismatches = inspect_globals(reader, transport)
        all_mismatches = (singleton_mismatches + code_mismatches + catalog_mismatches +
                          actor_lookup_mismatches + global_mismatches)
        result = {
            "status": "pass" if not all_mismatches else "fail",
            "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "pid": args.pid, "access": "PROCESS_VM_READ only",
            "identity": identity,
            "summary": {
                "singletons_checked": len(singleton_rows),
                "singleton_mismatches": len(singleton_mismatches),
                "transport_code_sites_checked": len(code_rows),
                "transport_code_mismatches": len(code_mismatches),
                "catalog_scan_mismatches": len(catalog_mismatches),
                "actor_lookup_mismatches": len(actor_lookup_mismatches),
                "live_global_mismatches": len(global_mismatches),
            },
            "catalog_scan": catalog_scan,
            "actor_lookup": actor_lookup,
            "live_globals": live_globals,
            "transport_code": code_rows,
            "singletons": singleton_rows,
            "mismatches": all_mismatches,
            "limitations": [
                "Live identity, singleton metadata/vptr/handler, prologue bytes and selected object fields only.",
                "This does not prove packet payload ABI compatibility or authorize unverified optional hooks.",
            ],
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"status": result["status"], **result["summary"], "live_globals": live_globals}, indent=2))
        return 0 if result["status"] == "pass" else 1
    finally:
        reader.close()


if __name__ == "__main__":
    raise SystemExit(main())
