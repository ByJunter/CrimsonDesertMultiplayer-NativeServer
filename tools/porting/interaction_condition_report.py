"""Assert build-3527 interaction condition evidence without running game code.

Writes only an evidence JSON and disassembly text next to the other port reports.
The serializer caller must still be established from a crash stack; this report
does not claim that a null-holder substitution is safe for every entry kind.
"""
import hashlib
import json
import re
import struct
from pathlib import Path

from signature_map import ROOT, CS, ImageScanner, Pe


EXPECTED_SHA = "dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841"
SITES = [
    (0x22E2604, "8b8198000000", "Component serializer reads vector count at +98"),
    (0x22E2628, "488bbe900000008b8698000000486be838", "Component vector +90, count +98, stride38"),
    (0x22E2650, "488b4f08488bd3e8d41b1b00", "Unchecked entry+8 holder in RCX, archive in RDX, call2494230"),
    (0x22E2705, "4883c738", "Serializer advances every entry by38"),
    (0x22EB0A4, "4533e4", "Zero R12 before constructing derived interaction entries"),
    (0x22EAEB0, "410fb7f8", "Parent index is the incoming InteractionInfo index R8W, saved in DI"),
    (0x22EB162, "4c8965d7", "Derived stack entry+8 is null, relative to entry at RBP-31"),
    (0x22EB183, "4c8965ef4883e90175f6", "Derived stack entry+20 stage id is initialized to zero"),
    (0x22EB1A3, "668945df488d55cf488bcee89d550000", "Set derived index+10 and append stack entry to component vector"),
    (0x22EB1D1, "418b8798000000ffc8486bc838488b0666897c012e", "Set derived entry+2E to its parent InteractionInfo index"),
    (0xCB4879C, "488b47084889420848894f08", "Native append moves holder verbatim and zeros the source holder"),
    (0xCB487F0, "ff4308", "Native append publishes entry by incrementing count"),
    (0x22ECF8B, "33db48895908", "Native entry copy constructor initializes destination holder to null"),
    (0x22ECFC3, "488b06488b084885c90f849e00000038190f8496000000", "Copy skips condition allocation if condition string data is null or empty"),
    (0x22ED059, "48897e08", "Copy installs allocated wrapper only on nonempty-string path"),
    (0x22ED06B, "e820761a00", "Copy parses condition text through2494690"),
    (0x22ED0E7, "488b452048894620", "Copy preserves stage id even if empty text left the holder null"),
    (0x2494230, "48895c2418574883ec2048833900", "Serializer prologue and first dereference of RCX at249423A"),
    (0x2494247, "41b801000000488d542430488bcb0f95442430ff5008807c243000746f", "Write one-byte pointee presence; empty wrapper skips body and flags"),
    (0x24942FA, "488b0241b801000000488d542438488bcfff502884c00f84d0000000", "Inverse reader reads one byte through archive vf28"),
    (0x2494316, "807c2438000f84b3000000", "Zero presence branches to success without parsing contents"),
    (0x24943D4, "b001", "Empty wrapper encoding is accepted successfully"),
    (0x22ED427, "48891f66895f08885f0aeb03", "Entry reader initializes allocated16-byte wrapper to empty"),
    (0x22ED448, "48897d08488bcf488bd6e8896e1a00", "Reader installs wrapper then invokes24942E0"),
    (0x22ED4FF, "488b44244048894520", "Reader stores transmitted stage id without changing it"),
    (0x22ED5E4, "84c07421b001eb1f", "Reader succeeds after final byte; no empty-wrapper normalization"),
    (0x22ED64D, "f74120ffffff1f7507b002e9fe030000", "Interaction condition evaluation returns2 early for stage id low29bits zero"),
    (0x22ED888, "4d8b55084d85d20f8425010000", "Stage-linked evaluation explicitly tests holder pointer and branches if null"),
    (0x22ED8FC, "498b0a488b01", "Stage-linked evaluation assumes a nonnull holder contains a nonnull function"),
    (0x22ED9BA, "32db", "Null holder returns condition result0 on stage-linked path"),
    (0x22E28BC, "66448964244e", "Entry reader initializes derived-parent index toFFFF"),
    (0x22EA1C7, "6644397b2e7527", "Native enumeration excludes derived entries unless requested"),
    (0xCB2B5D3, "4531c0b201e873eb7bf5", "Refresh gathers primary entries via22EA150 with include-derived=false"),
    (0xCB2B5DD, "66c787400100000000", "Refresh resets component summary flags; it does not allocate derived wrappers"),
    (0xCB2A0E3, "420fb7543610e8224b8bf3395808", "Native erase resolves entry+10 through InteractionInfo getter"),
    (0xCB2A0F3, "420fb754362ee8124b8bf3", "Native erase resolves entry+2E through the same getter, proving it is not a vector index"),
    (0x1603A1A, "8bf9", "Native shared-string allocator saves incoming u32 length"),
    (0x1603A7B, "488d431848c74308ffffffff488903", "String char pointer is stored at object+0"),
    (0x1603A8D, "c7431001000000c6431400897b08", "String object refcount+10 and u32 length+8"),
    (0x521753, "488d15c625980648890b483bca740b", "String copy treats exe+6EA3D20 as empty singleton"),
    (0x6EA3D20, "3e9e0d470100000000000000ffffffffffffffff00000000", "Pinned empty shared-string object: char pointer, length0, immortal refcount"),
]

