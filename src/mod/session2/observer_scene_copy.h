#pragma once
// P8, HOST: [session2] observer_scene_copy=1 (default 0; needs create_session=1)
// (docs/research/pendientes/p8-escenas-y-acks.md §2; the pure logic: observer_scene_copy_logic.h).
//
// A scene user 2 starts in the host's field (e.g. the ESC "view item" sequencer, UseItemReq 2855)
// lives in the stage manager of the field's OWNER on the server, so its closing
// DeleteSequencerAck 2810 goes to [manager+0x18], the host's character, and user 2's client never
// closes it. With this key, at the entry of the 2810 serializer 0x2A4E4C0 (observer_stage's mid
// hook, only caller 0x2879819 in 0x2879440, the server's logic thread):
//   - identify user 2's scene: the server sequencer (rsi of 0x2879440, checked: [r8] == [rsi+0x30])
//     has owner key [rsi+0x98] == user 2's character key (0x2879753 -> 0x20532B0 -> 0x205D7C0 reads
//     that field to find its owner actor), its 2661 reached user 2's queue in this trip and named
//     user 2's character as actor and owner, and the native recipient rcx ([r12+0x18]) is another
//     character;
//   - then, on this same thread and frame, the native emitter once more: ToTarget 0x2A757A0(finder,
//     user 2's character) (session = its vfE8(), must be the live session 2) and
//     0x2A4E4C0(user 2's character, &finder, &seq, &u32, &u8) with the native call's values. The
//     2810 is serialized by the game for user 2 (actor key A0..user 2, the same sequencer, u32, u8)
//     and reaches take_frame during the call (checked and logged with its corr= token).
// Never for the host's own scenes, never a second 2810 to the host (the native one is untouched),
// never twice for one sequencer (its 2810 in user 2's queue closes it in the book).
// Diagnostics: "ObserverSceneCopy [...]" lines with corr= tokens matching the guest's GuestStage
// [2810 in 0xC06330] line; counters every 60 s; describe.

#include <cstdint>
#include <span>
#include <string>

#include "mod/session2/guest_actor_diag_logic.h"

namespace cdmp::session2 {

// session2::install: the key (before observer_stage's thread starts). True = wanted.
bool observer_scene_copy_configure(bool on);
bool observer_scene_copy_wanted();
bool observer_scene_copy_active();  // wanted, code verified, not disabled
// observer_stage's thread, before its hooks go in: the code the copy relies on (build 2976).
bool observer_scene_copy_verify(uintptr_t base, std::span<const gad::Section> secs);
void observer_scene_copy_disable(const char* why);
// This thread is inside our own copy's call of 0x2A4E4C0 (P8 review M6: observer_stage's 2810 hook sees
// that nested call too; it is not a native 2810).
bool observer_scene_copy_in_copy() noexcept;
// observer_stage's 2810 hook (0x2A4E4C0 entry), the native call's registers. Returns at once when off.
void observer_scene_copy_on_delete(uintptr_t rcx, uintptr_t r8, uintptr_t r9, uintptr_t rsi, uintptr_t r12,
                                   uintptr_t rsp) noexcept;
// take_frame, after the push, for a packet to the live session 2 (2661, 2810, 2174 matter).
void observer_scene_copy_on_out(uint16_t opcode, std::span<const uint8_t> payload, bool pushed) noexcept;
// A new guest epoch (or the server instance replaced): the book starts empty.
void observer_scene_copy_on_epoch(uint32_t epoch) noexcept;
// observer_stage's thread: the lines (every second) and the counters line (every 60 s).
void observer_scene_copy_drain();
std::string observer_scene_copy_counters();
std::string observer_scene_copy_describe();  // atomics only

}  // namespace cdmp::session2
