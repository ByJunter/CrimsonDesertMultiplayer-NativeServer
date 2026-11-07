#pragma once
// [session2] focus_follow = 1 (default) | 0, both PCs, builds 3527 and 3549 (session2/focus_follow.h). Pure logic and
// wire format, no game access.
//
// Why: a user's playable characters are all loaded, and the mercenary clan hides every one that is not in control
// (Status bit 0x100000, alpha 0; read live on 3549: Damiane 0xB0100002 flags 0x100000 alpha 0 while Kliff plays). The
// game's focus routine 0x23144B0(clan, old, new) hides the old, shows the new and stores the new key at clan+0x118,
// but only on the client of the user who switched: no packet tells the other client. There the character left behind
// stays on screen and the new one moves invisibly. So each client tells the other which of its characters is in
// control, and the other applies it to its copies.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace cdmp::session2::ffl {

inline constexpr uint16_t kControlOpcode = 1;  // troc_frame.h kControlOpcode
inline constexpr uint8_t kControlFocus = 16;   // troc_frame.h: 1-15 taken
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kHeader = 5;           // u16 opcode | i8 slot | u16 len
inline constexpr size_t kPayloadSize = 12;     // u8 state | u8 version | u16 0 | u32 focus | u32 main

inline constexpr uint64_t kResendMs = 2000;    // a guest that joins later learns it without a change
inline constexpr uint64_t kApplyEveryMs = 200; // at most this often at the client's dispatches
inline constexpr uint64_t kBusyGiveUpMs = 10000;

inline constexpr uint32_t kTypePlayer = 1, kTypeNamed = 4;  // [actor+0x88]+1: Kliff; Damiane, Oongka

struct Focus {
    uint32_t focus = 0;  // the character in control
    uint32_t main = 0;   // the user's main character ([user+0xD0])
    constexpr bool operator==(const Focus&) const = default;
};

constexpr bool is_main_key(uint32_t key) noexcept { return (key >> 28) == 0xA; }
constexpr bool is_party_key(uint32_t key) noexcept { return (key >> 28) == 0xA || (key >> 28) == 0xB; }

// clan+0x118 may still be 0 before any change (the routine is what writes it): then the main character plays.
constexpr Focus from_clan(uint32_t clan_focus, uint32_t main) noexcept {
    return Focus{is_party_key(clan_focus) ? clan_focus : main, main};
}
constexpr bool valid(const Focus& f) noexcept { return is_party_key(f.focus) && is_main_key(f.main); }

constexpr uint64_t pack(const Focus& f) noexcept { return (static_cast<uint64_t>(f.main) << 32) | f.focus; }
constexpr Focus unpack(uint64_t v) noexcept { return Focus{static_cast<uint32_t>(v), static_cast<uint32_t>(v >> 32)}; }

inline std::vector<uint8_t> frame(const Focus& f) {
    std::vector<uint8_t> out(kHeader + kPayloadSize, 0);
    out[0] = kControlOpcode & 0xFF;
    out[1] = kControlOpcode >> 8;
    out[2] = 0xFF;
    out[3] = kPayloadSize & 0xFF;
    out[4] = kPayloadSize >> 8;
    out[kHeader + 0] = kControlFocus;
    out[kHeader + 1] = kVersion;
    for (int i = 0; i < 4; ++i) {
        out[kHeader + 4 + i] = static_cast<uint8_t>(f.focus >> (8 * i));
        out[kHeader + 8 + i] = static_cast<uint8_t>(f.main >> (8 * i));
    }
    return out;
}

inline bool is_frame(std::span<const uint8_t> f) noexcept {
    return f.size() == kHeader + kPayloadSize && f[0] == (kControlOpcode & 0xFF) && f[1] == (kControlOpcode >> 8) &&
           f[2] == 0xFF && f[3] == (kPayloadSize & 0xFF) && f[4] == (kPayloadSize >> 8) && f[kHeader] == kControlFocus;
}

