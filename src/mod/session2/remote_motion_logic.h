#pragma once
// P8, the other player's character seen as a native remote actor (control mode
// 2 on both PCs): free wall climb, ledge climb-up and mount/dismount
// (docs/research/pendientes/p8-escalada-desmontar.md; the implementation:
// docs/research/pendientes/p8-impl-escalada.md). The pure logic of
// remote_motion.cpp, with no game access, so the tests can run it.
//
// Three [session2] keys, on BOTH PCs (host and guest):
//   remote_climb   0 | probe | 1: PointClimb::Enter (0x691400) on a remote
//                  player's character in control mode 2 reports success when
//                  the original fails (probe: count only).
//   wallup_send    0 | 1: the client's UpdateWallUpDataReq (2851) sender is
//                  switched on (byte exe+0x6D76D5C = 1).
//   dismount_probe 0 | 1: read-only samples of the remote player's character
//                  every 30 ms around mount/dismount (and climbs), compact log.
//   divergence_probe 0 | 1: the same samples against this mod's channel
//                  position of that player; a line per second while they
//                  differ by > 1.5 m for > 1 s (section 4 below).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cdmp::session2::rmo {

// ---------------------------------------------------------------- the option

enum class Mode : uint8_t { Off, Probe, On };

struct ParsedMode {
    Mode mode = Mode::Off;
    bool known = true;  // false: an unknown value, treated as Off (warned)
};

inline ParsedMode parse_mode(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) raw = raw.substr(0, c);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }
    std::string v(raw);
    for (char& ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (v.empty() || v == "0" || v == "off" || v == "no" || v == "false") return {Mode::Off, true};
    if (v == "probe") return {Mode::Probe, true};
    if (v == "1" || v == "on" || v == "yes" || v == "true") return {Mode::On, true};
    return {Mode::Off, false};
}

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "0";
        case Mode::Probe: return "probe";
        case Mode::On: return "1";
    }
    return "?";
}

// ---------------------------------------------------------------- keys

// A player's main character: 0xA01xxxxx (0xA0100001 the host's, 0xA01002E9 /
// 0xA0100309 user 2's in session 7). Users are 0x901..., NPCs/children 0xB01...
inline constexpr bool is_player_character_key(uint32_t k) { return (k >> 20) == 0xA01; }

// ---------------------------------------------------------------- 1. PointClimb::Enter

// What 0x88E8E0(movement controller) answers (the game's own control mode):
// 1 local control, 2 remote with an owning user (+0x90 != 0), 3 otherwise.
inline constexpr uint8_t kControlLocal = 1, kControlRemote = 2, kControlOther = 3;

enum class ClimbOut : uint8_t {
    OrigOk,          // the original succeeded: untouched
    Forced,          // remote_climb=1: mode 2, player character -> 1
    WouldForce,      // remote_climb=probe: the same case, original kept (0)
    FailMode1,       // failed in mode 1 (local control): never forced
    FailMode3,       // failed in mode 3: never forced
    FailModeOther,   // failed, the mode function answered something else
    FailMode2Other,  // failed in mode 2, not a player's character (0xA01...)
    Unreadable,      // failed, the method/actor/controller could not be read
    Count
};

inline const char* climb_out_name(ClimbOut o) {
    switch (o) {
        case ClimbOut::OrigOk: return "original ok";
        case ClimbOut::Forced: return "FORCED to 1 (mode 2, player character)";
        case ClimbOut::WouldForce: return "would be forced (probe: original 0 kept)";
        case ClimbOut::FailMode1: return "failed in mode 1 (local; never forced)";
        case ClimbOut::FailMode3: return "failed in mode 3 (never forced)";
        case ClimbOut::FailModeOther: return "failed, unexpected control mode (never forced)";
        case ClimbOut::FailMode2Other: return "failed in mode 2, not a player's character";
        case ClimbOut::Unreadable: return "failed, actor/controller unreadable (kept)";
        case ClimbOut::Count: break;
    }
    return "?";
}

struct ClimbFacts {
    bool orig_ok = false;
    bool readable = false;    // actor = [m+0x20], CC = [m+0x18], mc = [CC+0xC8], [mc+0x68] == actor, key read
    uint8_t control_mode = 0; // 0x88E8E0(mc), valid when readable
    uint32_t key = 0;         // u32 [actor+0x60]
};

