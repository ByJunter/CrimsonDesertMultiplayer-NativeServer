#!/usr/bin/env python3
"""Read captured state for the 3527 native nullable-holder serialization fault.

No process access. The precise caller is derived from the faulting function's
verified prologue (push rdi; sub rsp,20h), not from a heuristic stack scan.
Component-specific interpretation is made only for the known 22E2657 caller.
"""
import argparse
import io
import json
from pathlib import Path
import struct

from minidump_inspect import MinidumpFile, CONTEXT, REGISTERS, read_available, locate, hx

DATA = Path(__file__).parent / "data"


def inspect(path):
    dump = MinidumpFile.parse(str(path))
    try:
        modules = dump.modules.modules
        main = next(m for m in modules if m.name.lower().endswith("crimsondesert.exe"))
        if main.timestamp != 0x6AEA07A6 or main.size != 0x13EB2000:
            raise ValueError("Dump is not the verified 3527 image")
        reader = dump.get_reader()
        rtti = json.loads((DATA / "rtti_3527.json").read_text())
        names = {int(v["rva"], 0): c["name"] for c in rtti["classes"].values() for v in c["vtables"]}

        def value(at, fmt="<Q"):
            data = read_available(reader, at, struct.calcsize(fmt))
            return struct.unpack(fmt, data)[0] if len(data) == struct.calcsize(fmt) else None

        def raw(at, count):
            return read_available(reader, at, count).hex(" ")

        def object_info(at):
            vt = value(at)
            return dict(address=hx(at), vtable=hx(vt) if vt is not None else None,
                        rtti_name=names.get(vt - main.baseaddress) if vt is not None else None)

        def entry_info(at):
            stage = value(at + 0x20)
            condition = value(at)
            return dict(address=hx(at), holder=value(at + 8), bytes=raw(at, 0x38),
                        condition_string_pointer=hx(condition) if condition is not None else None,
                        condition_string_memory=raw(condition, 64) if condition else None,
                        stage_field=hx(stage) if stage is not None else None,
                        stage_index=stage & 0x1FFFFFFF if stage is not None else None,
                        parent_index=value(at + 0x2E, "<H"),
                        entry_type=value(at + 0x10, "<H"),
                        byte_2c=value(at + 0x2C, "<B"), byte_30=value(at + 0x30, "<B"))

        record = next((e for e in dump.exception.exception_records
                       if e.ExceptionRecord.ExceptionAddress == main.baseaddress + 0x249423A), None)
        if record is None:
            raise ValueError("No exception at the expected 3527 null-holder fault")
        dump.file_handle.seek(record.ThreadContext.Rva)
        context = CONTEXT.parse(io.BytesIO(dump.file_handle.read(record.ThreadContext.DataSize)))
        # Function 2494230 saves RBX in caller shadow space, pushes RDI, and
        # subtracts 20h before faulting at +Ah. The return is therefore +28h.
        return_address = value(context.Rsp + 0x28)
        report = dict(dump=str(path.resolve()), thread_id=record.ThreadId,
                      exception_code=hx(record.ExceptionRecord.ExceptionCode_raw),
                      exception_information=[hx(x) for x in record.ExceptionRecord.ExceptionInformation],
                      registers={r: hx(getattr(context, r)) for r in REGISTERS},
                      return_slot=hx(context.Rsp + 0x28),
                      caller_return=locate(modules, return_address) if return_address else None,
                      exact_known_caller=return_address == main.baseaddress + 0x22E265C,
                      caller_basis="Verified 2494230 prologue uses 0x28 bytes before fault at +0xA",
                      archive=object_info(context.Rdx), archive_bytes=raw(context.Rdx, 0x60))
        if not report["exact_known_caller"]:
            return report

        component = object_info(context.Rsi)
        component.update(bytes=raw(context.Rsi, 0x160), owner=value(context.Rsi + 8),
                         vector=value(context.Rsi + 0x90), count=value(context.Rsi + 0x98, "<I"))
        entry = dict(**entry_info(context.Rdi), end_register=hx(context.Rbp))
        start, count = component["vector"], component["count"]
        if start is not None and count is not None:
            end = start + count * 0x38
            entry["expected_end"] = hx(end)
            entry["end_register_matches"] = end == context.Rbp
            entry["inside_vector"] = start <= context.Rdi < end and (context.Rdi - start) % 0x38 == 0
            entry["index"] = (context.Rdi - start) // 0x38 if entry["inside_vector"] else None
            component["entries"] = [dict(index=i, **entry_info(start + i * 0x38))
                                     for i in range(min(count, 32))] if count <= 0x10000 else []
        report.update(component=component, current_entry=entry)
        owner = component["owner"]
        if owner:
            actor = object_info(owner)
            actor.update(key=value(owner + 0x60, "<I"), controller=value(owner + 0x90, "<I"),
                         user=value(owner + 0xA0), components=value(owner + 0x68), bytes=raw(owner, 0xB0))
            if actor["user"]:
                actor["user_info"] = object_info(actor["user"])
                actor["user_info"]["key"] = value(actor["user"] + 0x60, "<I")
            if actor["components"]:
                table = actor["components"]
                actor["matching_component_slots"] = [hx(i) for i in range(0, 0x400, 8)
                                                      if value(table + i) == context.Rsi]
            report["owner_actor"] = actor
        return report
    finally:
        dump.file_handle.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    report = inspect(args.dump)
    args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report.get(k) for k in ("thread_id", "exception_code", "caller_return",
                     "exact_known_caller", "current_entry", "owner_actor")}, indent=2))


if __name__ == "__main__":
    main()
