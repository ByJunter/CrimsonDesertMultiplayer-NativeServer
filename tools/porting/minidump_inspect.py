#!/usr/bin/env python3
"""Inspect an x64 minidump without attaching to or changing a live process.

Dependency: python -m pip install --target build/porting-python-deps minidump
The JSON contains exception-context registers, module-relative locations,
captured instruction bytes, and stack pointer candidates. Stack scanning is
explicitly not an unwind and may include data that happens to point into code.
"""
from __future__ import annotations

import argparse
import io
import json
import logging
import ntpath
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "build" / "porting-python-deps"))
from minidump.minidumpfile import MinidumpFile
from minidump.streams.ContextStream import CONTEXT
from minidump.streams.SystemInfoStream import PROCESSOR_ARCHITECTURE

try:
    import capstone
except ImportError:
    capstone = None

REGISTERS = ("Rax", "Rbx", "Rcx", "Rdx", "Rsi", "Rdi", "Rbp", "Rsp",
             "R8", "R9", "R10", "R11", "R12", "R13", "R14", "R15", "Rip", "EFlags")


def hx(value: int) -> str:
    return f"0x{value:X}"


def locate(modules, address: int) -> dict:
    for module in modules:
        if module.baseaddress <= address < module.endaddress:
            name = ntpath.basename(module.name)
            offset = address - module.baseaddress
            return dict(address=hx(address), module=name, module_path=module.name,
                        module_base=hx(module.baseaddress), rva=hx(offset),
                        location=f"{name}+{hx(offset)}")
    return dict(address=hx(address), module=None, location=hx(address))


def read_available(reader, address: int, count: int) -> bytes:
    """Read only contiguous bytes actually present in the dump."""
    result = bytearray()
    while count:
        segment = next((s for s in reader.memory_segments if s.inrange(address)), None)
        if segment is None:
            break
        size = min(count, segment.end_virtual_address - address)
        block = segment.read(address, size, reader.file_handle)
        result.extend(block)
        if len(block) != size:
            break
        address += size
        count -= size
    return bytes(result)


def disassemble(data: bytes, address: int, limit: int = 12) -> list[dict]:
    if not capstone:
        return []
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    return [dict(address=hx(ins.address), bytes=ins.bytes.hex(" "),
                 mnemonic=ins.mnemonic, operands=ins.op_str)
            for ins in decoder.disasm(data, address, count=limit)]


def preceding_calls(reader, address: int) -> list[dict]:
    """Possible calls ending at this value; instruction alignment is unproven."""
    if not capstone or address < 15:
        return []
    data = read_available(reader, address - 15, 15)
    if len(data) != 15:
        return []
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    result = []
    for start in range(15):
        instructions = list(decoder.disasm(data[start:], address - 15 + start, count=1))
        if instructions:
            ins = instructions[0]
            if ins.mnemonic == "call" and ins.address + ins.size == address:
                result.append(dict(address=hx(ins.address), bytes=ins.bytes.hex(" "),
                                   mnemonic=ins.mnemonic, operands=ins.op_str))
    return result


