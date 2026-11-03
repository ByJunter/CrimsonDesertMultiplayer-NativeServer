#pragma once
// Option D, P-RELOJ (docs/research/pendientes/reloj.md): the world clock stays
// the host's. The server keeps ONE world time (FieldTime, exe+0x6A6D238), and
// two .rsrc functions publish into it whatever the travelling character
// brings, checking only that its user owns its own field:
//
//   FieldMover::SyncFieldTimeOnMove 0x2BC0A30 (FM, destination field): the
//      owner of the field writes the level's fixed time (or its own saved
//      time) into the world clock (0x2BC0C50), then the setter 0x21A1CC0 may
//      publish again (.xtls 0xE52E970). User 2 owns its private parking field
//      on its first travel, whose fixed time (day 0, 24:00, scale 0) froze the
//      host's world at midnight (C1e-C1g, C4b).
//   TransformSync LoadingComplete 0x219D7F0 (TS, res, field, ?, u8 flag,
//      now*): with flag != 0 it publishes the character's time and unpauses
//      the field, without any owner check (0x219D8EC).
//
// The gate, for user 2's character (its user at [[TS]+8]+0xA0) and, fail
// safe (review of 10-09), for ANY user other than the host once the host's
// user key is known, so a guest the lookups miss still never publishes:
//   travel: the field's current time (CommonField::GetCurrentFieldTime
//      0x20E55A0, read only) into TS+0x450 and nothing else, which is what the
//      native non-owner branch does (0x2BC0CBD) minus the setter's publishing.
//      The original never runs for user 2, also when the copy fails.
//   LoadingComplete: the original with flag 0 (the rest of the loading still
//      runs, 0x219D941 on; the Ack is untouched).
// The host's own travels and loads stay native, and so do characters with no
// user. A thread samples the world clock once a second and logs every
// change (a publisher the inventory missed would show there).

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// The game function the travel gate calls (injectable for the tests).
struct ClockGame {
    void*(__fastcall* field_time)(void* field, void* out);  // 0x20E55A0, returns out
};

namespace clk {
inline constexpr uint32_t kTsActor = 0x08;      // TS -> character
inline constexpr uint32_t kActorUser = 0xA0;    // character -> ServerUserActor
inline constexpr uint32_t kUserKey = 0x60;      // ServerUserActor -> u32 user key (0x2BC0A70)
inline constexpr uint32_t kTsFieldTime = 0x450; // 32-byte FieldTime of the character (setter 0xE52E914)
inline constexpr uint32_t kFieldTimeSize = 32;
inline constexpr uintptr_t kWorldTime = 0x6A6D238;  // server FieldTime (exe+)
}  // namespace clk

enum class ClockWho { Other, Host, User2 };

// Whose character a TransformSync is. host_key = the host's user key (0 =
// not known yet), user2 = session 2's user (0 = none). Other = no user (or the
// host not known yet and not user 2); Host = the host's key; User2 = user 2,
// or any other user once the host is known.
ClockWho clock_who(uintptr_t ts, uint32_t host_key, uintptr_t user2);

// User 2's travel: TS+0x450 = the destination field's current time. False =
// nothing written (unreadable TS or field).
bool clock_user2_travel(const ClockGame& g, uintptr_t ts, uintptr_t field);

// The flag LoadingComplete gets: 0 for user 2, the original otherwise.
uint8_t clock_loading_flag(ClockWho who, uint8_t flag);

// Install both hooks as one unit, after checking their code; false = none.
bool clock_gate_install();
bool clock_gate_active();
std::string clock_gate_describe();

}  // namespace cdmp::session2
