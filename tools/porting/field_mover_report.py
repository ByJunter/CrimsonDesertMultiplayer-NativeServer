"""Verify build-3527 FieldMover flags and the shared-field ABI from the PE."""
import hashlib
import json
from pathlib import Path

from signature_map import ROOT, CS, ImageScanner, Pe


EXPECTED_SHA = "dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841"
SITES = [
    (0x2E604FF, "mov", "dword ptr [rcx + 8], 0x30000", "FieldMover reset field key"),
    (0x2E60508, "mov", "qword ptr [rcx + 0x10], rsi", "FieldMover reset field pointer"),
    (0x2E6050C, "mov", "qword ptr [rcx + 0x40], rsi", "FieldMover reset sector pointer"),
    (0x2E60510, "mov", "word ptr [rcx + 0x4a], si", "Reset access counter"),
    (0x2E60514, "mov", "byte ptr [rcx + 0x48], sil", "Reset move state"),
    (0x2E61188, "mov", "rsi, rcx", "GetField preserves the FieldMover in RSI"),
    (0x2E611B6, "cmp", "byte ptr [rsi + 0x48], r15b", "GetField refuses a nonzero move state"),
    (0x2E6125F, "inc", "word ptr [rsi + 0x4a]", "Successful GetField increments active accesses"),
    (0x2E61263, "mov", "rbp, qword ptr [rsi + 0x10]", "The same object supplies its ServerField"),
    (0x2E61267, "mov", "r14, qword ptr [rsi + 0x40]", "The same object supplies its sector"),
    (0x2E60F82, "mov", "rdi, rcx", "Release preserves the FieldMover in RDI"),
    (0x2E60FAE, "dec", "word ptr [rdi + 0x4a]", "Release decrements active accesses"),
    (0x2E60FB2, "cmp", "word ptr [rdi + 0x4a], r14w", "Release checks remaining active accesses"),
    (0x2E6100F, "cmp", "byte ptr [rdi + 0x48], 1", "Pending move state checked after final release"),
    (0x2E61015, "mov", "byte ptr [rdi + 0x48], 2", "Pending move advances after final release"),
    (0x2E619B0, "mov", "r15, rcx", "MoveField preserves its FieldMover in R15"),
    (0x2E619F7, "mov", "eax, dword ptr [rcx + 0x50]", "Current destination FieldInfoKey"),
    (0x2E61A23, "mov", "rax, qword ptr [rsi + 0x68]", "MoveField obtains the actor component table"),
    (0x2E61A27, "mov", "rcx, qword ptr [rax + 0x1a0]", "MoveField obtains TransformSync"),
    (0x2E61A2E, "mov", "byte ptr [rcx + 0x3dc], 1", "TransformSync moving byte remains at 0x3DC"),
    (0x2E623A1, "mov", "byte ptr [r15 + 0x48], 3", "MoveField advances the same move-state byte"),
    (0x2E62624, "movzx", "r12d, word ptr [rbx + 0x58]", "Old move-state address now holds destination level"),
    (0x2E62601, "mov", "rsi, qword ptr [rcx + 0x68]", "Current companion vector"),
    (0x2E62605, "mov", "eax, dword ptr [rcx + 0x70]", "Current companion count"),
    (0x2E62608, "imul", "rdi, rax, 0x3c", "Current companion stride"),
    (0x2E61F3D, "mov", "qword ptr [rsp + 0x20], rax", "AcquireField fifth argument is forwarded on stack"),
    (0x2E61F53, "call", "0x2da08e0", "Native AcquireField caller"),
    (0x2DA0AF1, "mov", "rax, qword ptr [rbp + 0x50]", "AcquireField retrieves fifth argument"),
    (0x2DA0AF8, "mov", "dword ptr [r14 + 0x180], ecx", "ServerField owner remains at 0x180"),
    (0x2DA0B2E, "call", "0x1617950", "Native TryAddRef"),
    (0x2DA0B33, "test", "al, al", "TryAddRef returns a bool in AL"),
    (0x2DA0B6A, "mov", "qword ptr [r15 + 0x18], rsi", "Attacher holder offset unchanged"),
    (0x2DA0B6E, "mov", "qword ptr [r15 + 8], r14", "Attacher field offset unchanged"),
    (0x2DA0B72, "mov", "byte ptr [r15 + 0x10], bl", "Attacher valid offset unchanged"),
    (0x2DA0B86, "mov", "byte ptr [r15 + 0x20], 1", "Attacher newly-created offset unchanged"),
    (0x2E620DE, "mov", "r8, rdi", "EnterField gets the destination field as third argument"),
    (0x2E620E8, "mov", "rcx, r15", "EnterField gets the FieldMover as first argument"),
    (0x2E620EB, "call", "0x2e625b0", "Native EnterField caller"),
    (0xE6166EF, "mov", "rax, qword ptr [rbp + 0x2a8]", "Native AddActor user vector"),
    (0xE6166F6, "mov", "ecx, dword ptr [rbp + 0x2b0]", "Native AddActor user count"),
    (0xE6166FC, "shl", "rcx, 4", "Native user-entry size is 16 bytes"),
    (0xE616741, "inc", "dword ptr [rbp + 0x2b8]", "Native total user-actor count"),
    (0xE61678B, "call", "0x4a2500", "Native 16-byte user-entry vector append"),
]


def main():
    data = (ROOT.parent / "bin64" / "CrimsonDesert.exe").read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA, "unsupported executable"
    pe = Pe(data)
    scanner = ImageScanner(pe)
    records = []
    for address, mnemonic, operands, meaning in SITES:
        offset = pe.rva_to_off(address)
        instruction = next(CS.disasm(data[offset:offset + 16], address, count=1))
        assert (instruction.mnemonic, instruction.op_str) == (mnemonic, operands), hex(address)
        records.append(dict(rva=hex(address), bytes=instruction.bytes.hex(" "),
                            mnemonic=mnemonic, operands=operands, meaning=meaning))
    report = dict(build="1.0.0.3527", exe_sha256=digest, assertions=records,
                  field_mover=dict(ts_offset="0x508", field="0x10", sector="0x40",
                                   move_state="0x48", active_accesses="0x4A",
                                   destination_key="0x50", destination_level="0x58",
                                   companion_vector="0x68", companion_count="0x70",
                                   companion_stride="0x3C"),
                  transform_sync_moving="0x3DC",
                  affected_runtime_reads=["share_candidate busy gate", "read_move_diag move/access fields"],
                  crash_causality="Confirmed layout defect, not established as the cause of run3 host crash.",
                  functions={hex(x):scanner.function(x) for x in
                             (0x2E604E0,0x2E60F70,0x2E61170,0x2E61990,0x2DA08E0,0x2E625B0,0xE616690)})
    path = Path(__file__).parent / "data" / "field_mover_layout_3527.json"
    path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(output=str(path), assertions=len(records))))


if __name__ == "__main__":
    main()