def inspect(path: Path, stack_bytes: int, max_candidates: int) -> dict:
    # The package tries optional PEB parsing. Its failure is irrelevant to the
    # exception stream, but preserve diagnostic warnings in the report.
    parser_log = io.StringIO()
    handler = logging.StreamHandler(parser_log)
    logger = logging.getLogger()
    logger.addHandler(handler)
    try:
        dump = MinidumpFile.parse(str(path))
    finally:
        logger.removeHandler(handler)
    try:
        if not dump.sysinfo or dump.sysinfo.ProcessorArchitecture != PROCESSOR_ARCHITECTURE.AMD64:
            raise ValueError("This inspector requires an AMD64 minidump")
        modules = dump.modules.modules if dump.modules else []
        reader = dump.get_reader()
        threads = {t.ThreadId: t for t in dump.threads.threads} if dump.threads else {}
        records = dump.exception.exception_records if dump.exception else []
        report = dict(dump=str(path.resolve()), bytes=path.stat().st_size,
                      architecture="AMD64", module_count=len(modules), thread_count=len(threads),
                      stack_scan_note="Candidates only: not an unwound call stack; data can resemble code pointers.",
                      exceptions=[], modules=[dict(name=ntpath.basename(m.name), path=m.name,
                                                    base=hx(m.baseaddress), size=hx(m.size),
                                                    timestamp=hx(m.timestamp)) for m in modules])
        if parser_log.getvalue():
            report["parser_warnings"] = parser_log.getvalue()
        for record in records:
            exception = record.ExceptionRecord
            context_location = record.ThreadContext
            if context_location.DataSize < 256:
                raise ValueError(f"Exception context too short: {context_location.DataSize}")
            dump.file_handle.seek(context_location.Rva)
            context = CONTEXT.parse(io.BytesIO(dump.file_handle.read(context_location.DataSize)))
            registers = {name: hx(getattr(context, name)) for name in REGISTERS}
            code = exception.ExceptionCode_raw
            information = exception.ExceptionInformation
            item = dict(thread_id=record.ThreadId, code=hx(code),
                        code_name=exception.ExceptionCode.name, flags=hx(exception.ExceptionFlags),
                        exception_address=locate(modules, exception.ExceptionAddress),
                        information=[hx(v) for v in information], registers=registers,
                        rip=locate(modules, context.Rip), context_source="exception stream")
            if code in (0xC0000005, 0xC0000006) and len(information) >= 2:
                item["access"] = dict(kind={0: "read", 1: "write", 8: "execute"}.get(information[0], "unknown"),
                                      kind_value=information[0], address=hx(information[1]))
            instruction_bytes = read_available(reader, context.Rip, 96)
            item["code_at_rip"] = dict(captured_bytes=instruction_bytes.hex(" "),
                                       instructions=disassemble(instruction_bytes, context.Rip),
                                       source="captured dump memory only")
            stack = read_available(reader, context.Rsp, stack_bytes)
            thread = threads.get(record.ThreadId)
            item["stack"] = dict(rsp=hx(context.Rsp), requested_bytes=stack_bytes,
                                  captured_bytes=len(stack), first_qwords=[], candidates=[])
            if thread:
                item["stack"]["thread_descriptor_start"] = hx(thread.Stack.StartOfMemoryRange)
                item["stack"]["thread_descriptor_bytes"] = thread.Stack.DataSize
            for off in range(0, len(stack) - 7, 8):
                value = struct.unpack_from("<Q", stack, off)[0]
                if off < 128:
                    item["stack"]["first_qwords"].append(dict(offset=hx(off), value=hx(value)))
                location = locate(modules, value)
                if location["module"] and len(item["stack"]["candidates"]) < max_candidates:
                    item["stack"]["candidates"].append(dict(stack_address=hx(context.Rsp + off),
                                                             stack_offset=hx(off), **location,
                                                             possible_preceding_calls=preceding_calls(reader, value)))
            report["exceptions"].append(item)
        return report
    finally:
        dump.file_handle.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--stack-bytes", type=lambda s: int(s, 0), default=0x2000)
    parser.add_argument("--max-candidates", type=int, default=64)
    args = parser.parse_args()
    if not 0 <= args.stack_bytes <= 0x100000 or not 0 <= args.max_candidates <= 4096:
        parser.error("stack bytes must be <= 1 MiB and candidates <= 4096")
    result = inspect(args.dump, args.stack_bytes, args.max_candidates)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    summary = dict(out=str(args.out), exceptions=[dict(thread_id=e["thread_id"], code=e["code"],
                   rip=e["rip"]["location"], access=e.get("access"),
                   stack_candidates=len(e["stack"]["candidates"])) for e in result["exceptions"]])
    print(json.dumps(summary, indent=2))
    return 0 if result["exceptions"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
