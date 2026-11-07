# Opcode adapter audit: 2976 logical IDs, 3527 native IDs

This is a source/static-binary audit, not a working runtime port. All file lines
refer to the source snapshot inspected on 2026-11-04. `troc_map.py` recovers all
1,175 current Troc singleton objects through RTTI and their initializer stores;
it records executable SHA-256 and the exact LEA/MOV evidence for each singleton.

## Mapping evidence

- 1,108 classes retain the old exact RTTI name; 1,072 of their opcodes changed.
- 67 classes are new. Seven old names disappeared; the mapper does not invent aliases.
- All 1,175 current singleton addresses and opcodes are unique.
- Singleton range: `[0x6E8ED10, 0x6EC2400)`, objects `0x90` bytes.
- Kind, channel/flags, expected-session and deferred flags are unchanged for
  shared names. Fixed size changes: `InteractionRewardOnceTimer` 11 -> 15 and
  `SequencerTimelineEventSummonCharacterOnceTimer` 15 -> 19.
- Variable-sized messages retain broad `5..32710` bounds. That does not prove
  payload field layouts are unchanged. Native parsing/injection needs separate
  layout verification for every payload the mod inspects or synthesizes.

`data/opcode_mapping_2976_3527.json` is the compact map. The full map includes
singleton RVAs, vtable RVAs, handler RVAs, initializer sites, and exact bytes.

## Recommended representation

Keep `common/troc_frame.{h,cpp}` and pure logic/tests as the existing logical
2976 format. Do not make `troc::parse()` implicitly convert: its callers include
already-logical network packets, synthetic packets, and test fixtures. Such a
change would double-convert values whose current number collides with another
old message (most current opcodes lie inside the old range).

At explicitly native boundaries, copy the packet, translate its outer opcode,
then pass that logical copy to mod logic. Immediately before a native call or
native queue publication, encode the outer opcode back in a separate buffer.
Keep scalar `decode_native_opcode()` for hooks that read a native Troc object's
opcode or inspect the original native packet. Do not temporarily change a
shared game-owned header in place; callback re-entry and other hooks see it.

Added messages require a reversible logical namespace; blindly leaving their
native number unchanged would collide with an old logical message. One option
is reserved logical `0x8000 | native_id`, admitted only for a validated current
Troc class. Existing server channel arrays span all 65,536 values. Diagnostic
tables restricted to `<4096` would need an explicit policy. Removed old IDs
must fail encoding; never encode a missing name by its old numeric value.

Both peers must negotiate the same game build and mapping identity; logical
opcode conversion does not provide cross-build payload compatibility. Preserve
the tunnel's control opcodes 1 and 2 outside native conversion.

## Required native ingress points

| File/site | Native input | Required handling |
|---|---|---|
| `session2/server.cpp:435` `take_frame()` | Native `AsyncRequestSend` frame; copied at 459 | Decode copied outer header before `parse_wire` at 460 and every hold/filter/observer. |
| `session2/guest.cpp:500` `frame_packet()` | Native C->S packet buffer; copy at 516 | Decode before `parse_wire`/return. This covers remote guest filters, save logic, and tunnel enqueue. |
| `session2/guest.cpp:976,991` `on_send()` | Reads first opcode directly from native buffer before `frame_packet()` | Decode scalar before `decide()`, local-login and re-entry comparisons. |
| `session2/server.cpp:825` `on_server_recv()` | Native packet opcode, including the host's normal session | Decode scalar before save/movement/login/lifecycle decisions. The native packet pointer passed to downstream functions still needs explicit native provenance. |
| `session2/server.cpp:983` `load_channels()` | Native singleton metadata opcode | Index logical channel/blocked-channel arrays with decoded logical ID, preserving native kind/name/channel policy. |
| `session2/user2_funnels.cpp:459,472` `subject_enter`, `scene_enter` | Native singleton opcode `Troc+0x0C` | Decode before subject/quest/travel/shop/timer decisions; these bypass all byte-packet boundaries. |
| `net/troc_sniffer.cpp:178` `on_post_packet()` | Native opcode for channel/flags cache | Cache by logical ID or explicitly encode before native cache lookup; keep convention consistent. |
| `net/troc_sniffer.cpp:493` `Writer::write()` | Native captured byte packet | Preserve native bytes for the on-disk capture; send separately decoded copies to PacketTap and legacy local_capture observers. |
| `net/troc_sniffer.cpp:341` `load_troc_names()` | Native singleton metadata | Keep native name table for native captures; opcode self-check at 770 must resolve native IDs for the expected logical messages. |
| `net/troc_sniffer.cpp:254` `on_recv_packet()` callbacks | Raw native packet passed to `RecvRewrite` and `RecvExtra` | Invoke callbacks on logical copies, then restore native outer IDs before copying permitted rewrites back or injecting extras. |

These are the principal functional boundaries. Header reads in legacy
`local_capture`, `enemy_relay`, `player_hits`, `guest_hits`, `coop`, and observers
can stay logical if their sole sniffer/tunnel inputs are normalized. The search
snapshot in `data/opcode_read_sites.txt` lists the read locations found by a
targeted regex; it is not a proof of exhaustiveness.

## Required native egress points