// Never 1 for a failed Enter unless: readable, mode exactly 2, a player's
// character, and remote_climb=1 (forcing in mode 1/3 would make vf1 0x691DB0
// use an invalid climb-point handle).
inline ClimbOut climb_decide(Mode mode, const ClimbFacts& f) {
    if (f.orig_ok) return ClimbOut::OrigOk;
    if (!f.readable) return ClimbOut::Unreadable;
    if (f.control_mode == kControlLocal) return ClimbOut::FailMode1;
    if (f.control_mode == kControlOther) return ClimbOut::FailMode3;
    if (f.control_mode != kControlRemote) return ClimbOut::FailModeOther;
    if (!is_player_character_key(f.key)) return ClimbOut::FailMode2Other;
    return mode == Mode::On ? ClimbOut::Forced : ClimbOut::WouldForce;
}

// The value the detour returns.
inline uint8_t climb_result(ClimbOut o, uint8_t orig) { return o == ClimbOut::Forced ? uint8_t{1} : orig; }

// ---------------------------------------------------------------- 2. the wall-up byte

// The game's code: [0x1000, 0x51EB000) (the section named .rsrc). Nothing of
// the mod's ever writes there except verified hooks; the data byte must be
// outside it, outside .xtls (Denuvo) and in a writable, non-executable section.
inline constexpr uint32_t kCodeBegin = 0x1000, kCodeEnd = 0x51EB000;
inline constexpr uint32_t kScnExecute = 0x20000000, kScnRead = 0x40000000, kScnWrite = 0x80000000;

struct SectionInfo {
    char name[9] = {};
    uint32_t va = 0, vsize = 0, flags = 0;
};

enum class ByteSite : uint8_t { Ok, NoSection, InCode, Xtls, NotWritable, Executable };

inline const char* byte_site_name(ByteSite s) {
    switch (s) {
        case ByteSite::Ok: return "ok";
        case ByteSite::NoSection: return "in no section of the image";
        case ByteSite::InCode: return "inside the game's code [0x1000, 0x51EB000)";
        case ByteSite::Xtls: return "inside .xtls (Denuvo)";
        case ByteSite::NotWritable: return "in a section that is not writable";
        case ByteSite::Executable: return "in an executable section";
    }
    return "?";
}

inline const SectionInfo* section_info_of(std::span<const SectionInfo> secs, uint32_t rva) {
    for (const SectionInfo& s : secs) {
        if (rva >= s.va && rva - s.va < s.vsize) return &s;
    }
    return nullptr;
}

inline ByteSite check_data_byte(std::span<const SectionInfo> secs, uint32_t rva) {
    if (rva >= kCodeBegin && rva < kCodeEnd) return ByteSite::InCode;
    const SectionInfo* s = section_info_of(secs, rva);
    if (!s) return ByteSite::NoSection;
    if (std::string_view(s->name) == ".xtls") return ByteSite::Xtls;
    if (s->flags & kScnExecute) return ByteSite::Executable;
    if (!(s->flags & kScnWrite)) return ByteSite::NotWritable;
    return ByteSite::Ok;
}

// The page as VirtualQuery reports it: committed, read/write, no execute, no guard.
inline constexpr uint32_t kMemCommit = 0x1000;
inline constexpr uint32_t kPageReadWrite = 0x04, kPageWriteCopy = 0x08, kPageGuard = 0x100, kPageNoAccess = 0x01;
inline bool page_writable_data(uint32_t state, uint32_t protect) {
    if (state != kMemCommit || (protect & kPageGuard)) return false;
    const uint32_t base = protect & 0xFF;
    return base == kPageReadWrite || base == kPageWriteCopy;
}

// The periodic check: one observation of the byte; true = write 1 again.
struct ByteWatch {
    uint64_t checks = 0, resets = 0, unreadable = 0;
    uint8_t last_seen = 1;
    bool observe(bool read_ok, uint8_t v) {
        ++checks;
        if (!read_ok) {
            ++unreadable;
            return false;
        }
        last_seen = v;
        if (v == 1) return false;
        ++resets;
        return true;
    }
};

// ---------------------------------------------------------------- 3. the probe

// Movement methods (mc+0xF0): 5 WallUp, 6 RideOn, 7 RideOff, 10 Vehicle, 18 PointClimb.
inline constexpr uint8_t kMethodWallUp = 5, kMethodRideOn = 6, kMethodRideOff = 7, kMethodVehicle = 10,
                         kMethodPointClimb = 18;
inline constexpr bool is_ride_method(uint8_t m) {
    return m == kMethodRideOn || m == kMethodRideOff || m == kMethodVehicle;
}

