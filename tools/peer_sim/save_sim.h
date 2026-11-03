#pragma once
// cdmp_peer --mode replay, P8 session 25 phase B (docs/research/noche/progreso-invitado.md, "Implementado (fase B)"):
// the guest's own save on ONE PC. cdmp_peer stands in for the guest's mod with the same tunnel states 6-11
// (src/mod/session2/guest_save_wire.h):
//
//   --guest-save DIR     offer and upload DIR\lobby.save + DIR\save.save (a COPY of a slot folder) at the host's first
//                        control packet; the script waits for the host's SAVE_ACK (30 s at most) before its first Req
//   --guest-slot N       the guest slot N the offer names (default: --var SAVE_SLOT, else 1; 0-2 or 100-108)
//   --guest-id ID        the SteamID64 the offer names (default 0: the host files it under "local-peer")
//   --base-gen G         the last generation "applied" (default 0: a newer one in the host's mailbox is staged instead)
//   --save-at SEC        a SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op': it saves now; a 3037 does not, see
//                        guest_save_wire.h) SEC seconds after the first Req
//   --save-before-bye    the script's goodbye (2001) waits behind that 2910 for the SAVE_RESULT (15 s at most; at
//                        once if the server's 3016 says it failed)
//   --save-results DIR   every SAVE_RESULT generation is checked (SHA-256, header) and stored in DIR\gen<n>\
//                        (lobby.save, save.save, manifest.txt), then acknowledged; without it they are checked and
//                        acknowledged only
// The tunnel's caps packet then says kCapsGuestSave (0x02).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/guest_save_wire.h"

struct SaveSimOptions {
    std::wstring guest_save;   // --guest-save
    std::wstring results;      // --save-results
    std::optional<uint32_t> slot;  // --guest-slot
    uint64_t guest_id = 0;     // --guest-id
    uint32_t base_gen = 0;     // --base-gen
    double save_at_s = -1;     // --save-at (< 0: no)
    bool save_before_bye = false;
    bool any() const {
        return !guest_save.empty() || !results.empty() || save_at_s >= 0 || save_before_bye;
    }
};

class SaveSim {
public:
    explicit SaveSim(SaveSimOptions o) : o_(std::move(o)) {}

    // Reads and checks --guest-save (nothing to do without it). False (why) if it cannot be offered.
    bool load(uint32_t default_slot, std::string& why);
    bool enabled() const { return o_.any(); }
    bool uploads() const { return loaded_; }

    // The offer and its chunks (Troc frames), the first time the host is heard on this connection.
    std::vector<std::vector<uint8_t>> on_host_seen();
    // A save control frame of the host (states 7-10): consumed, with frames to send back (SAVE_RESULT_ACK).
    bool on_host_frame(const std::vector<uint8_t>& packet, std::vector<std::vector<uint8_t>>& replies);
    void on_host_packet(uint16_t opcode, std::span<const uint8_t> payload);
    // 3016s that said the save failed (the final hold ends at once on one).
    uint32_t saves_failed() const { return n3016_failed_; }

    // The upload's answer: nullopt while waiting.
    std::optional<uint8_t> ack_result() const { return ack_; }
    uint32_t ack_gen() const { return ack_gen_; }
    bool offered() const { return offered_; }
    uint32_t stored() const { return stored_; }
    uint32_t last_gen() const { return last_gen_; }
    const SaveSimOptions& options() const { return o_; }
    std::string describe() const;

    // The final save the guest's mod sends: SaveToFileCurrentPlayerReq 2910, slot 100, name 'Co-op', slot byte 0.
    static std::vector<uint8_t> final_save_req();

private:
    bool store(const cdmp::session2::gsw::Result& r, std::string& why);

    SaveSimOptions o_;
    bool loaded_ = false, offered_ = false;
    uint32_t slot_ = 1, xfer_ = 0;
    std::vector<uint8_t> lobby_, save_;
    cdmp::session2::gsw::Files files_{};
    std::optional<uint8_t> ack_;
    uint32_t ack_gen_ = 0;
    cdmp::session2::gsw::Assembler down_;
    cdmp::session2::gsw::Result meta_{};
    uint32_t stored_ = 0, results_ = 0, failed_ = 0, last_gen_ = 0, n3016_ = 0, n3016_failed_ = 0;
    std::optional<cdmp::session2::gsw::Status> status_;
};
