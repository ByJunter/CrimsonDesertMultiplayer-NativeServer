"""Assert the 3527 guest action-readiness fault and related native query paths.

Reads the installed PE and saved dump analysis; never opens or writes a process.
Writes only JSON and disassembly evidence. This is a bounded static audit, not a
claim that every indirect call or character phase has been exhaustively covered.
"""
import hashlib
import json
from pathlib import Path

from signature_map import ROOT, CS, ImageScanner, Pe


EXPECTED_SHA = "dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841"
SITES = [
    (0x9AC5E0, "lea", "rax, [rip + 0x1ea9]", "Task constructor selects thunk9AE490"),
    (0x9AE490, "jmp", "0x99b380", "Task thunk enters actor iteration"),
    (0x9A03CC, "call", "0x9ac5c0", "Separate phase dispatcher constructs this task"),
    (0x99B397, "mov", "rax, qword ptr [rcx + 0x248]", "Task actor vector"),
    (0x99B3F9, "mov", "rcx, qword ptr [rax + 0x40]", "Task obtains CharacterControl"),
    (0x99B402, "vmovss", "xmm1, dword ptr [rsi + 0x398]", "Phase dt argument"),
    (0x99B40A, "call", "0x94f7f0", "Omitted native CC phase call"),
    (0x99B40F, "mov", "rcx, qword ptr [rbx + 0x68]", "Caller does not consume phase return value"),
    (0x99B41F, "call", "0x57f210", "Caller continues other component work before actor reference cleanup"),
    (0x94F7F0, "mov", "qword ptr [rsp + 0x10], rbx", "Five-byte true entry instruction, before side effects"),
    (0x94F80D, "vmovaps", "xmm6, xmm1", "Phase preserves dt"),
    (0x94F811, "mov", "rsi, rcx", "Phase RCX is CharacterControl"),
    (0x94F82D, "mov", "byte ptr [rcx + 0x871], bl", "Phase can mutate flags before movement operation"),
    (0x94F84E, "mov", "r13, qword ptr [rcx + 0x1a8]", "Navigation component; not CC pending action storage"),
    (0x94F878, "mov", "rdi, qword ptr [r13 + 0x5c8]", "Navigation pending object"),
    (0x94F883, "mov", "qword ptr [r13 + 0x5c8], rbx", "Phase drains navigation pending object"),
    (0x94F93F, "call", "0x21bfd00", "Publish navigation object independently of action restore"),
    (0x94FB47, "call", "0x7f28e0", "Earlier movement pending operation"),
    (0x94FB50, "mov", "rcx, qword ptr [rsi + 0x108]", "Movement object argument"),
    (0x94FB57, "call", "0x7f2c20", "Movement update containing multiple unchecked consumers"),
    (0x7F2C4F, "mov", "rdx, qword ptr [rcx]", "Movement object first pointer is CC"),
    (0x7F2CEA, "call", "0x7f5b10", "Unchecked consumers before fault leaf"),
    (0x7F2CFF, "call", "0x7f66a0", "Fault leaf call"),
    (0x7F5BFC, "movzx", "ecx, byte ptr [rax + 0x1b]", "Native selected layer index"),
    (0x7F5C00, "mov", "rax, qword ptr [rdx + rcx*8 + 0x88]", "Selected layer in CC array"),
    (0x7F5C0C, "movzx", "eax, byte ptr [rcx + 0x116]", "Earlier unchecked current-action read"),
    (0x7F5C94, "movzx", "edx, byte ptr [rcx + 0x117]", "Second unchecked current-action read"),
    (0x7F5DF9, "mov", "rax, qword ptr [rax + 0x18]", "Selected playback pointer"),
    (0x7F5DFD, "mov", "r8, qword ptr [rax + 0x30]", "Unchecked playback read"),
    (0x7F685F, "mov", "rcx, qword ptr [rdi]", "Fault leaf obtains CC from movement object"),
    (0x7F6862, "mov", "rax, qword ptr [rcx + 0x30]", "CC selected-state object"),
    (0x7F6866, "movzx", "edx, byte ptr [rax + 0x1b]", "Selected layer byte"),
    (0x7F686A, "mov", "rax, qword ptr [rcx + rdx*8 + 0x88]", "Selected layer pointer"),
    (0x7F6872, "mov", "rdx, qword ptr [rax + 0x10]", "Current-action pointer"),
    (0x7F6876, "cmp", "byte ptr [rdx + 0x116], r15b", "Captured AV: current action is null"),
    (0x99F611, "call", "0x944e70", "Existing guarded update task path"),
    (0x99F61E, "call", "0x946060", "Existing guarded post task path"),
    (0x99A8FB, "call", "0x9454a0", "Separate cleanup phase"),
    (0x2513754, "test", "rbx, rbx", "Later playback query has a native null check"),
    (0x2513757, "je", "0x2513833", "Null playback takes cleanup and false return"),
    (0xB5F870, "test", "rax, rax", "Later animation path checks null playback"),
    (0xB5CF39, "test", "rax, rax", "Related animation path checks null playback"),
    (0x99B0CA, "movzx", "eax, byte ptr [rcx + 8]", "Sibling task has additional CC mode predicates"),
    (0x99B14E, "mov", "rcx, qword ptr [rcx + 0xc8]", "Sibling phase argument is CC+C8 subobject"),
    (0x99B155, "call", "0x966490", "Sibling transform phase"),
    (0x966780, "call", "0xb0b740", "Sibling phase invokes transform processing"),
    (0xB0B8A6, "call", "0xb0b230", "Transform processing queries action byte"),
    (0xB0B8AB, "mov", "byte ptr [rbp + 0x118], al", "Query result is a byte in AL"),
    (0xB0B230, "mov", "qword ptr [rsp + 8], rbx", "Five-byte true query entry, before side effects"),
    (0xB0B242, "movzx", "ebp, dl", "Query second argument is byte DL"),
    (0xB0B257, "mov", "rbx, qword ptr [rcx + 8]", "Query component owner actor at +8"),
    (0xB0B25B, "mov", "r8, qword ptr [rbx + 0x68]", "Actor component table"),
    (0xB0B342, "mov", "rdx, qword ptr [rax + 0x40]", "Query obtains CC from component table"),
    (0xB0B34A, "movzx", "ecx, byte ptr [rax + 0x1b]", "Query uses the selected layer"),
    (0xB0B356, "mov", "rcx, qword ptr [rax + 0x10]", "One query branch loads current without a null check"),
    (0xB0B35A, "movzx", "eax, byte ptr [rcx + 0xf8]", "Latent unchecked current-action byte read"),
    (0xB0B5FC, "mov", "rax, qword ptr [rax + 0x10]", "Another native query branch loads current"),
    (0xB0B600, "test", "rax, rax", "That native branch explicitly accepts null current"),
    (0xB0B603, "je", "0xb0b60e", "Null current chooses the native neutral result"),
    (0xB0B60E, "xor", "al, al", "Native neutral result is AL=0"),
    (0xB0B622, "ret", "", "Normal query return"),
]