// One reading of the remote character (30 ms apart).
struct Sample {
    uint32_t t_ms = 0;           // the mod's clock (QPC ms)
    bool ok = false;             // actor found and its controller read
    uint8_t method = 0xFF;       // u8 mc+0xF0
    uint8_t enter_ok = 0xFF;     // u8 [mc+0xF8+8*method]+0x44 (0xFF unreadable)
    uint32_t act0 = 0, act1 = 0; // u32 [[CC+0x88]+0x10]+0x18, the same of layer 1 (CC+0x90)
    uint32_t link = 0;           // u32 [[[actor+0x68]+0x48]+0x18]+0x10 (rider<->vehicle; 0 none)
    bool pos_ok = false;
    float pos[3] = {};           // TransformSync world (TS+0xB4 + sector TS+0xC0 * 1000)
    uint8_t wall_state = 0xFF;   // u8 D+0xEE0 (D = [CC+0xD8])
    uint32_t wall_id = 0;        // u32 D+0x1200
    float wall_up[3] = {};       // D+0x1170 (the WallUpPath's first point; 0 = never received)
};

// What a change between two consecutive samples means.
inline constexpr uint8_t kTrigRide = 1, kTrigClimb = 2, kTrigWallUp = 4;
inline uint8_t trigger_of(const Sample& prev, const Sample& cur) {
    if (!prev.ok || !cur.ok) return 0;
    uint8_t t = 0;
    if (prev.method != cur.method) {
        if (is_ride_method(prev.method) || is_ride_method(cur.method)) t |= kTrigRide;
        if (prev.method == kMethodPointClimb || cur.method == kMethodPointClimb) t |= kTrigClimb;
        if (prev.method == kMethodWallUp || cur.method == kMethodWallUp) t |= kTrigWallUp;
    }
    if (prev.link != cur.link) t |= kTrigRide;
    return t;
}

inline std::string trigger_name(uint8_t t) {
    std::string s;
    if (t & kTrigRide) s += "ride";
    if (t & kTrigClimb) s += s.empty() ? "climb" : "+climb";
    if (t & kTrigWallUp) s += s.empty() ? "wallup" : "+wallup";
    return s.empty() ? std::string("none") : s;
}

// The last N samples (the "1 s before"), fixed storage.
template <size_t N>
class SampleRing {
public:
    void push(const Sample& s) {
        buf_[head_] = s;
        head_ = (head_ + 1) % N;
        if (size_ < N) ++size_;
    }
    size_t size() const { return size_; }
    // 0 = the oldest kept.
    const Sample& at(size_t i) const { return buf_[(head_ + N - size_ + i) % N]; }
    const Sample* last() const { return size_ ? &at(size_ - 1) : nullptr; }
    void clear() { size_ = head_ = 0; }

private:
    std::array<Sample, N> buf_{};
    size_t head_ = 0, size_ = 0;
};

// The state a log segment groups by (positions are summarised, not grouped).
struct SegState {
    bool ok = false;
    uint8_t method = 0xFF, enter_ok = 0xFF, wall_state = 0xFF;
    uint32_t act0 = 0, act1 = 0, link = 0, wall_id = 0;
    float wall_up[3] = {};
    bool operator==(const SegState& o) const {
        return ok == o.ok && method == o.method && enter_ok == o.enter_ok && wall_state == o.wall_state &&
               act0 == o.act0 && act1 == o.act1 && link == o.link && wall_id == o.wall_id &&
               std::memcmp(wall_up, o.wall_up, sizeof wall_up) == 0;
    }
};

inline SegState state_of(const Sample& s) {
    SegState st;
    st.ok = s.ok;
    if (!s.ok) return st;
    st.method = s.method;
    st.enter_ok = s.enter_ok;
    st.wall_state = s.wall_state;
    st.act0 = s.act0;
    st.act1 = s.act1;
    st.link = s.link;
    st.wall_id = s.wall_id;
    std::memcpy(st.wall_up, s.wall_up, sizeof st.wall_up);
    return st;
}

struct Segment {
    int32_t t0 = 0, t1 = 0;  // ms relative to the trigger
    uint32_t n = 0;
    SegState st;
    bool pos_ok = false;
    float p0[3] = {}, p1[3] = {};
    float ymin = 0, ymax = 0;
};

// One episode: from the trigger's 1 s before to 5 s after its last trigger
// (a new trigger inside extends it, up to kMaxSpanMs in all).
class Episode {
public:
    static constexpr int32_t kBeforeMs = 1000, kAfterMs = 5000, kMaxSpanMs = 20000;
    static constexpr size_t kMaxSegments = 64;

