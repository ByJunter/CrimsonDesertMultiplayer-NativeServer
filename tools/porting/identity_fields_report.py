"""Check build3527 C2 identity fields and true login phases against native code."""
import hashlib
import json
import re
import struct

import payload_disasm
from signature_map import Pe, ROOT, ImageScanner


def main():
    pe = Pe((ROOT.parent / 'bin64/CrimsonDesert.exe').read_bytes())
    digest = hashlib.sha256(pe.data).hexdigest()
    assert digest == 'dc4237f21d6ce1e49f8e5d6fa89bf803fbbe6549123078efdc393265c7bf6841'

    def raw(rva, size):
        off = pe.rva_to_off(rva)
        return pe.data[off:off+size]

    assert struct.unpack('<Q', raw(0x5EE7610+0x50, 8))[0]-pe.image_base == 0x2BAFA40
    assert struct.unpack('<Q', raw(0x5EF3048+0x140, 8))[0]-pe.image_base == 0x2E13F40
    proof = []
    for rva, expected, meaning in [
        (0x2BAFA40, 'e9bb408d0b', 'Named ServerStatus load thunk to0xE483B00'),
        (0xE483B4E, '488b879000000048b9ffffffffffffff7f488908', 'Load u64 pointer atStatus+0x90 and write native characterNo through it'),
        (0x18C7DC8, '488d9798000000488bcbe819b0d3ff', 'Name is separate atStatus+0x98, passed to string reader'),
        (0x18C7E6D, '488bb790000000488b0341b808000000', 'Common Status unpack obtains u64 pointer at+0x90 and reads8 bytes'),
        (0x18C7E8F, '488b4548488906', 'Unpack stores received characterNo through the pointer'),
        (0xCBA7A1A, '488b41184c8b108b40084c69d8d0000000', 'Lobby+0x18 list; list+0 data,+8 count, stride0xD0'),
        (0xCBA7A33, '498b00493902', 'Lobby duplicate lookup compares entry+0 characterNo'),
        (0xE489FC2, '488b7108488b86a00000004c8b5068498b5a10', 'Login validation follows Status+8 actor,actor+A0 user,user+68 components,table+10 lobby'),
        (0xE489FF5, '488b4b18488b018b49084869d1d0000000', 'Login validation checks same lobby list+18,count8,strideD0'),
        (0xE48A00E, '498b0e483908', 'Login validation compares requested characterNo to entry+0'),
        (0xE489FB2, '4883ec20', 'Caller has only32-byte outgoing shadow space; no stack arguments'),
        (0xE48A09E, 'e82d6952f3', 'Caller first invokes LoadSlot0x19B09D0'),
        (0xE48A0BE, '4889e9e89a6c72f4', 'On successful LoadSlot, RCX=same Status and call true phases0x2BB0D60'),
        (0x2BB0D60, '44894c24204c89442418535556574154415641574881ec800000004c8bfa4c8b7108498b7e78', 'True phases prologue; result inRDX,actor=[RCX+8],components=[actor+78]'),
        (0x2BB0D86, '418bae8000000048c1e504', 'Component count actor+0x80, stride16'),
        (0x2BB0DC3, 'ff5068', 'First phase invokes each component vf+0x68'),
        (0x2BB1162, 'ff5078', 'Later phase invokes each component vf+0x78'),
        (0x2BB1222, '498bc7', 'Return original int* result retained inR15'),
    ]:
        code = bytes.fromhex(expected)
        assert raw(rva,len(code)) == code, hex(rva)
        proof.append(dict(rva=hex(rva), bytes=expected, meaning=meaning))
    start, patch = 0x2BB0D60, 14
    refs = []
    for section in pe.sections:
        if not section.exec:
            continue
        region = pe.data[section.raw_ptr:section.raw_ptr+section.raw_size]
        for match in re.finditer(rb'[\xe8\xe9]|\x0f[\x80-\x8f]', region):
            at = match.start()
            length = len(match.group())+4
            if at+length > len(region):
                continue
            target = section.va+at+length+struct.unpack_from('<i',region,at+length-4)[0]
            if start <= target < start+patch:
                refs.append(dict(source=hex(section.va+at),target=hex(target),kind='rel32'))
        lo = max(0,start-section.va-130)
        hi = min(len(region)-1,start-section.va+patch+130)
        for at in range(lo,hi):
            if region[at] not in (0xEB,0xE0,0xE1,0xE2,0xE3) and not 0x70 <= region[at] <= 0x7F:
                continue
            target=section.va+at+2+struct.unpack_from('<b',region,at+1)[0]
            if start <= target < start+patch:
                refs.append(dict(source=hex(section.va+at),target=hex(target),kind='rel8_potential'))
    assert all(not start < int(r['target'],16) < start+5 for r in refs), refs
    # A raw branch encoding in packed/chaff bytes targets entry+12. Preserve the
    # finding: it is outside a five-byte patch, and no executable flow proves it
    # is a real instruction. Do not silently claim a clean fourteen-byte scan.
    for ref in refs:
        ref['source_pdata_function'] = ImageScanner(pe).function(int(ref['source'],16))
    result = dict(build='1.0.0.3527',exe_sha256=digest,
        status=dict(old_number_pointer_offset='0x70',number_pointer_offset='0x90',name_offset='0x98',
                    owner_offset=8,server_vtable='0x5EE7610',native_constant_store='0xE483B5F',
                    mutation='Change the aligned u64 pointed to by Status+0x90; never overwrite the pointer itself.',
                    common_wire_unpack='0x18C7C80',wire_identity_store='0x18C7E93'),
        unchanged_c2_fields=dict(actor_components='0x68',status_component_slot='0x20',actor_user='0xA0',
                                 lobby_component_slot='0x10',component_owner='0x08',lobby_list='0x18',
                                 list_data='0x00',list_count='0x08',lobby_entry_stride='0xD0',lobby_entry_number='0x00'),
        phases=dict(old_entry='0x2941020',entry='0x2BB0D60',head=raw(start,38).hex(' '),
                    function=ImageScanner(pe).function(start),raw_incoming_to_first14bytes=refs,
                    clean_patch_boundary=5,
                    raw_chaff_caveat='Raw0x327C55B encodes jl entry+12, outside the5-byte patch. Source has no .pdata range and is near packed thunk327C530, which jumps away. Actual executable flow from327C55B is unproven; do not claim14-byte raw scan clean.',
                    caller='0xE489FA0',call_site='0xE48A0C1',preceding_load_slot_call='0xE48A09E',
                    abi='int* __fastcall(component*,int*,register3,register4); no stack arguments. First two arguments consumed ascomponent andresult. Caller does not resetR8/R9 afterLoadSlot; forwarding old four-register signature is sufficient.',
                    order='Validate requested characterNo against user lobby; successful LoadSlot; invoke component phases; return result.',
                    wrong_previous_candidate=dict(entry='0x2E13FB0',reason='Caller0x2E13F40 is named ServerKnowledgeActorComponent vtable0x5EF3048 slot+0x140, unrelated to Status login phases. Its nine-argument ABI must not be attributed to phases.')),
        assertions=proof,
        limitations=['No runtime mutation or hook activation performed by this audit.',
                     'Source review confirms corrected offset readers and four-register phases hook, but deployment/build readiness belongs to root.',
                     'Global uniqueness scan for3527 remains disabled in current identity profile; this audit does not remap that optional scan.'])
    out = ROOT/'tools/porting/data/identity_fields_3527.json'
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(out),assertions=len(proof),incoming=refs)))


if __name__=='__main__':
    main()
