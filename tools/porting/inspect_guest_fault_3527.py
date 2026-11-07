#!/usr/bin/env python3
"""Read-only captured-state analysis of the 3527 guest's 7F6876 fault."""
import argparse
import io
import json
from pathlib import Path
import struct

from minidump_inspect import MinidumpFile, CONTEXT, REGISTERS, read_available, locate, hx
from signature_map import Pe, ROOT, ImageScanner


def inspect(path):
    dump = MinidumpFile.parse(str(path))
    try:
        modules = dump.modules.modules
        main = next(m for m in modules if m.name.lower().endswith('crimsondesert.exe'))
        if main.timestamp != 0x6AEA07A6 or main.size != 0x13EB2000:
            raise ValueError('Not the verified 3527 image')
        reader = dump.get_reader()
        pe = Pe((ROOT.parent / 'bin64' / 'CrimsonDesert.exe').read_bytes())
        scanner = ImageScanner(pe)
        names = {int(v['rva'], 0): c['name'] for c in json.loads(
            (Path(__file__).parent / 'data' / 'rtti_3527.json').read_text())['classes'].values()
                 for v in c['vtables']}

        def value(at, fmt='<Q'):
            if not at:
                return None
            data = read_available(reader, at, struct.calcsize(fmt))
            return struct.unpack(fmt, data)[0] if len(data) == struct.calcsize(fmt) else None

        def raw(at, count):
            return read_available(reader, at, count).hex(' ') if at else None

        def obj(at):
            vt = value(at)
            return dict(address=hx(at) if at else None, first_qword=hx(vt) if vt is not None else None,
                        rtti=names.get(vt - main.baseaddress) if vt is not None else None)

        rec = next(e for e in dump.exception.exception_records
                   if e.ExceptionRecord.ExceptionAddress == main.baseaddress + 0x7F6876)
        dump.file_handle.seek(rec.ThreadContext.Rva)
        ctx = CONTEXT.parse(io.BytesIO(dump.file_handle.read(rec.ThreadContext.DataSize)))
        cc = ctx.Rcx
        report = dict(dump=str(path.resolve()), thread_id=rec.ThreadId,
                      exception_code=hx(rec.ExceptionRecord.ExceptionCode_raw),
                      exception_information=[hx(x) for x in rec.ExceptionRecord.ExceptionInformation],
                      registers={r: hx(getattr(ctx, r)) for r in REGISTERS},
                      fault='cmp byte ptr [rdx+0x116],r15b; RDX is selected layer current action (+0x10)')
        control = obj(cc)
        control.update(bytes=raw(cc, 0x160), actor=value(cc + 8), movement=value(cc + 0x108),
                       selector_object=value(cc + 0x30), restore_data=value(cc + 0x40),
                       pending=value(cc + 0xA8, '<B'), default_layers=[value(cc + 0x70), value(cc + 0x78)])
        selector = control['selector_object']
        control['selected_layer'] = value(selector + 0x1B, '<B') if selector else None
        restore = control['restore_data']
        if restore:
            control['restore'] = dict(**obj(restore), byte_30=value(restore + 0x30, '<B'),
                                      raw_30=raw(restore + 0x30, 0x30), field_f8=value(restore + 0xF8))
        layers = []
        for i in range(2):
            layer = value(cc + 0x88 + i * 8)
            item = dict(index=i, **obj(layer))
            if layer:
                item.update(current=value(layer + 0x10), playback=value(layer + 0x18),
                            hash_3c=value(layer + 0x3C, '<I'), chart_118=value(layer + 0x118),
                            bytes=raw(layer, 0x140))
            layers.append(item)
        control['layers'] = layers
        report['character_control'] = control
        actor = control['actor']
        if actor:
            report['actor'] = dict(**obj(actor), key=value(actor + 0x60, '<I'),
                                   controller=value(actor + 0x90, '<I'), state=value(actor + 0x5E, '<H'),
                                   ready=value(actor + 0x4A, '<B'), bytes=raw(actor, 0xD0))
        report['movement_state'] = dict(address=hx(ctx.Rdi), first_pointer=value(ctx.Rdi),
                                        bits_b0=value(ctx.Rdi + 0xB0, '<I'),
                                        bits_b4=value(ctx.Rdi + 0xB4, '<I'), bytes=raw(ctx.Rdi, 0xC0))
        # These deltas come from verified full prologues, including every push.
        # They are not obtained by scanning for module-looking stack words.
        frames = [(0x7F66A0, 0x258), (0x7F2C20, 0x88), (0x94F7F0, 0x88),
                  (0x99B380, 0x58), (0x15DDE90, 0x48), (0x15F7D80, 0x58)]
        cursor = ctx.Rsp
        report['prologue_derived_stack'] = []
        for function, delta in frames:
            slot = cursor + delta
            target = value(slot)
            frame = dict(function=hx(function), stack_adjustment=hx(delta),
                         return_slot=hx(slot), fault_rsp_offset=hx(slot - ctx.Rsp),
                         return_address=locate(modules, target) if target else None)
            if target and main.baseaddress <= target < main.baseaddress + main.size:
                frame['return_function'] = scanner.function(target - main.baseaddress)
                off = pe.rva_to_off(target - main.baseaddress - 10)
                frame['static_bytes_before_return'] = pe.data[off:off + 10].hex(' ') if off else None
            report['prologue_derived_stack'].append(frame)
            cursor = slot + 8
        report['stack_basis'] = ('Each return slot uses statically verified prologue stack adjustment. '
                                 'Tail-called functions may be absent; raw scan candidates are in generic report.')
        report['native_role'] = ('Worker loop 15F7D80 -> 15DDE90 -> 15E9B20 tail dispatch '
                                 '-> 99B380 actor iteration -> 94F7F0 CC phase '
                                 '-> 7F2C20 movement state -> 7F66A0 current-action reader.')
        report['coverage'] = dict(existing_guard_entries=['0x944E70', '0x946060'],
                                 fault_path_cc_entry='0x94F7F0',
                                 finding='Fault path reaches the CC through a separate direct call; neither guarded entry occurs in this chain.',
                                 report_timing='RemoteUpdateGuard holding lines drain a queue; log time is not the time the hook recorded the key.')
        report['restore_capture_note'] = ('The restore-data pointer is captured in the CC, but its target page '
                                          'is absent from this dump; null report fields are unavailable, not zero.')
        return report
    finally:
        dump.file_handle.close()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('dump', type=Path)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    result = inspect(args.dump)
    args.out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('thread_id', 'character_control', 'actor', 'prologue_derived_stack')}, indent=2))