    bool active() const { return active_; }
    uint32_t key() const { return key_; }

    template <size_t N>
    void start(uint32_t key, uint32_t trigger_ms, uint8_t why, const Sample& from, const Sample& to,
               const SampleRing<N>& ring) {
        *this = Episode{};
        active_ = true;
        key_ = key;
        t_trigger_ = trigger_ms;
        end_ms_ = trigger_ms + kAfterMs;
        why_ = why;
        first_ = state_of(from);
        second_ = state_of(to);
        for (size_t i = 0; i < ring.size(); ++i) {
            const Sample& s = ring.at(i);
            if (static_cast<int32_t>(s.t_ms - trigger_ms) >= -kBeforeMs) add(s);
        }
    }

    // A trigger while active: extends the window (bounded).
    void retrigger(uint32_t trigger_ms, uint8_t why) {
        why_ |= why;
        ++retriggers_;
        const uint32_t cap = t_trigger_ + kMaxSpanMs;
        const uint32_t want = trigger_ms + kAfterMs;
        end_ms_ = static_cast<int32_t>(want - cap) > 0 ? cap : want;
    }

    void add(const Sample& s) {
        ++samples_;
        const int32_t rel = static_cast<int32_t>(s.t_ms - t_trigger_);
        const SegState st = state_of(s);
        if (nseg_ == 0 || !(segs_[nseg_ - 1].st == st)) {
            if (nseg_ == kMaxSegments) {
                ++dropped_;  // the last segment absorbs nothing more: counted
                return;
            }
            Segment& g = segs_[nseg_++];
            g = Segment{};
            g.t0 = g.t1 = rel;
            g.st = st;
        }
        Segment& g = segs_[nseg_ - 1];
        g.t1 = rel;
        ++g.n;
        if (s.ok && s.pos_ok) {
            if (!g.pos_ok) {
                std::memcpy(g.p0, s.pos, sizeof g.p0);
                g.ymin = g.ymax = s.pos[1];
                g.pos_ok = true;
            }
            std::memcpy(g.p1, s.pos, sizeof g.p1);
            if (s.pos[1] < g.ymin) g.ymin = s.pos[1];
            if (s.pos[1] > g.ymax) g.ymax = s.pos[1];
        }
    }

    bool done(uint32_t now_ms) const { return active_ && static_cast<int32_t>(now_ms - end_ms_) >= 0; }
    void close() { active_ = false; }

    // The compact log: one header, one line per segment.
    std::vector<std::string> lines(uint64_t number, bool closed_early) const {
        std::vector<std::string> out;
        out.push_back(std::format(
            "RemoteMotion probe #{}: actor 0x{:08X} {} at t0 (method {} -> {}, link 0x{:X} -> 0x{:X}){} | {} samples "
            "from {} to {} ms, {} segments{}{}",
            number, key_, trigger_name(why_), method_text(first_.method), method_text(second_.method), first_.link,
            second_.link, retriggers_ ? std::format(", {} more triggers", retriggers_) : std::string(), samples_,
            nseg_ ? segs_[0].t0 : 0, nseg_ ? segs_[nseg_ - 1].t1 : 0, nseg_,
            dropped_ ? std::format(", {} samples past the segment limit not shown", dropped_) : std::string(),
            closed_early ? " (cut: actor gone or probe stopped)" : ""));
        for (size_t i = 0; i < nseg_; ++i) out.push_back(segment_text(segs_[i]));
        return out;
    }

    static std::string method_text(uint8_t m) {
        switch (m) {
            case kMethodWallUp: return "5 WallUp";
            case kMethodRideOn: return "6 RideOn";
            case kMethodRideOff: return "7 RideOff";
            case kMethodVehicle: return "10 Vehicle";
            case kMethodPointClimb: return "18 PointClimb";
            case 0xFF: return "?";
            default: return std::to_string(m);
        }
    }

