"""Read-only native assertion inventory for the build-3527 loading milestone."""
import hashlib
import json
import struct

from signature_map import ROOT, Pe, CS, ImageScanner


def main():
    pe = Pe((ROOT.parent / 'bin64' / 'CrimsonDesert.exe').read_bytes())
    scanner = ImageScanner(pe)
    spans = [
        (0x2D04590, 0x4B, 'ToTarget constructor: RCX=finder, RDX=actor; vfE8 returns session'),
        (0x2D04530, 0x57, 'ToTarget scalar deleting destructor; flag0 only resets base vtable'),
        (0x2D045E0, 0x5F, 'ToTarget recipient append from finder+10'),
        (0x2D04640, 0x0C, 'ToTarget session equality'),
        (0x2C142A0, 5, '2322/native2384 emitter original head'),
        (0x2C142A5, 0x66, 'Emitter frame, actor/finder ABI, TLS250, recipients2B0/count2B8, ScopeFind'),
        (0x2C1437C, 0x1B, 'Emitter serializes actor key+60 as exactly four bytes'),
        (0x2C14397, 6, 'Emitter native2384 opcode0x950'),
        (0x2ADB2C4, 0x38, 'Native stack ToTarget recipe before emitter call'),
        (0x2ADB2FC, 0x13, 'Native actor/finder argument setup, emitter call, stack destructor reset'),
        (0x2E6A324, 0x4E, '2144/native2340 finder recipe and emitter call'),
        (0x2C147D3, 6, '2144 writer native2340 opcode0x924'),
        (0x2E6A39F, 0x14, '2144 wrapper sets R8 field, RCX component and calls MoveFieldCompleteCore'),
        (0x2E6A72A, 0x13, 'Core register provenance: R15=R8 field; R14=[RCX+8] actor'),
        (0x2E6B1DE, 0x4C, '3066/native3029 finder recipe and emitter call before milestone'),
        (0x2C14C67, 6, '3066 writer native3029 opcode0xBD5'),
        (0x2E6B5E0, 0x23, 'Actor+A0 user, user+60 identity compared with fieldR15+180; nonowner skips native2322'),
        (0x2E6B628, 0x1B, 'Owner user+D0 main, main+68 components, components+A8 sequencer, component+18 manager'),
        (0x2E6B659, 0x1C, 'Owner-only native2322 call then owner/nonowner converge before hook'),
        (0x2E6B675, 0x10, 'Milestone hook: original seven-byte instruction reads fieldR15+2D8'),
        (0x2BD571A, 0x28, 'Scope lookup TLS250, mode2AC and count2A0'),
        (0x2BD5C86, 0x2E, 'Current scope flush TLS250, current2A8 and array298'),
        (0x29AAF36, 9, 'ServerRunThread realm TLS1EB=1'),
        (0xBE5A10, 8, 'ServerChildOnlyInGameActor vfE8 returns actor+D0 session'),
        (0x2D0C4F0, 8, 'ServerUserActor vfE8 returns user+1F8 session'),
        (0x2D0467A, 0x1C, 'Actor+68 -> components+1A0 -> TransformSync+508 passed to FieldMover GetField'),
        (0x2E61188, 3, 'GetField preserves FieldMover RCX as RSI'),
        (0x2E61263, 4, 'GetField reads FieldMover+10, hence TransformSync+518 field'),
        (0x2D8CA6F, 3, 'ServerField constructor preserves this in RDI'),
        (0x2D8CC0E, 10, 'ServerField constructor installs verified RTTI vtable5EF0908'),
        (0x2D8CDDF, 10, 'ServerField constructor initializes field key at+2D0 to0x30000'),
        (0x2DA24B2, 0x13, 'Native field identity setter writes lowword+2D0 and typebyte+2D2'),
        (0xC9CB91, 0x11, '2322 client handler obtains field sequencer and writes readiness+259=2'),
        (0x856090, 0x4F, 'Loading state11 waits for owner sequencer+259>=2 then BA2F70 predicate before state12'),
    ]
    assertions = []
    for rva, size, meaning in spans:
        off = pe.rva_to_off(rva)
        buf = pe.data[off:off + size]
        assertions.append(dict(rva=hex(rva), bytes=buf.hex(' '), meaning=meaning,
                               instructions=[f'{i.address:X}: {i.mnemonic} {i.op_str}' for i in CS.disasm(buf, rva)]))
    f = scanner.function(0x2E6B675)
    off = pe.rva_to_off(f['begin'])
    insns = list(CS.disasm(pe.data[off:off + f['end'] - f['begin']], f['begin']))
    ins = {i.address: i for i in insns}
    edges = {}
    for i in insns:
        nxt = i.address + i.size
        target = i.operands[0].imm if i.operands and i.operands[0].type == 2 else None
        successors = []
        if i.mnemonic.startswith('j'):
            if target in ins:
                successors.append(target)
            if i.mnemonic != 'jmp' and nxt in ins:
                successors.append(nxt)
        elif not i.mnemonic.startswith('ret') and nxt in ins:
            successors.append(nxt)
        edges[i.address] = successors

    def reaches_hook(blocked=None):
        todo = [f['begin']]
        seen = set()
        while todo:
            address = todo.pop()
            if address == blocked or address in seen:
                continue
            if address == 0x2E6B675:
                return True
            seen.add(address)
            todo.extend(edges.get(address, []))
        return False

    reverse = {}
    for a, successors in edges.items():
        for b in successors:
            reverse.setdefault(b, []).append(a)
    todo = [0x2E6B675]
    ancestors = set()
    while todo:
        a = todo.pop()
        if a not in ancestors:
            ancestors.add(a)
            todo.extend(reverse.get(a, []))
    nonvolatile_writes = []
    for i in insns:
        if i.address not in ancestors:
            continue
        _, writes = i.regs_access()
        names = [i.reg_name(r) for r in writes]
        if any(n in ('r14', 'r14d', 'r15', 'r15d') for n in names):
            nonvolatile_writes.append(f'{i.address:X}: {i.mnemonic} {i.op_str}')
    inbound = []
    # Audit all executable sections, including the protected writable .text.
    # Unaligned false positives would also be reported and reviewed.
    for sec in pe.sections:
        if not sec.exec:
            continue
        b = pe.data[sec.raw_ptr:sec.raw_ptr + sec.raw_size]
        for opcode, length, displacement in [(b'\xe8', 5, 1), (b'\xe9', 5, 1)] + [(bytes([15, n]), 6, 2) for n in range(0x80, 0x90)]:
            at = b.find(opcode)
            while at >= 0:
                if at + length <= len(b):
                    dest = sec.va + at + length + struct.unpack_from('<i', b, at + displacement)[0]
                    if 0x2E6B675 < dest < 0x2E6B67C:
                        inbound.append(dict(site=hex(sec.va + at), target=hex(dest)))
                at = b.find(opcode, at + 1)
    for i in insns:
        if i.mnemonic.startswith('j') and i.operands[0].type == 2 and 0x2E6B675 < i.operands[0].imm < 0x2E6B67C:
            inbound.append(dict(site=hex(i.address), target=hex(i.operands[0].imm)))
    tables = {}
    for label, rva, count in [('finder', 0x5EEC840, 3), ('base_finder', 0x5EEC860, 1),
                              ('child_actor_vfE8', 0x5EE4DA8 + 0xE8, 1), ('user_vfE8', 0x5EECCA8 + 0xE8, 1)]:
        values = struct.unpack_from('<' + 'Q' * count, pe.data, pe.rva_to_off(rva))
        tables[label] = dict(rva=hex(rva), targets=[hex(v - 0x140000000) for v in values])
    report = dict(build='1.0.0.3527', exe_sha256=hashlib.sha256(pe.data).hexdigest(),
                  timestamp=hex(pe.timestamp), assertions=assertions, tables=tables,
                  hook=dict(rva='0x2E6B675', stolen_bytes=7, field='r15', actor='r14',
                            core='0x2E6A700', inbound_inside_stolen=inbound,
                            registers_written_on_paths_to_hook=nonvolatile_writes,
                            dominates_hook={hex(a): not reaches_hook(a) for a in [0x2E6A72D, 0x2E6A739, 0x2E6B225, 0x2E6B670]},
                            native_owner_call_dominates=not reaches_hook(0x2E6B667)),
                  layout=dict(tls_realm='0x1EB', tls_send_ctx='0x250', actor_key='0x60', actor_user='0xA0',
                              actor_components='0x68', components_ts='0x1A0', ts_field='0x518',
                              user_key='0x60', user_main='0xD0', user_session='0x1F8', field_owner='0x180', field_key='0x2D0',
                              scope_count='0x2A0', scope_current='0x2A8', scope_mode='0x2AC', scope_array='0x298'),
                  layout_extra_evidence='field_mover_layout_3527.json; guest_loading_run8_3527.json; native_coop_join8_phase_fixed_journal_3527.json')
    path = ROOT / 'tools/porting/data/observer_milestone_3527.json'
    path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(path)
    print(json.dumps(report['hook'], indent=2))


if __name__ == '__main__':
    main()