| File/site | Native sink | Required handling |
|---|---|---|
| `session2/server.cpp:1319` `push_request()` | Fills native packet buffer then pushes Q0 | Encode logical packet into native bytes before copying at 1331; queue lengths unchanged for opcode-only transformation. Keep logical `p.opcode` for mod diagnostics. |
| `session2/guest.cpp:1145` `deliver_all()` | `guarded_on_recv` -> native `ClientNet::OnRecvPacket` | Finish mod gates/rewrites first, encode separate native bytes, inject. Retain logical packet for post-delivery observers. |
| `session2/guest.cpp:1186` `answer_goodbye()` | Fabricated internal logout Ack to same native function | Encode too; present logout IDs 2001/2002/2003 happen to be unchanged but that is a mapping fact. |
| `net/troc_sniffer.cpp:209` | `guarded_post` -> native ClientNet::PostPacket | Encode queued synthetic C->S packets before native call. |
| `net/troc_sniffer.cpp:279` | `deliver_extra` -> native OnRecvPacket | Encode synthesized `RecvExtra` output before native call. |
| `net/troc_sniffer.cpp:294` | `guarded_inject` -> native OnRecvPacket | Encode legacy puppet injection packets before native call. |

Native writer-function calls that take typed fields, not byte packets, should
keep native execution unchanged; their byte output is normalized at `take_frame`
or the sniffer observer. A synthetic native timer that includes an opcode in its
own memory layout needs explicit conversion at its native publication point.

## Direct hooks and mixed packet provenance

Converting only the tunnel queues is not enough. Concrete bypasses include:

- `user2_funnels` native Troc instance reads above control gameplay behavior.
- `server.cpp:846-859` hands the original native pointer plus a decoded scalar
  to `user2_stages_server_packet`, `docked_gimmicks_server_ingress`,
  `door_trace_ingress`, and `scene_level_share_server_ingress`. Most read only
  payload fields, but `door_trace.cpp:267-274` copies and reparses the header:
  normalize that copy or give it an explicit-native entry point.
- `guest_stage_diag.cpp:309` and `p8_probes.cpp:463` read opcode directly from
  native TrocParams packet pointers. They are diagnostic but should label native
  versus logical IDs correctly.
- Hook callbacks for individual handlers often assume a fixed old logical
  message by their hook identity; their raw payload pointer is still native.
  If that payload embeds opcode fields, an outer-header adapter cannot fix it.

## ScopeSenderAck: preserve opaque fragments

Old logical ID 2042 maps to native 3527 ID 2046. The current handler at RVA
`0xDEB070` reads a one-byte kind and four-byte stream identifier. Kinds 1/4 read
additional two-byte and four-byte metadata; trailing bytes accumulate in a
scope object. Kinds 3/4 cause the assembled stream to be dispatched through
`0xDEABF0`. Individual nested headers may therefore cross outer packet bounds.

The existing mod treats ScopeSenderAck as opaque (see
`docs/research/opcion-d-p8.md:444`); it does not inspect nested packets for holds
or filters. For that existing behavior, translate ONLY the outer header and
leave payload bytes native and unchanged. Encode the outer header back on
delivery; the native client then sees the original nested opcodes. Blind
recursive per-packet substitution would corrupt fragmented streams. Supporting
logical filtering inside Scope requires a separately verified complete
reassembler and reproduces a pre-existing coverage limitation of this mod.

## Verification needed before enabling the adapter

The shared codec is implemented in `src/common/native_opcode.h/.cpp`. Its
explicit `Build` parameter prevents implicit global/double translation.
`decode`/`encode` return optional scalar IDs; `decode_packet`/`encode_packet`
mutate only the first two bytes after complete validation; `decode_copy` and
`encode_copy` leave the source buffer untouched. Unknown IDs/builds, tunnel
controls, invalid directions, malformed sizes, and removed target messages
fail closed. Ordinary kind/direction admission remains the caller's policy.
The 2001/2002/2003 internal packets retain their strict short formats.

The checked-in catalogs are reproducibly generated by
`tools/porting/generate_native_opcode.py` from the compact mapping, which now
includes the 67 additions as `added_opcodes`. Run the generator with `--check`
to detect stale output. Mapping version 1 has fingerprint
`0x01cc7f9fb4ee59f6`; both native build and mapping identity must match between
peers because Scope payloads retain native opcodes. CTest `native_opcode`
exhausts scalar admission across all 65536 IDs in each build, round-trips every
catalog packet, checks removed IDs and collisions, validates malformed and
short packets, and checks byte-for-byte preservation of fragmented Scope data.
These tests pass on the local Release build; no live game validation is implied.

1. Check the live Troc table against the generated name/opcode map, not only PE
   timestamp, and refuse mismatches.
2. Round-trip every mapped current ID, every extension ID, and every supported
   short internal message; missing old IDs and malformed frames must fail.
3. Test numbers that collide across builds, synthetic control IDs, and native
   callback re-entry to ensure conversion occurs exactly once.
4. Verify outer Scope round-trip preserves the entire native payload byte for
   byte, including fragments that do not begin on a nested header.
5. Trace an actual native host/guest login and request/ack sequence; passing old
   pure-logic tests alone does not prove current native payload compatibility.