    static std::string segment_text(const Segment& g) {
        if (!g.st.ok) return std::format("  [{:+}..{:+} ms x{}] actor/controller not readable", g.t0, g.t1, g.n);
        std::string pos = "pos ?";
        if (g.pos_ok) {
            const float dx = g.p1[0] - g.p0[0], dy = g.p1[1] - g.p0[1], dz = g.p1[2] - g.p0[2];
            pos = std::format("pos ({:.2f}, {:.2f}, {:.2f}) -> ({:.2f}, {:.2f}, {:.2f}) moved {:.2f} m, y {:.2f}..{:.2f}",
                              g.p0[0], g.p0[1], g.p0[2], g.p1[0], g.p1[1], g.p1[2],
                              std::sqrt(dx * dx + dy * dy + dz * dz), g.ymin, g.ymax);
        }
        std::string wall;
        if (g.st.method == kMethodWallUp || g.st.wall_id || g.st.wall_up[0] != 0.0f || g.st.wall_up[1] != 0.0f ||
            g.st.wall_up[2] != 0.0f) {
            wall = std::format(" | wall D+0xEE0 {} id 0x{:X} up ({:.2f}, {:.2f}, {:.2f})", g.st.wall_state,
                               g.st.wall_id, g.st.wall_up[0], g.st.wall_up[1], g.st.wall_up[2]);
        }
        return std::format("  [{:+}..{:+} ms x{}] method {} enter {} | act {:08x}/{:08x} | link 0x{:X} | {}{}", g.t0,
                           g.t1, g.n, method_text(g.st.method),
                           g.st.enter_ok == 0xFF ? std::string("?") : std::to_string(g.st.enter_ok), g.st.act0,
                           g.st.act1, g.st.link, pos, wall);
    }

    size_t segments() const { return nseg_; }
    const Segment& segment(size_t i) const { return segs_[i]; }
    uint8_t why() const { return why_; }
    uint32_t samples() const { return samples_; }
    uint32_t end_ms() const { return end_ms_; }

private:
    bool active_ = false;
    uint32_t key_ = 0, t_trigger_ = 0, end_ms_ = 0, samples_ = 0, retriggers_ = 0, dropped_ = 0;
    uint8_t why_ = 0;
    SegState first_, second_;
    std::array<Segment, kMaxSegments> segs_{};
    size_t nseg_ = 0;
};

// Which actors are "the other player": a player's character (0xA01...), with
// an owning user (+0x90 != 0, so the game's mode function answers 2 unless it
// is locally controlled), and not this client's own controlled actor.
inline bool is_remote_player_candidate(uint32_t key, uint32_t owner_user, uintptr_t actor, uintptr_t local_actor,
                                       uint32_t local_key) {
    return actor != 0 && is_player_character_key(key) && owner_user != 0 && actor != local_actor &&
           key != local_key;
}

// ---------------------------------------------------------------- 4. divergence_probe
//
// [session2] divergence_probe = 0 | 1 (pendientes/p8-impl-log-async.md §3; p8-s8-congelacion.md §3.3), both
// PCs. Every 30 ms (the probe's sample): the other player's character in this client's memory (TS+0xB4 +
// sector, the probe's `pos`) against the position this mod's own channel reports for that player (the HUD's,
// ClientSession::remote_state, published by the network thread). Beyond kThresholdM for more than kHoldMs:
// one line per second while it lasts, and one line when it ends.
//
// The distance that decides is measured against the channel's TRAIL of the last kTrailMs, not only its newest
// point: the actor in memory plays the network kGameDelay (200 ms for a 0xA character) late, plus the
// latency; at a run (~7 m/s) the plain distance to the newest point is ~1.6 m all the time, riding ~3 m. A
// character that follows the path late stays near the trail; a frozen one leaves it behind. Both distances
// are logged.

inline constexpr float kDivergenceThresholdM = 1.5f;
inline constexpr uint32_t kDivergenceHoldMs = 1000;    // over the threshold for longer than this
inline constexpr uint32_t kDivergenceLineEveryMs = 1000;
inline constexpr uint32_t kDivergenceMaxLines = 900;   // a session's budget (15 min of divergence); then counted
inline constexpr uint32_t kTrailMs = 600;              // 200 ms delay + latency + the channel's send period
inline constexpr uint32_t kChannelStaleMs = 2000;      // no newer channel position: not compared

struct ChannelPoint {
    uint64_t t_ms = 0;  // the mod's clock (QPC ms) when the network thread got it
    float pos[3] = {};
};