inline std::optional<Focus> decode(std::span<const uint8_t> f) noexcept {
    if (!is_frame(f) || f[kHeader + 1] != kVersion || f[kHeader + 2] != 0 || f[kHeader + 3] != 0) return std::nullopt;
    Focus out;
    for (int i = 0; i < 4; ++i) {
        out.focus |= static_cast<uint32_t>(f[kHeader + 4 + i]) << (8 * i);
        out.main |= static_cast<uint32_t>(f[kHeader + 8 + i]) << (8 * i);
    }
    if (!valid(out)) return std::nullopt;
    return out;
}

// The sender: at once on a change, and again every kResendMs.
class Publisher {
public:
    bool due(const Focus& now, uint64_t now_ms) noexcept {
        if (!valid(now)) return false;
        if (sent_ && now == last_ && now_ms - at_ms_ < kResendMs) return false;
        sent_ = true;
        last_ = now;
        at_ms_ = now_ms;
        return true;
    }

private:
    bool sent_ = false;
    Focus last_{};
    uint64_t at_ms_ = 0;
};

// What this client sees of one of the other player's characters.
struct Seen {
    bool exists = false;
    bool other_user = false;   // [actor+0xA0] is a user, and not this client's
    bool hidden = false;       // Status bit 0x100000
    bool busy = false;         // [Status+0x369]: the native routine does not touch such an actor
    uint32_t type = 0;
};

constexpr bool may_touch(const Seen& s) noexcept {
    return s.exists && s.other_user && !s.busy && (s.type == kTypePlayer || s.type == kTypeNamed);
}

enum class Act : uint8_t { None, Show, Hide };

// The character in control: at every pass, so that one that comes back into range and is hidden again gets shown.
constexpr Act act_on_focus(const Seen& s) noexcept { return may_touch(s) && s.hidden ? Act::Show : Act::None; }

// The characters to hide once per change. An actor that is not here needs nothing: when it loads the clan hides it
// natively, because remote_visible no longer refuses that (refuse_client_hide).
class Follower {
public:
    struct Pending {
        uint32_t key = 0;
        uint64_t since_ms = 0;
    };
    static constexpr size_t kSlots = 2;

    // A frame arrived. `reset`: a new connection, what was known of the old one goes.
    void on_want(const Focus& want, uint64_t now_ms, bool reset) noexcept {
        if (reset) *this = Follower{};
        if (known_ && want == want_) return;
        const Focus before = want_;
        const bool had = known_;
        known_ = true;
        want_ = want;
        ++changes_;
        for (auto& p : left_) {
            if (p.key == want.focus) p = Pending{};
        }
        if (want.main != want.focus) note_left(want.main, now_ms);
        if (had && before.focus != want.focus) note_left(before.focus, now_ms);
    }

    bool known() const noexcept { return known_; }
    const Focus& want() const noexcept { return want_; }
    uint64_t changes() const noexcept { return changes_; }
    std::span<const Pending, kSlots> left() const noexcept { return std::span<const Pending, kSlots>(left_); }

    // One pending character, seen now. Show/None for the caller; Hide when it should be hidden now.
    Act act_on_left(size_t slot, const Seen& s, uint64_t now_ms) noexcept {
        Pending& p = left_[slot];
        if (!p.key) return Act::None;
        if (!s.exists || s.hidden || !s.other_user) {  // nothing to do, or nothing this client may do
            p = Pending{};
            return Act::None;
        }
        if (s.busy) {
            if (now_ms - p.since_ms >= kBusyGiveUpMs) p = Pending{};
            return Act::None;
        }
        const bool ok = may_touch(s);
        p = Pending{};
        return ok ? Act::Hide : Act::None;
    }

private:
    void note_left(uint32_t key, uint64_t now_ms) noexcept {
        if (!key || key == want_.focus) return;
        for (auto& p : left_) {
            if (p.key == key) return;
        }
        for (auto& p : left_) {
            if (!p.key) {
                p = Pending{key, now_ms};
                return;
            }
        }
    }

    bool known_ = false;
    Focus want_{};
    uint64_t changes_ = 0;
    std::array<Pending, kSlots> left_{};
};

}  // namespace cdmp::session2::ffl