RANGES = [
    (0x94F7F0, 0x43F), (0x99B380, 0xF0), (0x7F2C20, 0xE4),
    (0x7F5BF0, 0x214), (0x7F685F, 0x24), (0xB0B230, 0x414),
    (0x99AFF0, 0x1BC), (0x2513732, 0x36), (0x9AC5C0, 0x3E),
]


def main():
    data = (ROOT.parent / "bin64" / "CrimsonDesert.exe").read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA, "Unsupported executable"
    pe, records = Pe(data), []
    scanner = ImageScanner(pe)
    for rva, mnemonic, operands, meaning in SITES:
        offset = pe.rva_to_off(rva)
        inst = next(CS.disasm(data[offset:offset + 16], rva, count=1))
        assert (inst.mnemonic, inst.op_str) == (mnemonic, operands), (
            hex(rva), inst.mnemonic, inst.op_str)
        records.append(dict(rva=f"0x{rva:X}", bytes=inst.bytes.hex(" "),
                            mnemonic=mnemonic, operands=operands, meaning=meaning))
    entries = []
    for rva, abi in [(0x94F7F0, "RCX=CC; XMM1=dt; caller ignores return"),
                     (0xB0B230, "RCX=component; [RCX+8]=actor; DL=flag; return byte AL")]:
        offset = pe.rva_to_off(rva)
        inst = next(CS.disasm(data[offset:offset + 16], rva, count=1))
        assert inst.size == 5
        assert scanner.function(rva)["begin"] == rva
        entries.append(dict(rva=f"0x{rva:X}", head32=data[offset:offset + 32].hex(" "),
                            first_instruction_size=inst.size, abi=abi))
    report = dict(
        build="1.0.0.3527", exe_sha256=digest, assertions=records, guarded_entry_candidates=entries,
        selected_state=dict(cc_selector="0x30", selector_index="0x1B", cc_layers="0x88",
                            layer_stride=8, current="0x10", playback="0x18",
                            note="Unchecked movement reads do not predicate access on chart presence."),
        fault_path=["9A0360 task construction", "9AC5C0 -> 9AE490", "99B380",
                    "94F7F0", "7F2C20", "7F66A0", "7F6876"],
        latent_path=["99AFF0", "966490", "B0B740", "B0B230 -> B0B35A"],
        query_neutral="For remote selected-action-unready, an entry return AL=0 matches native B0B600/B0B603/B0B60E missing-current fallback. Nonremote and ready calls should retain native behavior.",
        recovery="94F7F0 navigation+5C8 is distinct from CC+80 pending records and CC+A8. Independent Restore949B40 and Pending251A650 must remain callable. Eventual first-action arrival is not proven by this report.",
        limitations=["Only the fault path was captured; sibling B0B230 branch activation was not captured.",
                     "No live process, build, or UI validation is performed.",
                     "Bounded static call-path audit; no exhaustive claim about indirect calls or every phase."])
    out = Path(__file__).parent / "data"
    out.mkdir(exist_ok=True)
    (out / "guest_phase_3527.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    text = [f"Build3527 SHA256 {digest}", f"{len(records)} instruction assertions passed."]
    for rva, size in RANGES:
        offset = pe.rva_to_off(rva)
        text += ["", f"Range {rva:X}+{size:X}: {scanner.function(rva)}"]
        text += [f"{x.address:08X}  {x.bytes.hex(' '):32s} {x.mnemonic} {x.op_str}"
                 for x in CS.disasm(data[offset:offset + size], rva)]
    (out / "guest_phase_3527.txt").write_text("\n".join(text) + "\n", encoding="utf-8")
    print(json.dumps(dict(assertions=len(records), entries=entries, report=str(out / "guest_phase_3527.json"))))


if __name__ == "__main__":
    main()