inline float dist3(const float a[3], const float b[3]) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Distance from p to the segment [a, b].
inline float dist_to_segment(const float p[3], const float a[3], const float b[3]) {
    const float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float ap[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
    const float len2 = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
    float t = len2 > 1e-12f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    const float q[3] = {a[0] + t * ab[0], a[1] + t * ab[1], a[2] + t * ab[2]};
    return dist3(p, q);
}

struct ChannelDistance {
    bool ok = false;        // a channel position no older than kChannelStaleMs
    float latest = 0;       // to the newest channel position
    float trail = 0;        // to the trail of the last kTrailMs (<= latest): what decides
    uint32_t age_ms = 0;    // of the newest channel position
    uint32_t in_trail = 0;  // channel positions in the trail
    float chan[3] = {};     // the newest channel position
};

// The channel's last N positions (fixed storage; one writer, the caller serialises).
template <size_t N>
class ChannelTrail {
public:
    void push(uint64_t t_ms, const float pos[3]) {
        ChannelPoint& c = buf_[head_];
        c.t_ms = t_ms;
        std::memcpy(c.pos, pos, sizeof c.pos);
        head_ = (head_ + 1) % N;
        if (size_ < N) ++size_;
    }
    size_t size() const { return size_; }
    const ChannelPoint& at(size_t i) const { return buf_[(head_ + N - size_ + i) % N]; }  // 0 = oldest
    void clear() { size_ = head_ = 0; }

    ChannelDistance distance(const float p[3], uint64_t now_ms, uint32_t trail_ms = kTrailMs,
                             uint32_t stale_ms = kChannelStaleMs) const {
        ChannelDistance d;
        if (!size_) return d;
        const ChannelPoint& newest = at(size_ - 1);
        d.age_ms = now_ms > newest.t_ms ? static_cast<uint32_t>(std::min<uint64_t>(now_ms - newest.t_ms, 0xFFFFFFFFu)) : 0;
        std::memcpy(d.chan, newest.pos, sizeof d.chan);
        if (d.age_ms > stale_ms) return d;
        d.ok = true;
        d.latest = dist3(p, newest.pos);
        d.trail = d.latest;
        d.in_trail = 1;
        // From the newest backwards: every segment that ends inside the trail window (the first one that starts
        // before it too, so the window is covered).
        for (size_t i = size_ - 1; i > 0; --i) {
            const ChannelPoint& b = at(i);
            const ChannelPoint& a = at(i - 1);
            if (now_ms > b.t_ms && now_ms - b.t_ms > trail_ms) break;
            d.trail = std::min(d.trail, dist_to_segment(p, a.pos, b.pos));
            if (!(now_ms > a.t_ms && now_ms - a.t_ms > trail_ms)) ++d.in_trail;
        }
        return d;
    }

private:
    std::array<ChannelPoint, N> buf_{};
    size_t head_ = 0, size_ = 0;
};

// The session's numbers, shared by every tracked character's watch: episode numbering and ONE line budget
// (kDivergenceMaxLines) for the per-second lines AND the end lines.
struct DivergenceLedger {
    uint32_t episodes = 0, lines = 0, unlogged = 0, short_spikes = 0;
    bool take_line() {
        if (lines >= kDivergenceMaxLines) {
            ++unlogged;
            return false;
        }
        ++lines;
        return true;
    }
};

// The watch of ONE tracked character against the channel: when to write a line. Fed every sample.
class DivergenceWatch {
public:
    enum class Kind : uint8_t { None, Line, End };
    struct Step {
        Kind kind = Kind::None;
        uint32_t episode = 0;    // 1.. (numbered at its first line, in the ledger)
        uint32_t over_ms = 0;    // since the distance went over the threshold
        uint32_t line = 0;       // the line's number inside the episode (1..)
        float max_m = 0;         // the largest trail distance of the episode
        bool invalid_end = false;// ended because the comparison stopped (no actor / stale channel), not back under
    };

    Step observe(uint64_t now_ms, bool valid, float trail_m, DivergenceLedger& led) {
        Step s;
        if (valid && trail_m > kDivergenceThresholdM) {
            if (!over_) {
                over_ = true;
                since_ = now_ms;
                max_ = 0;
                lines_ = 0;
                last_line_ = 0;
                episode_ = 0;
            }
            max_ = std::max(max_, trail_m);
            const uint64_t over = now_ms - since_;
            if (over > kDivergenceHoldMs && (lines_ == 0 || now_ms - last_line_ >= kDivergenceLineEveryMs)) {
                if (lines_ == 0) episode_ = ++led.episodes;
                ++lines_;
                last_line_ = now_ms;
                if (!led.take_line()) return s;
                s.kind = Kind::Line;
                s.episode = episode_;
                s.over_ms = static_cast<uint32_t>(std::min<uint64_t>(over, 0xFFFFFFFFu));
                s.line = lines_;
                s.max_m = max_;
            }
            return s;
        }
        if (!over_) return s;
        over_ = false;
        const uint64_t over = now_ms - since_;
        if (lines_ == 0) {
            ++led.short_spikes;  // over the threshold for <= kHoldMs: not an episode
            return s;
        }
        if (!led.take_line()) return s;  // the end line counts against the same budget
        s.kind = Kind::End;
        s.episode = episode_;
        s.over_ms = static_cast<uint32_t>(std::min<uint64_t>(over, 0xFFFFFFFFu));
        s.line = lines_;
        s.max_m = max_;
        s.invalid_end = !valid;
        return s;
    }

    bool over() const { return over_; }

private:
    bool over_ = false;
    uint64_t since_ = 0, last_line_ = 0;
    float max_ = 0;
    uint32_t lines_ = 0, episode_ = 0;
};

// Before the game's control-mode function is called on a controller from the mod's thread (as climb_facts
// does in the detour): the control component and the movement controller both name this actor.
inline bool mc_identity_ok(uintptr_t actor, uintptr_t cc_actor, uintptr_t mc, uintptr_t mc_actor) {
    return actor != 0 && mc != 0 && cc_actor == actor && mc_actor == actor;
}

// The interpolation buffer mc+0x3D0 (movimiento-remoto-cliente.md §2; build 2976): +0x00 u8 done, +0x08 the
// sample array (0xA0 bytes each: vec3 pos +0, u64 ts +0x98), +0x10 u32 count, +0x18 the last interpolated
// sample (0xA0 bytes: rewritten by every update that finds T > ts[0], with ts = T = now - delay).
inline constexpr uint32_t kInterpPosBuffer = 0x3D0;
inline constexpr uint32_t kInterpDone = 0x00, kInterpArray = 0x08, kInterpCount = 0x10, kInterpShown = 0x18;
inline constexpr uint32_t kInterpSampleSize = 0xA0, kInterpSampleTs = 0x98;
inline constexpr uint32_t kInterpSaneCount = 64;  // the game keeps <= 30

struct InterpView {
    bool ok = false;
    uint8_t done = 0xFF;
    uint32_t count = 0;
    uint64_t ts_first = 0, ts_last = 0;  // 0 with count 0
    bool shown_ok = false;
    uint64_t shown_ts = 0;               // T of the last update that wrote the shown sample
    float shown[3] = {};
};

// Mem: ptr(a, uintptr_t&), u8(a, uint8_t&), u32(a, uint32_t&), u64(a, uint64_t&), vec3(a, float[3]); each false
// when unreadable (SEH-guarded reads in the mod, a fake memory in the tests).
template <class Mem>
InterpView read_interp(const Mem& m, uintptr_t buf) {
    InterpView v;
    if (!buf || !m.u32(buf + kInterpCount, v.count) || v.count > kInterpSaneCount) return v;
    m.u8(buf + kInterpDone, v.done);
    if (v.count) {
        uintptr_t arr = 0;
        if (!m.ptr(buf + kInterpArray, arr) || !arr) return v;
        if (!m.u64(arr + kInterpSampleTs, v.ts_first)) return v;
        if (!m.u64(arr + static_cast<uintptr_t>(v.count - 1) * kInterpSampleSize + kInterpSampleTs, v.ts_last)) return v;
    }
    v.ok = true;
    v.shown_ok = m.u64(buf + kInterpShown + kInterpSampleTs, v.shown_ts) && m.vec3(buf + kInterpShown, v.shown);
    return v;
}

// Whether the per-frame update wrote the shown sample lately: its ts moved.
struct ShownWatch {
    uint64_t ts = 0;          // the last shown ts seen
    uint64_t changed_ms = 0;  // the mod's clock when it last moved (0 = never seen moving)
    bool seen = false;
    void observe(uint64_t now_ms, bool ok, uint64_t shown_ts) {
        if (!ok) return;
        if (seen && shown_ts != ts) changed_ms = now_ms;
        ts = shown_ts;
        seen = true;
    }
    void reset() { *this = ShownWatch{}; }
    // -1 never seen moving, else ms since it last moved.
    int64_t since_ms(uint64_t now_ms) const {
        return changed_ms ? static_cast<int64_t>(now_ms - changed_ms) : -1;
    }
};

// Everything one line says.
struct DivergenceFacts {
    DivergenceWatch::Step step;
    uint32_t key = 0;
    uintptr_t actor = 0, mc = 0;
    Sample s;                        // the remote character's probe sample (method, actions, pos)
    ChannelDistance cd;
    uint8_t control_mode = 0xFF;     // 0x88E8E0(mc); 0xFF unknown (not verified / not called)
    InterpView iv;
    bool now_ok = false;
    uint64_t game_now = 0;           // the client's clock: frame ms + bias [exe+0x6D69438]
    uint32_t delay = 0;              // the game's global delay exe+0x6CB9548 (a 0xA character always uses it)
    int64_t shown_since_ms = -1;     // ShownWatch::since_ms
    bool local_ok = false;
    uint8_t local_method = 0xFF;
    uint32_t local_act0 = 0, local_act1 = 0;
};

inline std::string method_text(uint8_t m) {
    switch (m) {
        case 0: return "0";
        case kMethodWallUp: return "5 WallUp";
        case kMethodRideOn: return "6 RideOn";
        case kMethodRideOff: return "7 RideOff";
        case kMethodVehicle: return "10 Vehicle";
        case 13: return "13 Sequencer";
        case kMethodPointClimb: return "18 PointClimb";
        case 0xFF: return "?";
        default: return std::to_string(m);
    }
}

inline std::string signed_ms(int64_t v) { return v >= 0 ? std::format("+{}", v) : std::format("{}", v); }

inline std::string divergence_text(const DivergenceFacts& f) {
    const auto& st = f.step;
    if (st.kind == DivergenceWatch::Kind::End) {
        return std::format("RemoteMotion divergence #{} END key 0x{:08X} after {} ms over {:.1f} m ({} line(s), max "
                           "{:.2f} m): {}",
                           st.episode, f.key, st.over_ms, kDivergenceThresholdM, st.line, st.max_m,
                           st.invalid_end ? "the comparison stopped (no actor or no fresh channel position)"
                                          : "back within the threshold");
    }
    std::string buf;
    if (!f.iv.ok) {
        buf = "unreadable";
    } else {
        const std::string t_text =
            f.now_ok ? std::format("now-delay {} (now {}, delay {}) = ts[last]{} ms, ts[0]{} ms", f.game_now - f.delay,
                                   f.game_now, f.delay,
                                   f.iv.count ? signed_ms(static_cast<int64_t>(f.game_now - f.delay - f.iv.ts_last))
                                              : std::string(" n/a"),
                                   f.iv.count ? signed_ms(static_cast<int64_t>(f.game_now - f.delay - f.iv.ts_first))
                                              : std::string(" n/a"))
                     : std::string("now-delay ? (the frame clock is unreadable)");
        buf = std::format("count {} done {} ts[0] {} ts[last] {} | {} | update {} (shown ts {}{})", f.iv.count,
                          f.iv.done, f.iv.ts_first, f.iv.ts_last, t_text,
                          f.shown_since_ms < 0      ? "NOT seen moving the shown sample"
                          : f.shown_since_ms <= 1000 ? "yes in the last 1 s"
                                                     : std::format("NO for {} ms", f.shown_since_ms),
                          f.iv.shown_ts,
                          f.iv.shown_ok ? std::format(", shown pos ({:.2f}, {:.2f}, {:.2f})", f.iv.shown[0],
                                                      f.iv.shown[1], f.iv.shown[2])
                                        : std::string(", shown sample unreadable"));
    }
    return std::format(
        "RemoteMotion divergence #{} [{}] +{} ms over {:.1f} m: actor 0x{:X} key 0x{:08X} | memory ({:.2f}, {:.2f}, "
        "{:.2f}) vs channel ({:.2f}, {:.2f}, {:.2f}) {:.2f} m, to its last {} ms {:.2f} m ({} pos, newest {} ms old; "
        "max {:.2f} m) | method {} enter {} | act {:08x}/{:08x} | control mode {} | buffer mc+0x3D0 (mc 0x{:X}): {} | "
        "local client: method {} act {:08x}/{:08x} (no verified dialogue/scene flag: method 13 = Sequencer is the "
        "proxy)",
        st.episode, st.line, st.over_ms, kDivergenceThresholdM, f.actor, f.key, f.s.pos[0], f.s.pos[1], f.s.pos[2],
        f.cd.chan[0], f.cd.chan[1], f.cd.chan[2], f.cd.latest, kTrailMs, f.cd.trail, f.cd.in_trail, f.cd.age_ms,
        st.max_m, method_text(f.s.ok ? f.s.method : 0xFF), f.s.enter_ok, f.s.act0, f.s.act1,
        f.control_mode == 0xFF ? std::string("?") : std::to_string(f.control_mode), f.mc, buf,
        f.local_ok ? method_text(f.local_method) : std::string("?"), f.local_act0, f.local_act1);
}

}  // namespace cdmp::session2::rmo
