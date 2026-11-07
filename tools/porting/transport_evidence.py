#!/usr/bin/env python3
"""Recheck and export static build-3527 transport evidence; never modify the game.

Seed addresses were identified by unique masked signatures, RTTI and native
call relationships. Assertions keep the evidence tied to those observations.
This is not a claim that native multiplayer has been verified in a live game.
"""
from __future__ import annotations
import json
from pathlib import Path
import re
import struct
from capstone.x86 import X86_OP_IMM
from signature_map import ROOT, Pe, CS, ImageScanner, relative_mask
from aob_scan_file import load_sigs, compile_pattern
from collections import Counter

DATA = Path(__file__).parent / "data"


def main():
    pe = Pe((ROOT.parent / "bin64" / "CrimsonDesert.exe").read_bytes())
    assert pe.timestamp == 0x6AEA07A6
    scanner = ImageScanner(pe)
    signatures = json.loads((DATA / "signature_mapping_2976_3527.json").read_text())
    rtti = json.loads((DATA / "rtti_3527.json").read_text())
    assert signatures["timestamp"] == pe.timestamp

    def unique(name):
        found = {m["rva"] for r in signatures["signatures"] if r["name"] == name
                 and len(r["matches"]) == 1 for m in r["matches"]}
        assert len(found) == 1, (name, found)
        return found.pop()

    def code(rva, whole=False):
        f = scanner.function(rva)
        assert f, hex(rva)
        start = f["begin"] if whole else rva
        off = pe.rva_to_off(start)
        return list(CS.disasm(pe.data[off:off+f["end"]-start], start))

    def calls(insns):
        return [(i.address, i.operands[0].imm) for i in insns if i.mnemonic == "call"
                and i.operands and i.operands[0].type == X86_OP_IMM]

    send, sync = unique("kSessionSendHead"), unique("kSyncSessionSendHead")
    alloc, pump = unique("kAllocThreadBytes"), unique("kNetLoopPumpAob")
    ca, cb = calls(code(send)), calls(code(sync))
    assert len(ca) == len(cb) == 1 and ca[0][1] == cb[0][1]
    queue = ca[0][1]
    recv = None
    pc = code(pump)
    for n, i in enumerate(pc):
        if i.mnemonic == "lea" and i.op_str == "r9, [r14 + 3]":
            near = pc[n:n+5]
            assert any(j.mnemonic == "mov" and j.op_str == "rdx, qword ptr [r15 + 0x10]" for j in near)
            cs = calls(near)
            assert len(cs) == 1
            recv = cs[0][1]
            break
    assert recv is not None
    candidates = {m["rva"] for r in signatures["signatures"] if r["name"] == "kClientPostPacketHead" for m in r["matches"]}
    posts = [a for a in candidates if any(t == alloc for _, t in calls(code(a)))
             and any(i.mnemonic == "mov" and i.op_str == "ecx, 0x8030" for i in code(a))]
    assert len(posts) == 1, posts
    post = posts[0]
    post_code = code(post)
    alloc_global = None
    for n, i in enumerate(post_code):
        if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM and i.operands[0].imm == alloc:
            assert post_code[n-1].mnemonic == "jmp" and post_code[n-2].mnemonic == "call"
            alloc_global = post_code[n-2].operands[0].imm
            break
    assert alloc_global
    assert any(i.mnemonic == "mov" and i.op_str == "ebp, 0x1fe" for i in post_code)
    tls_indices = [i.address+i.size+i.disp for i in code(alloc)
                   if i.mnemonic == "mov" and i.op_str.startswith("r8d, dword ptr [rip")]
    assert len(tls_indices) == 1
    tls_index = tls_indices[0]
    doc_signatures = {s.name:s for s in load_sigs(str(ROOT / "docs/research/troc-dispatch.md"))}
    def documented(name):
        sig = doc_signatures[name]
        pattern = compile_pattern(sig.pattern)
        hits = [section.va + m.start() for section, buf in scanner.regions for m in pattern.finditer(buf)]
        assert len(hits) == 1, (name, hits)
        return hits[0]
    server_recv = documented("troc_server_on_recv_packet")
    server_send = documented("troc_server_send_packet")
    server_exec = documented("troc_server_execute")
    client_exec = documented("troc_client_execute")
    def callers_and_globals(target):
        sites, counts = [], Counter()
        for sec, buf in scanner.regions:
            for m in re.finditer(b"\xe8....", buf, re.S):
                site = sec.va + m.start()
                if site+5+struct.unpack_from("<i",m[0],1)[0] != target:
                    continue
                sites.append(site)
                preceding = buf[max(0,m.start()-100):m.start()]
                for load in re.finditer(b"\x48\x8b\x0d....", preceding, re.S):
                    a = site-len(preceding)+load.start()
                    counts[a+7+struct.unpack_from("<i",load[0],3)[0]] += 1
        return sites, counts
    server_recv_calls, _ = callers_and_globals(server_recv)
    assert len(server_recv_calls) == 1
    server_loop = scanner.function(server_recv_calls[0])["begin"]
    server_send_calls, server_slots = callers_and_globals(server_send)
    post_calls, client_slots = callers_and_globals(post)
    assert len(server_slots) == 1 and next(iter(server_slots.values())) == len(server_send_calls)
    server_slot = next(iter(server_slots))
    assert len(client_slots) == 1 and next(iter(client_slots.values())) >= 200
    client_slot = next(iter(client_slots))
    nw = [c for c in rtti["classes"].values() if c["name"] == "pa::NwSession"]
    assert len(nw) == 1
    tables = {int(v["rva"],16) for v in nw[0]["vtables"] if v["offset"] == 0}
    ctor_refs = []
    for section, buf in scanner.regions:
        for m in re.finditer(b"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]....", buf, re.S):
            site = section.va+m.start()
            target = site+7+struct.unpack_from("<i",m[0],3)[0]
            if target not in tables:
                continue
            f = scanner.function(site)
            if f and any(i.mnemonic == "mov" and i.op_str == "byte ptr [rcx + 0xa9], dl" for i in code(f["begin"])):
                ctor_refs.append((site,target,f["begin"]))
    assert len(ctor_refs) == 1, ctor_refs
    ctor_site, ctor_vt, ctor = ctor_refs[0]
    def named_vtable(mangled):
        cls = rtti["classes"][mangled]
        candidates = [v for v in cls["vtables"] if v["offset"] == 0]
        assert len(candidates) == 1
        return int(candidates[0]["rva"],16)
    vtable_rows = [
        ("kNwVirtualAsyncSessionVtable",0x57260C0,".?AVNwVirtualAsyncSession@pa@@"),
        ("kNwAsyncSessionVtable",0x5726140,".?AVNwAsyncSession@pa@@"),
        ("kToNearExceptOneControlVtable",0x5B25358,".?AVToNearExceptOneControlSendTargetFinder@pa@@"),
        ("kToNearExceptOneSelfVtable",0x5B25378,".?AVToNearExceptOneSelfSendTargetFinder@pa@@"),
        ("kToNearVtable",0x5B25418,".?AVToNearSendTargetFinder@pa@@"),
        ("kWindowsLock3Vtable",0x557C120,".?AV?$WindowsLock@$02@pa@@"),
    ]
    vt = {symbol:named_vtable(name) for symbol,old,name in vtable_rows}
    def slot(symbol,n):
        value = pe.u64(vt[symbol]+n*8)
        assert value is not None and pe.is_exec_rva(value-pe.image_base)
        return value-pe.image_base
    assert slot("kNwVirtualAsyncSessionVtable",9) == send
    refresh_sequence = unique("kSlotRefreshReqSequenceBytes")
    seq_record = next(r for r in signatures["signatures"] if r["name"] == "kSlotRefreshReqSequenceBytes")
    seq_off = pe.rva_to_off(refresh_sequence)
    seq = list(CS.disasm(pe.data[seq_off:seq_off+seq_record["length"]],refresh_sequence))
    assert seq[0].mnemonic == "mov" and seq[0].op_str.startswith("rax, qword ptr [rip")
    server_lookup = seq[0].address+seq[0].size+seq[0].disp
    refresh_calls = calls(seq)
    assert len(refresh_calls) == 1
    refresh = refresh_calls[0][1]
    rows = [
        ("kSessionSend",0x1437740,send,"Unique head; session+0xC8, request+0x30, single call to shared queue push."),
        ("kSyncSessionSend",0x14380B0,sync,"Unique head; session+0xE0, single call to shared queue push."),
        ("kQueuePush",0x35DF50,queue,"Only direct call in both session send methods; lock, enqueue pointer, increment count+0x58, signal event."),
        ("kNetLoopPump",0x35BAD4,pump,"Unique exact 39-byte mid-hook signature; native loop consumes buffers and calls client receive."),
        ("kClientOnRecvPacket",0x35CA00,recv,"Native pump call after size=word[buffer+0x800A]-0x1F, r9=buffer+3, rdx=[ClientNet+0x10], rcx=ClientNet."),
        ("kClientPostPacket",0x35C670,post,"Only old-head candidate allocating 0x8030 packet buffer through the identified thread allocator."),
        ("kAllocThread",0x47EF9B0,alloc,"Entire 92-byte function matches after relative masking; TLS fields+0x90/+0x98 unchanged."),
        ("kAllocGlobal",0x47EFA78,alloc_global,"Sibling branch of thread allocation in ClientPostPacket; critical section protects shared heap."),
        ("kNwSessionCtor",0x1436EE0,ctor,"Named RTTI NwSession vtable store at entry+0x1F; clears fields+0x88/+0x90/+0x98, saves bool arg at+0xA9, creates subobjects, returns this."),
        ("kTlsIndexSlot",0x6C88C50,tls_index,"RIP-relative global loaded into r8d in complete matched thread allocator."),
        ("kServerOnRecvPacket",0x2773C20,server_recv,"Unique exact 47-byte documented signature; sole caller in server loop passes native session and packet-buffer fields unchanged."),
        ("ServerNetSendPacket",0x2773870,server_send,"Unique exact 46-byte documented signature; all 532 direct callers load the same global ServerNet into rcx."),
        ("ServerNetExecuteTroc",0x2774B20,server_exec,"Unique documented signature; handler dispatch with original TrocParams calling convention."),
        ("ClientNetExecuteTroc",0x35D2A0,client_exec,"Unique documented signature; normal client Troc dispatcher."),
        ("ServerRunThreadLoop",0x27733F0,server_loop,"Enclosing exception-directory function of the sole ServerOnRecvPacket call."),
        ("kServerNetSlot",0x6D69680,server_slot,f"All {len(server_send_calls)} direct native ServerNetSendPacket callers load this RIP-relative global into rcx."),
        ("kClientNetSlot",0x6D69170,client_slot,f"{client_slots[client_slot]} of {len(post_calls)} direct ClientPostPacket callers load this RIP-relative global into rcx; other three call from this object directly."),
        ("kExceptOneVf1",0x2A759B0,slot("kToNearExceptOneControlVtable",1),"vf1 of exact RTTI-named ToNearExceptOneControlSendTargetFinder."),
        ("kExceptOneSelfVf1",0x2A75880,slot("kToNearExceptOneSelfVtable",1),"vf1 of exact RTTI-named ToNearExceptOneSelfSendTargetFinder."),
        ("kToNearVf1",0x2A75860,slot("kToNearVtable",1),"vf1 of exact RTTI-named ToNearSendTargetFinder; also independently unique head signature."),
        ("kWindowsLockVf1",0x1371340,slot("kWindowsLock3Vtable",1),"vf1 of exact RTTI-named WindowsLock<3>; indirect call through lock object slot+0x18."),
        ("kWindowsLockVf2",0xCE4EF0,slot("kWindowsLock3Vtable",2),"vf2 of exact RTTI-named WindowsLock<3>; indirect call through lock object slot+0x20."),
        ("kSlotRefreshReqSequence",0x2A22475,refresh_sequence,"Unique full relative-masked native refresh sequence; lock, busy flag+0x60, refresh call, unlock preserved."),
        ("kServerActorLookupSlot",0x6D69698,server_lookup,"RIP-relative target of first mov rax in the unique slot refresh sequence, then manager read at+0x48."),
        ("kSaveMgrRefreshSlots",0x1870C20,refresh,"Only direct call in unique slot refresh sequence; target is the native refresh thunk, not its virtualized implementation."),
    ]
    rows.extend((symbol,old,vt[symbol],f"Exact RTTI primary vtable name {name}") for symbol,old,name in vtable_rows)
    out, disasm = [], []
    by_old = {old:new for _,old,new,_ in rows}
    replacements = []
    for symbol, old, new, reason in rows:
        f = scanner.function(new)
        entry = {"symbol":symbol,"old_rva":old,"new_rva":new,"reason":reason,
                 "validation":"static_semantic_cross_reference","runtime_verified":False,"function":f}
        if f or symbol == "kSaveMgrRefreshSlots":
            off = pe.rva_to_off(new)
            entry["first_32_bytes"] = pe.data[off:off+32].hex(" ")
            disasm.append(f"\n{symbol}: 0x{old:X} -> 0x{new:X}\n{reason}")
            insns = code(new) if f else CS.disasm(pe.data[off:off+5],new)
            disasm.extend(f"{i.address:08X}: {i.bytes.hex():30s} {i.mnemonic} {i.op_str}" for i in insns)
        out.append(entry)
    for sig in signatures["signatures"]:
        if sig["old_rva"] not in by_old:
            continue
        new = by_old[sig["old_rva"]]
        off = pe.rva_to_off(new)
        data = pe.data[off:off+sig["length"]]
        replacements.append({"file":sig["file"],"name":sig["name"],"new_rva":new,"bytes":data.hex(" "),
                             "changed":data.hex(" ") != sig["bytes"],"length":len(data)})
    result = {"timestamp":pe.timestamp,"sha256":signatures["sha256"],
              "limitations":"Static identity and selected operands only; no live verification or complete object-layout certification.",
              "mappings":out,"replacement_arrays":replacements,
              "changed_fields":[{"symbol":"kTls_UseGlobalHeap","old":0x1FD,"new":0x1FE,
                                  "evidence":"ClientPostPacket mov ebp,0x1FE; add rbp,[gs:0x58]; cmp byte[rbp],0 selects global/thread allocation. Same offset in session constructor."},
                                 {"symbol":"server_thread_park_tls_offset","old":0x4460,"new":0x4430,
                                  "evidence":"ServerRunThread loop uses mov esi,0x4430 then derives pointer from the current thread TLS block."}],
              "constructor_vtable":{"lea_site":ctor_site,"vtable":ctor_vt}}
    (DATA/"transport_evidence_3527.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    (DATA/"transport_disassembly_3527.txt").write_text("\n".join(disasm)+"\n",encoding="utf-8")
    print(json.dumps({e["symbol"]:f"0x{e['new_rva']:X}" for e in out},indent=2))


if __name__ == "__main__":
    main()
