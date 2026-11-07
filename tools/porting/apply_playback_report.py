"""Offline evidence for the captured 3527 null selected-playback flag read."""
import argparse
import hashlib
import io
import json
import logging
from pathlib import Path
import struct

from minidump_inspect import MinidumpFile, CONTEXT, read_available
from signature_map import ROOT, Pe, CS, ImageScanner


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dump', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    pe = Pe((ROOT.parent / 'bin64/CrimsonDesert.exe').read_bytes())
    sha = hashlib.sha256(pe.data).hexdigest()
    assert sha == 'dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841'
    scanner = ImageScanner(pe)
    function = scanner.function(0x25191F4)
    assert function['begin'] == 0x2517A80
    start = pe.rva_to_off(function['begin'])
    code = list(CS.disasm(pe.data[start:start + function['end'] - function['begin']], function['begin']))
    by_rva = {i.address: i for i in code}
    secondary = scanner.function(0x251A1AD)
    secondary_start = pe.rva_to_off(secondary['begin'])
    secondary_code = list(CS.disasm(pe.data[secondary_start:secondary_start + secondary['end'] - secondary['begin']],
                                   secondary['begin']))
    by_rva.update({i.address: i for i in secondary_code})
    expected = [
        (0x2517AE2, 'mov', 'r15, rcx'),
        (0x25191E0, 'mov', 'rax, qword ptr [r15 + 0x30]'),
        (0x25191E4, 'movzx', 'ebx, byte ptr [rax + 0x1b]'),
        (0x25191E8, 'mov', 'rax, qword ptr [r15 + rbx*8 + 0x88]'),
        (0x25191F0, 'mov', 'rcx, qword ptr [rax + 0x18]'),
        (0x25191F4, 'mov', 'rax, qword ptr [rcx + 0x28]'),
        (0x25191F8, 'movzx', 'edi, byte ptr [rax + 0x7e]'),
        (0x25191FC, 'mov', 'rcx, qword ptr [r15 + 8]'),
        (0x251923A, 'test', 'rax, rax'),
        (0x2519248, 'cmp', 'qword ptr [rcx + 0x18], 0'),
        (0x251924D, 'je', '0x2519284'),
        (0x251924F, 'cmp', 'dl, bl'),
        (0x2519251, 'jne', '0x2519271'),
        (0x2519253, 'test', 'dil, dil'),
        (0x2519256, 'je', '0x2519271'),
        (0x251928E, 'cmp', 'dl, 2'),
        (0x2519291, 'jb', '0x2519226'),
        (0x2519293, 'mov', 'rbx, qword ptr [rsp + 0x90]'),
        (0x25192A3, 'call', 'qword ptr [rax + 0x20]'),
        (0x251A199, 'mov', 'r14, rcx'),
        (0x251A19C, 'movzx', 'r12d, byte ptr [rax + 0x1b]'),
        (0x251A1A9, 'mov', 'rdx, qword ptr [rax + 0x18]'),
        (0x251A1AD, 'mov', 'rax, qword ptr [rdx + 0x28]'),
        (0x251A1B1, 'movzx', 'ebp, byte ptr [rax + 0x7e]'),
        (0x251A1B5, 'mov', 'rax, qword ptr [rcx + 8]'),
        (0x251A1F6, 'cmp', 'qword ptr [rbx + 0x18], 0'),
        (0x251A1FB, 'je', '0x251a23d'),
        (0x251A20D, 'cmp', 'dil, r12b'),
        (0x251A210, 'jne', '0x251a22b'),
        (0x251A212, 'test', 'bpl, bpl'),
        (0x251A215, 'je', '0x251a22b'),
    ]
    checks = []
    for rva, mnemonic, operands in expected:
        instruction = by_rva[rva]
        assert (instruction.mnemonic, instruction.op_str) == (mnemonic, operands), hex(rva)
        checks.append(dict(rva=hex(rva), bytes=instruction.bytes.hex(), instruction=f'{mnemonic} {operands}'))
    assert sum(by_rva[r].size for r in (0x25191F4, 0x25191F8)) == 8
    assert sum(by_rva[r].size for r in (0x251A1AD, 0x251A1B1)) == 8
    internal_targets = [i.op_str for i in code if i.mnemonic.startswith('j') and
                        i.op_str.startswith('0x') and 0x25191F4 < int(i.op_str, 16) < 0x25191FC]
    assert not internal_targets
    assert not [i for i in secondary_code if i.mnemonic.startswith('j') and i.op_str.startswith('0x') and
                0x251A1AD < int(i.op_str, 16) < 0x251A1B5]
    # Small crash dumps omit the PEB. The library logs that optional parse
    # failure, although exception registers and captured pages remain usable.
    prior_logging = logging.root.manager.disable
    logging.disable(logging.CRITICAL)
    try:
        dump = MinidumpFile.parse(str(args.dump))
    finally:
        logging.disable(prior_logging)
    try:
        module = next(m for m in dump.modules.modules if m.name.lower().endswith('crimsondesert.exe'))
        assert (module.timestamp, module.size) == (0x6AEA07A6, 0x13EB2000)
        exc = next(e for e in dump.exception.exception_records
                   if e.ExceptionRecord.ExceptionAddress == module.baseaddress + 0x25191F4)
        dump.file_handle.seek(exc.ThreadContext.Rva)
        ctx = CONTEXT.parse(io.BytesIO(dump.file_handle.read(exc.ThreadContext.DataSize)))
        reader = dump.get_reader()
        def val(address, fmt='<Q'):
            data = read_available(reader, address, struct.calcsize(fmt)) if address else b''
            return struct.unpack(fmt, data)[0] if len(data) == struct.calcsize(fmt) else None
        cc = ctx.R15
        actor = val(cc + 8)
        selector = val(cc + 0x30)
        layers = []
        for index in (0, 1):
            layer = val(cc + 0x88 + index * 8)
            layers.append(dict(index=index, address=layer, current=val(layer + 0x10) if layer else None,
                               playback=val(layer + 0x18) if layer else None))
        captured = dict(cc=hex(cc), actor=hex(actor) if actor else None,
                        key=hex(val(actor + 0x60, '<I')) if actor else None,
                        controller=hex(val(actor + 0x90, '<I')) if actor else None,
                        actor_c8=val(actor + 0xC8, '<B') if actor else None,
                        applied_layer_mask=list(read_available(reader, ctx.R12, 2)),
                        selected_register=ctx.Rbx,
                        selected_memory=val(selector + 0x1B, '<B') if selector else None,
                        layers=layers, rcx=hex(ctx.Rcx), rsp=hex(ctx.Rsp),
                        caller_return=hex(val(ctx.Rsp + 0x4C8) or 0))
        # EBX was loaded directly from selector+1B before the failing read.
        # The selector target page itself is absent in this minidump.
        assert ctx.Rcx == 0 and captured['selected_register'] == 0
        assert layers[0]['playback'] == 0 and layers[1]['playback']
        assert captured['actor_c8'] == 0 and captured['applied_layer_mask'] == [0, 1]
        assert captured['caller_return'] == hex(module.baseaddress + 0x946495)
    finally:
        dump.file_handle.close()
    report = dict(exe_sha256=sha, dump=str(args.dump.resolve()), captured=captured,
                  assertions=checks, hook_rva='0x25191F4', hook_bytes=8, resume='0x25191FC',
                  secondary_hook_rva='0x251A1AD', secondary_resume='0x251A1B5',
                  internal_branches_into_hook=internal_targets,
                  reasoning='DIL is tested only after the per-layer playback check and only for the selected index. '
                            'With null selected playback its value is unused. Clearing EDI and skipping only the '
                            'two reads retains other-layer blend work and the native unlock/cleanup after2519293.',
                  scope='Remote user copies only. Native pending/restore execution is never returned from early.')
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(assertions=len(checks), captured=captured), indent=2))


if __name__ == '__main__':
    main()
