#pragma once
// [session2] housing_share = 0 (default) | probe | 1 | live, HOST: the guest sees the host's houses and what is placed
// in them. See housing_share_logic.h. probe: everything is decided and logged, nothing is dropped or queued. 1: the
// login set only. live: also the host's live changes -- UNSAFE: the design mode's 2500 crashed the guest (2026-11-06).
// Threads: the sniffer's writer (housing_share_sniffed), the server's threads (housing_share_on_out). One leaf lock.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace cdmp::session2 {

// mode: housing_share; live_ops: housing_share_live_ops (only with live: the live changes to forward, e.g. "2623").
// edits: housing_share_edits (the host's construction-mode edits reach user 2: its client's 2408 sent as 2733).
// guest_spawn: housing_share_guest_spawn (0: user 2's own spawn requests of the housing gimmicks are dropped).
// rekey: housing_share_rekey ("1": the host's housing gets user 2's key, the guest owns it; "0": the host's key is
// kept; or a list of housing opcodes to re-key, hsh::parse_rekey).
// housing_share_level_states = 0 (default) | 1, host: user 2 gets the host's level states (ChangeGameLevelStateAck
// 2344) with its house list at login, so its client loads the house plot's phase the host's did (no pond).
void housing_share_set_level_states(bool on);
void housing_share_configure(const std::string& mode, const std::string& live_ops = {}, bool edits = false,
                             bool guest_spawn = false, const std::string& rekey = "0",
                             const std::string& drop_gimmicks = "0");
bool housing_share_wanted();  // probe or on
// The sniffer's writer thread: one whole logical packet of the host's own client.
void housing_share_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept;
// take_frame, for a packet to the LIVE session 2 before it is queued: true = drop it (the host's set was queued).
bool housing_share_on_out(uint16_t opcode, std::span<const uint8_t> packet) noexcept;
// server inject (user 2's requests, logical opcode): true = drop it (housing_share_guest_spawn=0, hsh::kGuestSpawnReqs).
bool housing_share_blocks_request(uint16_t opcode) noexcept;
std::string housing_share_describe();

}  // namespace cdmp::session2