FUNCTIONS = [
    0x22E25F0, 0x2494230, 0x24942E0, 0x22ED3B0,
    0x22EAE90, 0xCB48730, 0x22ECF60, 0xCB2B580,
    0x22EA150, 0x22ED620, 0x25F9FF0, 0xCB2A0A0,
    0x1603A10, 0x521740,
]


def main():
    data = (ROOT.parent / "bin64" / "CrimsonDesert.exe").read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    assert digest == EXPECTED_SHA, "unsupported executable"
    pe = Pe(data)
    scanner = ImageScanner(pe)
    evidence = []
    for rva, expected, meaning in SITES:
        raw = bytes.fromhex(expected)
        offset = pe.rva_to_off(rva)
        actual = data[offset:offset + len(raw)]
        assert actual == raw, f"0x{rva:X}: {actual.hex()} != {expected}"
        evidence.append(dict(rva=f"0x{rva:X}", bytes=expected, meaning=meaning))

    rtti = json.loads((Path(__file__).parent / "data/rtti_3527.json").read_text())
    named_slots = []
    for cls in rtti["classes"].values():
        if cls["name"] not in ("pa::CommonInteractionActorComponent",
                               "pa::ServerInteractionActorComponent",
                               "pa::ClientInteractionActorComponent"):
            continue
        vt = next(v for v in cls["vtables"] if v["offset"] == 0)
        vtable = int(vt["rva"], 16)
        target = struct.unpack_from("<Q", data, pe.rva_to_off(vtable + 0xC8))[0] - pe.image_base
        assert target == 0x22E25F0, cls["name"]
        named_slots.append(dict(name=cls["name"], vtable=vt["rva"], slot="0xC8", target="0x22E25F0"))
    assert len(named_slots) == 3

    patch, patch_size = 0x22E2657, 5
    function = scanner.function(patch)
    assert function["begin"] == 0x22E25F0
    offset = pe.rva_to_off(patch)
    instruction = next(CS.disasm(data[offset:offset + 16], patch, count=1))
    assert instruction.size == patch_size and instruction.mnemonic == "call"
    assert instruction.operands[0].imm == 0x2494230
    incoming = []
    for section in pe.sections:
        if not section.exec:
            continue
        raw = data[section.raw_ptr:section.raw_ptr + section.raw_size]
        for match in re.finditer(rb"[\xe8\xe9]|\x0f[\x80-\x8f]", raw):
            pos = match.start()
            size = len(match.group()) + 4
            if pos + size > len(raw):
                continue
            target = section.va + pos + size + struct.unpack_from("<i", raw, pos + size - 4)[0]
            if patch <= target < patch + patch_size:
                incoming.append(dict(source=hex(section.va + pos), target=hex(target), kind="raw-rel32"))
        lo = max(0, patch - section.va - 130)
        hi = min(len(raw) - 1, patch - section.va + patch_size + 130)
        for pos in range(lo, hi):
            if raw[pos] not in (0xEB, 0xE0, 0xE1, 0xE2, 0xE3) and not 0x70 <= raw[pos] <= 0x7F:
                continue
            target = section.va + pos + 2 + struct.unpack_from("<b", raw, pos + 1)[0]
            if patch <= target < patch + patch_size:
                incoming.append(dict(source=hex(section.va + pos), target=hex(target), kind="raw-rel8"))
    interior = [row for row in incoming if int(row["target"], 16) != patch]
    assert not interior, interior

    runtime_path = Path(__file__).parent / "data/host_fault_62480_native.json"
    runtime = None
    if runtime_path.exists():
        captured = json.loads(runtime_path.read_text())
        assert captured["exact_known_caller"]
        assert int(captured["caller_return"]["rva"], 16) == 0x22E265C
        comp = captured["component"]
        assert comp["rtti_name"] == "pa::ServerInteractionActorComponent"
        nulls = [e for e in comp["entries"] if not e["holder"]]
        assert len(nulls) == 18
        assert all(e["stage_index"] == 0 and e["parent_index"] == 9 for e in nulls)
        assert all(int(e["condition_string_pointer"], 16) == 0x146EA3D20 for e in nulls)
        runtime = dict(source=str(runtime_path), source_sha256=hashlib.sha256(runtime_path.read_bytes()).hexdigest(),
                       dump=captured["dump"], caller_return="0x22E265C", component=comp["rtti_name"],
                       count=comp["count"], null_count=len(nulls), null_indices=[e["index"] for e in nulls],
                       null_stage=0, null_parent_interaction_index=9,
                       condition_string="Native empty singleton at exe+6EA3D20")

    disassembly = []
    for start in FUNCTIONS:
        fn = scanner.function(start)
        assert fn and fn["begin"] == start, hex(start)
        offset = pe.rva_to_off(start)
        disassembly.append(f"\nFUNCTION 0x{start:X}: {fn}\n")
        for ins in CS.disasm(data[offset:offset + fn["end"] - start], start):
            disassembly.append(f"{ins.address:08X}  {ins.bytes.hex(' '):40s} {ins.mnemonic} {ins.op_str}\n")

    report = dict(
        build="1.0.0.3527", exe_sha256=digest, timestamp=hex(pe.timestamp),
        evidence=evidence, rtti_slots=named_slots,
        layout=dict(vector="0x90", count="0x98", stride="0x38", holder="0x08",
                    condition_string="0x00", stage_id="0x20", parent_interaction_index="0x2E"),
        shared_string=dict(entry_pointer_offset="0x00", chars_offset="0x00", u32_length_offset="0x08",
                           refcount_offset="0x10", empty_singleton_rva="0x6EA3D20",
                           safe_empty_check="Read object=[entry], u32[object+8]==0, chars=[object] nonnull, byte[chars]==0. The native copy checks char data, not the object as direct text.",
                           parent_index="Static InteractionInfo index; do not bound it by component vector count."),
        confirmed_native_null_paths=[
            "22EAE90 appends associated InteractionInfo entries with holder0, stage0 and parent index+2E.",
            "22ECF60 reconstructs a copied entry's condition only when its condition text is nonempty; otherwise holder0 remains.",
            "22EB3E0->CB2B580 only gathers primary copies and refreshes summary flags, not derived wrappers.",
        ],
        encoding=dict(empty="00", explanation="2494230 writes exactly one zero byte for a valid wrapper whose first qword is0; 24942E0 accepts this byte.",
                      reader="22ED3B0 allocates a wrapper before reading, so00 becomes nonnull wrapper with null contents.",
                      forbidden_shortcut="Skipping the serializer without writing00 shifts all following fields."),
        evaluation_caveat="Null and nonnull-empty holders are not globally equivalent. Stage0 returns2 before examining holder. Stage-linked null returns0, whereas a nonnull-empty holder would be dereferenced twice. Do not claim unrestricted gameplay equivalence.",
        additional_wire_caveat="22E25F0/22ED3B0 do not transfer entry+2E. Reader initializes it toFFFF. Derived-entry lifetime/parent association is not preserved by this existing native format.",
        candidate_hook=dict(site=hex(patch), length=patch_size, bytes=instruction.bytes.hex(),
                            signature_before_and_at="488b4f08488bd3e8d41b1b00", function=function,
                            registers=dict(RCX="holder, already loaded from [RDI+8]", RDX="archive, copied from RBX", RDI="entry", RSI="interaction component"),
                            direct_incoming=incoming, interior_incoming=interior,
                            abi="A midhook replacing only null RCX with an aligned immutable16-byte zero object lets the relocated native call emit00. No ownership transfer or mutation occurs on the empty serializer branch. Keep RDX and all unrelated context unchanged; no allocation/game call/log formatting in callback.",
                            scope="Captured return address22E265C proves this caller. Initially constrain to native derived entry with stage low29bits0, parent index!=FFFF and empty condition text, or gather evidence for other cases. Nonnull RCX passes unchanged. Scope does not include all possible stage-linked null entries.",
                            install="Build/SHA profile and live byte guards required. Installation must succeed before enabling build3527 shared-field behavior; no unsupported-build fallback.",
                            limitation="No runtime hook or process changes made. This report establishes byte encoding and patch boundaries, not full gameplay correctness."),
        mod_review="Enabled core hooks do not directly write interaction entries. The legacy whole-entry shop swap is disabled by host configuration. Child/character hold neither repairs condition wrappers nor covers owner-directed/direct serialization.",
        runtime_caller_confirmed=runtime is not None, runtime_evidence=runtime,
    )
    out = Path(__file__).parent / "data/interaction_condition_3527.json"
    asm = Path(__file__).parent / "data/interaction_condition_3527.txt"
    out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    asm.write_text("exe SHA256 " + digest + "\n" + "".join(disassembly), encoding="utf-8")
    print(json.dumps(dict(report=str(out), disassembly=str(asm), byte_assertions=len(evidence),
                          rtti_assertions=len(named_slots), interior_incoming=interior)))


if __name__ == "__main__":
    main()
