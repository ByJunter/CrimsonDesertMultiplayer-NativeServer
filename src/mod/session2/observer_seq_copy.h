#pragma once
// P8 S1 option A, the sequencer part (docs/research/pendientes/p8-impl-opcion-a.md): [session2]
// observer_seq_copy = 0 | probe | 1, HOST (needs create_session=1 and [sniffer] enabled=1).
// observer_seq_copy_only = <hex sequencer ids and/or actor keys>, empty = every sequencer.
// observer_seq_copy_2322 = 1 (default) | 0: the host's last 2322 once per observer trip.
// observer_seq_copy_2100 = 0 (default) | 1: user 2's 2100 SyncStageCandidatePostionReq dropped while it observes.
//
// The host's own client's 2661/2981/2810/2322 (the sniffer's writer thread) feed a ledger; user 2's
// trip into the host's field (take_frame: its MoveFieldAck, as the server wrote the owner) turns
// the ledger into pending copies; each goes to user 2's queue (byte for byte, push_outgoing_extra)
// once every actor its 2661 binds was announced to user 2 by a 2005 in this trip and user 2's own
// 2322 went (or 30 s after its 2144); live 2661s become pending, a delivered sequencer's 2810 is
// copied. The C>S of user 2 about the host's sequencers (and its 2100 while it observes) are
// dropped on the network thread (offer). Pure logic: observer_seq_copy_logic.h.
// Since guest_seq_play (pendientes/p8-impl-escenas-play.md §5) also user 2's 2276 DeleteSequencerReq and
// 2105 StageBranchStateDelayEndReq about a host's sequencer, each with its own counter and line (user 2's
// own sequencers always pass; one about an id the host never got passes and is counted as "passed").
//
// observer_sched_copy = 0 (default) | 1 (pendientes/p8-impl-horarios-snapshot.md): on the same path, the host
// client's schedule stages: a fixed ledger of its live 2335s (and their 2958/2909) by scene key, emptied on its
// 2126/2411 and its own 2174; on user 2's observer trip a snapshot of it goes to user 2 once it finished loading
// (its 2549 or 2144), right behind the 2005 of each stage's actor, before the later NPC 2005s (unless that 2335
// reached user 2 natively first); live, a creating 2335 user 2 did not get natively the same way, and the
// 2126/2958/2909 of a stage user 2 has, unless the same payload just reached it natively. Its lines:
// 'ObserverSeqCopy [sched ...]', 'ObserverSchedCopy summary'. filter_req also sees user 2's 2549 (never dropped).
//
// probe: everything is decided and logged ("WOULD SEND", "WOULD DROP"), nothing is sent or dropped.
// Threads: the sniffer's writer (a mod thread), take_frame (the server's threads), the network
// thread (filter, tick). One leaf lock; under it only the outgoing queue's own lock is taken.
// Lines: 'ObserverSeqCopy [...]' from the network thread (tick), a summary every 60 s.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// From session2::install (before the tunnel opens). mode: the raw ini value ("0", "probe", "1").
void observer_seq_copy_configure(const std::string& mode, const std::string& only, bool copy_2322, bool filter_2100);
bool observer_seq_copy_wanted();  // probe or on
// P8 session 14 review M2: with observer_load_defer on, the host's 2322 copy waits while a snapshot copy waits for
// its actors, until user 2's 2762 + ms (0 = off, as before). After observer_seq_copy_configure.
void observer_seq_copy_set_milestone_hold(uint32_t ms);
bool observer_seq_copy_sends();   // on
// After observer_seq_copy_configure (needs it probe or on; its probe makes this one probe too).
void observer_sched_copy_configure(bool on);
bool observer_sched_copy_wanted();

// The sniffer's writer thread: one whole packet of the host's own client (only S>C is used).
void observer_seq_copy_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept;
// take_frame, for every packet to the LIVE session 2, after its push. server_owner: for a MoveFieldAck,
// the owner user the SERVER wrote (before user 2's copy is rewritten); 0 otherwise.
void observer_seq_copy_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool pushed,
                              uint32_t server_owner) noexcept;
// The network thread (session2 offer): true = drop this Req of user 2 (counted, a few lines).
bool observer_seq_copy_filter_req(uint16_t opcode, std::span<const uint8_t> payload) noexcept;
// The network thread, every loop turn: the gate's timeout, refused copies again, user 2 gone, the lines.
void observer_seq_copy_tick() noexcept;
// A new guest epoch: the trip and its copies end (the ledger stays: it is the host's).
void observer_seq_copy_on_epoch(uint32_t epoch) noexcept;
// A new server instance (the host back at the title, another game): the ledger, the known sequencer ids
// and user 2's own ids go too (the server's ids start again).
void observer_seq_copy_on_server_replaced() noexcept;
std::string observer_seq_copy_describe();

}  // namespace cdmp::session2
