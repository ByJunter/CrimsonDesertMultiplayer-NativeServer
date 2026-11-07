#pragma once
// [session2] guest_gimmick_interact = 0 (default) | trace | 1, GUEST. Pure logic (tests:
// tests/optd_gimmick_interact_tests.cpp). The hooks are docked_gimmicks.cpp's (it owns the entries of both requests).
//
// The client runs a gimmick event only if it controls the gimmick: 0x9D05D0 (ClientGimmickActorComponent vf +0x748) and
// 0x9D0B00 compare this client's user ([[local player]+0xA0]+0x60) with the gimmick's controller (actor+0x90) and refuse
// with eErrNoActorNotExist otherwise (docked_gimmicks_logic.h). In the host's field every world object is controlled by
// the host's user, so on the guest the "Buy" of a market stall item sends nothing (live 2026-11-06, build 3549: E on a
// host-controlled item, no request; the same item with its +0x90 set to the guest's user in the guest's memory, test
// only: InteractionPreemptionStepReq -> success, InteractionDoStepReq -> success, CatchActionResultAck, the item in hand).
//
// trace: every refused request is logged (deduplicated by event hash and caller; and every one for a gimmick within a
// few metres of the player), with the event hash and the caller, to find the event of an interaction.
// 1: as trace, and a refused request whose event hash is in guest_gimmick_interact_events passes the check (two mid
// hooks on the compares make eax the gimmick's controller for that call only). The server still decides: its own
// checks (InteractionPreemptionStep / DoStep per user) are untouched, and the controller-only state requests it gets
// from this client are dropped by the server as before (0x2D52980 / 0x2D51F10).

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cdmp::session2::gil {

enum class Mode : uint8_t { Off = 0, Trace = 1, On = 2 };

inline std::optional<Mode> parse_mode(std::string_view raw) noexcept {
    if (raw.empty() || raw == "0") return Mode::Off;
    if (raw == "trace" || raw == "probe") return Mode::Trace;
    if (raw == "1") return Mode::On;
    return std::nullopt;
}
inline const char* mode_name(Mode m) noexcept {
    return m == Mode::On ? "1" : m == Mode::Trace ? "trace" : "0";
}

inline constexpr size_t kMaxEvents = 16;

struct Events {
    std::array<uint32_t, kMaxEvents> hash{};
    size_t n = 0;
    bool contains(uint32_t h) const noexcept {
        for (size_t i = 0; i < n; ++i) {
            if (hash[i] == h) return true;
        }
        return false;
    }
};

// "0x1234ABCD,0xDEADBEEF" (spaces allowed, "0x" optional; empty = none). nullopt: a bad item or more than kMaxEvents.
inline std::optional<Events> parse_events(std::string_view raw) noexcept {
    Events out;
    size_t i = 0;
    while (i < raw.size()) {
        size_t j = raw.find(',', i);
        if (j == std::string_view::npos) j = raw.size();
        std::string_view item = raw.substr(i, j - i);
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);
        i = j + 1;
        if (item.empty()) continue;
        if (item.size() > 2 && item[0] == '0' && (item[1] == 'x' || item[1] == 'X')) item.remove_prefix(2);
        uint32_t v = 0;
        const auto r = std::from_chars(item.data(), item.data() + item.size(), v, 16);
        if (r.ec != std::errc{} || r.ptr != item.data() + item.size() || out.n >= kMaxEvents) return std::nullopt;
        if (!out.contains(v)) out.hash[out.n++] = v;
    }
    return out;
}

// The decision at a request's entry: let this call pass the controller compare.
inline bool allow(Mode m, const Events& ev, uint32_t hash) noexcept { return m == Mode::On && hash && ev.contains(hash); }

// A small bounded set of 64-bit keys with a per-key count (the trace's dedupe): note() returns how many times the key
// was seen before (0 = new); a full table answers kFull for new keys.
template <size_t N>
class Seen {
public:
    static constexpr uint32_t kFull = 0xFFFFFFFFu;
    uint32_t note(uint64_t key) noexcept {
        const size_t h = static_cast<size_t>((key * 0x9E3779B97F4A7C15ull) >> 32) % N;
        for (size_t p = 0; p < N; ++p) {
            Slot& s = slots_[(h + p) % N];
            if (s.used && s.key == key) return s.count++;
            if (!s.used) {
                if (used_ * 4 >= N * 3) return kFull;
                s = Slot{key, 1, true};
                ++used_;
                return 0;
            }
        }
        return kFull;
    }
    size_t used() const noexcept { return used_; }

private:
    struct Slot {
        uint64_t key = 0;
        uint32_t count = 0;
        bool used = false;
    };
    std::array<Slot, N> slots_{};
    size_t used_ = 0;
};

inline uint64_t pair_key(uint32_t a, uint32_t b) noexcept { return (uint64_t{a} << 32) | b; }

}  // namespace cdmp::session2::gil
