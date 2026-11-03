#pragma once
// P8, the ESC / menu pause with another player connected
// (docs/research/pendientes/p8-pausa-esc.md): the pure logic of ui_pause.cpp,
// with no game access, so the tests can run it.
//
// How the game pauses (build 1.0.0.2976, the research's section 1):
//   every UI root reads the `timeScale` attribute of its layout when it is
//   created (UIGamePlayControlCommonRoot::vf1 0xE41DA0, store 0xE41EA7) into
//   its scene view +0x23C; the card and dice minigames write 0 there directly.
//   Each frame 0x35CD990 (rcx = the first root scene, result in xmm0) returns
//   the scale of the FIRST open view (state +0x26F 0 or 1) whose root's
//   vf 0x168 or whose +0x23C is not 1, else 1.0f. Its only callers are
//   0xAD143C and 0xAD14D6, both in 0xAD1300 (the frame's dt): the first feeds
//   [[p+0x18]+0x70]->vf 0x160, the second is smoothed into world+0x312C0 and
//   multiplies the dt (0xAD1569) in the local mode, so a 0 freezes the world,
//   the game clock 0x1416B80 and with it the in-process server's game-time
//   timer wheel (ServerNet+0x68).
//
// Gameplay slow motion never goes through 0x35CD990: the engine scale
// ([[p+0x18]+0x60]+0x24C, 0xAD141A), the slow-motion channels game+0xCC0 /
// +0xCD0 -> +0xCE0 (0x9514A0: actions, sequences, the special mode) and the
// sequence pause are applied by 0xAD1300 on their own, before and after it.
// So every value this hook sees is a UI view's scale; replacing it with 1.0
// leaves those untouched (FrameModel below is the arithmetic of it).

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "mod/session2/p8_probe_logic.h"

namespace cdmp::session2::uip {

// ---------------------------------------------------------------- the option

// [session2] ui_pause_block:
//   0 / off (default)  nothing is installed
//   probe              read only: the original value's transitions and their UI view are logged
//   1 / on / session   1.0 instead of a UI pause / slowdown while a P8 session is live
//   force              always (one-PC tests T1)
enum class Mode : uint8_t { Off, Probe, Session, Force };

struct ParsedMode {
    Mode mode = Mode::Off;
    bool known = true;  // false: an unknown value, treated as Off (warned)
};

inline ParsedMode parse_mode(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r')) raw.remove_suffix(1);
    // An inline comment ("probe ; T0") is not part of the value.
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) {
        raw = raw.substr(0, c);
        while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t')) raw.remove_suffix(1);
    }
    std::string v(raw);
    for (char& ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (v.empty() || v == "0" || v == "off" || v == "no" || v == "false") return {Mode::Off, true};
    if (v == "probe") return {Mode::Probe, true};
    if (v == "1" || v == "on" || v == "session" || v == "yes" || v == "true") return {Mode::Session, true};
    if (v == "force") return {Mode::Force, true};
    return {Mode::Off, false};
}

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "off";
        case Mode::Probe: return "probe";
        case Mode::Session: return "1 (only while a P8 session is live)";
        case Mode::Force: return "force (always)";
    }
    return "?";
}

// ---------------------------------------------------------------- is a session live

enum class Role : uint8_t { Host, Guest };

// Guest (guest.cpp guest_remote_live): its client plays on the host's server
// right now: the current connection was decided remote (guest_mode=remote and
// the host ready), the tunnel is up and that connection did not stop. A guest
// in observe mode never has a remote connection, so it is never live.
constexpr bool guest_live(bool remote, bool tunnel_up, bool broken) { return remote && tunnel_up && !broken; }

// What the drain thread reads (server.cpp / guest.cpp, read only, no game call).
struct LiveInputs {
    Role role = Role::Host;
    bool host_user2 = false;   // host: session 2 has a ServerUserActor (logged in, until its logout completes)
    bool guest_remote = false; // guest: guest_live(...) above
};

// Host: user 2 is logged in. Guest: its client plays on the host.
constexpr bool session_live(const LiveInputs& in) {
    return in.role == Role::Host ? in.host_user2 : in.guest_remote;
}

// ---------------------------------------------------------------- the decision

// Below this the game itself snaps its smoothed scale to 0 ([0x6D2A1B8] =
// 0.05 at 0xAD1541): a "pause". Between it and 1: a UI slowdown.
inline constexpr float kPauseBelow = 0.05f;

enum class Kind : uint8_t {
    Neutral,  // exactly 1.0f: no UI view asks for anything
    Pause,    // [0, 0.05): the menu stops the world
    Slow,     // [0.05, 1): a UI slowdown (still a UI view's request)
    Fast,     // > 1 (finite): not a pause; passed through
    Invalid,  // NaN, infinite or negative: not understood; passed through
};

inline Kind classify(float v) {
    if (std::isnan(v) || std::isinf(v) || v < 0.0f) return Kind::Invalid;
    if (v == 1.0f) return Kind::Neutral;
    if (v > 1.0f) return Kind::Fast;
    return v < kPauseBelow ? Kind::Pause : Kind::Slow;
}

inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Neutral: return "none";
        case Kind::Pause: return "pause";
        case Kind::Slow: return "UI slowdown";
        case Kind::Fast: return "faster than 1 (passed)";
        case Kind::Invalid: return "invalid (passed)";
    }
    return "?";
}

// Only a UI view's pause or slowdown is replaced; 1, > 1 and garbage go through.
constexpr bool blockable(Kind k) { return k == Kind::Pause || k == Kind::Slow; }

// Whether the hook replaces values right now.
constexpr bool active(Mode m, bool live) { return m == Mode::Force || (m == Mode::Session && live); }

struct Decision {
    float value = 1.0f;     // what 0x35CD990 returns to the game
    bool blocked = false;   // the original was replaced by 1.0
    bool would = false;     // a blockable value (probe: what mode 1 / force would do)
};

inline Decision decide(Mode m, bool live, float original) {
    Decision d;
    d.would = blockable(classify(original));
    d.blocked = d.would && active(m, live);
    d.value = d.blocked ? 1.0f : original;
    return d;
}

// ---------------------------------------------------------------- 0xAD1300, as far as the pause goes

// The arithmetic of the dt in the local mode (the research's section 1.2,
// steps 5.2-5.5), simplified to what matters here: the UI scale returned by
// 0x35CD990 is smoothed (rate [0x6D2A028] = 2.5/s, snapped to 0 below 0.05)
// and multiplies the dt; the slow-motion channels (+0xCE0) multiply it
// afterwards, independently. The tests use it to show that replacing the UI
// value keeps a gameplay slow motion as it was.
struct FrameModel {
    float smoothed = 1.0f;  // world+0x312C0 (stored unsnapped, 0xAD1519)
    float rate = 2.5f;      // [0x6D2A028]
    float epsilon = 1e-4f;  // close enough: snaps to the target (0xAD152E..0xAD1534)

    // What multiplies the dt: below 0.05 it counts as 0 (0xAD1541 vcmpltss/vblendvps), stored value untouched.
    float effective() const { return smoothed < kPauseBelow ? 0.0f : smoothed; }

    // One frame: returns the dt the world advances by.
    float step(float dt, float ui_scale, float slowmo) {
        if (smoothed != ui_scale) {
            const float k = 1.0f - std::exp(-rate * dt);
            smoothed += k * (ui_scale - smoothed);
            if (std::fabs(smoothed - ui_scale) <= epsilon) smoothed = ui_scale;
        }
        return dt * effective() * slowmo;
    }
};

// ---------------------------------------------------------------- the views (replay of 0x35CD990's walk)

// Offsets of build 2976 (0x35CD990, decoded with capstone; the research's 1.3).
struct ViewLayout {
    static constexpr uint32_t kWorldList = 0x38;   // world -> {entries*, u32 count}
    static constexpr uint32_t kListCount = 0x08;
    static constexpr uint32_t kEntryHolder = 0xA0; // entry -> holder (0 = none)
    static constexpr uint32_t kHolderView = 0x10;  // holder -> view V
    static constexpr uint32_t kViewState = 0x26F;  // u8: 0/1 open, 2 hiding, 3 unloaded
    static constexpr uint32_t kViewRoot = 0x118;   // V -> UI root (UIGamePlayControl*)
    static constexpr uint32_t kViewScale = 0x23C;  // f32: the layout's timeScale
    static constexpr uint32_t kRootScaleSlot = 0x168;  // root vtable slot: 0x63DA70 `return 1.0f` in all of them
    static constexpr uint32_t kMaxEntries = 4096;  // more = garbage, the replay stops
};

inline constexpr size_t kMaxHits = 3;  // what one ring event carries

// An open view that asks for something: its scale is not 1, or its root's
// vf 0x168 is not the plain `return 1.0f` (its value cannot be known without
// calling it, and the hook never calls the game).
struct ViewHit {
    uint32_t index = 0;      // in the list
    uint8_t state = 0;       // +0x26F (0 or 1)
    bool custom_vf = false;  // root vf 0x168 is not 0x63DA70
    uintptr_t view = 0, root = 0, root_vtable = 0;
    uint32_t scale_bits = 0;  // +0x23C as read
    float scale() const { return std::bit_cast<float>(scale_bits); }
};

struct ViewWalk {
    bool ok = false;         // the list was readable
    bool truncated = false;  // count above kMaxEntries, or an entry could not be read
    uint32_t count = 0;      // entries in the list
    uint32_t open = 0;       // open views (state 0/1)
    uint32_t hits = 0;       // open views asking for something (all of them, even past kMaxHits)
    std::array<ViewHit, kMaxHits> hit{};
    // Which hit the original returned, as far as reads can tell: the first
    // hit, unless its root has a custom vf 0x168 (then -1: ambiguous).
    int source = -1;
};

// Mem: bool u8(uintptr_t, uint8_t&), bool u32(uintptr_t, uint32_t&), bool ptr(uintptr_t, uintptr_t&); all
// checked reads. trivial_vf = exe+0x63DA70.
template <class Mem>
ViewWalk walk_views(const Mem& m, uintptr_t world, uintptr_t trivial_vf) {
    using L = ViewLayout;
    ViewWalk w;
    uintptr_t list = 0, data = 0;
    uint32_t count = 0;
    if (!world || !m.ptr(world + L::kWorldList, list) || !list || !m.ptr(list, data) ||
        !m.u32(list + L::kListCount, count)) {
        return w;
    }
    w.ok = true;
    w.count = count;
    if (count > L::kMaxEntries) {
        w.truncated = true;
        return w;
    }
    if (count && !data) {
        w.truncated = true;
        return w;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t e = 0, holder = 0, view = 0;
        if (!m.ptr(data + 8ull * i, e)) {
            w.truncated = true;
            break;
        }
        if (!e) continue;
        if (!m.ptr(e + L::kEntryHolder, holder)) {
            w.truncated = true;
            continue;
        }
        // The original reads [0 + 0x26F] for an entry without a holder; such an
        // entry never exists while it runs (or it would have crashed).
        if (!holder || !m.ptr(holder + L::kHolderView, view) || !view) continue;
        uint8_t state = 0;
        if (!m.u8(view + L::kViewState, state)) {
            w.truncated = true;
            continue;
        }
        if (state >= 2) continue;  // hiding (2) or unloaded (3+): not counted by the original
        ++w.open;
        uintptr_t root = 0, vt = 0, fn = 0;
        uint32_t bits = 0;
        bool custom = false;
        if (m.ptr(view + L::kViewRoot, root) && root) {
            custom = !m.ptr(root, vt) || !vt || !m.ptr(vt + L::kRootScaleSlot, fn) || fn != trivial_vf;
        }
        if (!m.u32(view + L::kViewScale, bits)) {
            w.truncated = true;
            continue;
        }
        if (!custom && std::bit_cast<float>(bits) == 1.0f) continue;
        if (w.hits == 0) w.source = custom ? -1 : 0;
        if (w.hits < kMaxHits) {
            ViewHit& h = w.hit[w.hits];
            h.index = i;
            h.state = state;
            h.custom_vf = custom;
            h.view = view;
            h.root = root;
            h.root_vtable = vt;
            h.scale_bits = bits;
        }
        ++w.hits;
    }
    return w;
}

// ---------------------------------------------------------------- one ring event

// Which call of 0xAD1300 (by return address): 0xAD1441 after the call at
// 0xAD143C (every frame), 0xAD14DB after 0xAD14D6 (the dt, local mode only).
enum class Site : uint8_t { Other = 0, First = 1, Second = 2 };

enum : uint16_t {
    kEvTransition = 1,  // the original value changed
    kEvSource = 2,      // same value, another view explains it now (a menu opened from another one)
};

// What the hook saw at one event; packed into a fixed p8p::Event (no
// allocation, no strings on the game's thread).
struct Note {
    uint16_t kind = kEvTransition;
    uint32_t old_bits = 0, new_bits = 0;  // the original's value, before and now
    Decision d{};                          // for the new value
    bool live = false;
    Mode mode = Mode::Off;
    Site site = Site::Other;
    uint64_t ep_seen = 0, ep_blocked = 0;  // the episode this event closes (first-call frames)
    bool walked = false;                   // the views were walked (only for a value that is not 1)
    ViewWalk walk{};
};

inline void pack(const Note& n, uintptr_t base, p8p::Event& e) {
    e.kind = n.kind;
    e.flags = static_cast<uint16_t>((n.d.blocked ? 1u : 0u) | (n.d.would ? 2u : 0u) | (n.live ? 4u : 0u) |
                                    (static_cast<unsigned>(n.site) & 3u) << 3 |
                                    (static_cast<unsigned>(n.mode) & 3u) << 5);
    e.v[0] = n.old_bits | static_cast<uint64_t>(n.new_bits) << 32;
    e.v[1] = n.ep_seen;
    e.v[2] = n.ep_blocked;
    e.v[3] = n.walk.count | static_cast<uint64_t>(n.walk.open) << 32;
    e.v[4] = n.walk.hits | static_cast<uint64_t>(static_cast<uint8_t>(n.walk.source + 1)) << 32 |
             (n.walk.ok ? 1ull : 0ull) << 40 | (n.walk.truncated ? 1ull : 0ull) << 41 | (n.walked ? 1ull : 0ull) << 42;
    uint64_t meta = 0;
    for (size_t i = 0; i < kMaxHits; ++i) {
        const ViewHit& h = n.walk.hit[i];
        const bool in_image = h.root_vtable >= base && h.root_vtable - base <= 0xFFFFFFFFull;
        const uint64_t rva = in_image ? h.root_vtable - base : 0;
        e.v[5 + 2 * i] = h.view;
        e.v[6 + 2 * i] = rva | static_cast<uint64_t>(h.scale_bits) << 32;
        const uint64_t m = (h.index & 0xFFFFull) | (h.state & 1ull) << 16 | (h.custom_vf ? 1ull : 0ull) << 17 |
                           (in_image ? 1ull : 0ull) << 18;
        meta |= m << (20 * i);
    }
    e.v[11] = meta;
}

inline Note unpack(const p8p::Event& e, uintptr_t base) {
    Note n;
    n.kind = e.kind;
    n.d.blocked = (e.flags & 1u) != 0;
    n.d.would = (e.flags & 2u) != 0;
    n.live = (e.flags & 4u) != 0;
    n.site = static_cast<Site>((e.flags >> 3) & 3u);
    n.mode = static_cast<Mode>((e.flags >> 5) & 3u);
    n.old_bits = static_cast<uint32_t>(e.v[0]);
    n.new_bits = static_cast<uint32_t>(e.v[0] >> 32);
    n.d.value = n.d.blocked ? 1.0f : std::bit_cast<float>(n.new_bits);
    n.ep_seen = e.v[1];
    n.ep_blocked = e.v[2];
    n.walk.count = static_cast<uint32_t>(e.v[3]);
    n.walk.open = static_cast<uint32_t>(e.v[3] >> 32);
    n.walk.hits = static_cast<uint32_t>(e.v[4]);
    n.walk.source = static_cast<int>(static_cast<uint8_t>(e.v[4] >> 32)) - 1;
    n.walk.ok = (e.v[4] >> 40 & 1) != 0;
    n.walk.truncated = (e.v[4] >> 41 & 1) != 0;
    n.walked = (e.v[4] >> 42 & 1) != 0;
    for (size_t i = 0; i < kMaxHits; ++i) {
        ViewHit& h = n.walk.hit[i];
        const uint64_t m = e.v[11] >> (20 * i);
        h.index = static_cast<uint32_t>(m & 0xFFFF);
        h.state = static_cast<uint8_t>(m >> 16 & 1);
        h.custom_vf = (m >> 17 & 1) != 0;
        const bool in_image = (m >> 18 & 1) != 0;
        h.view = static_cast<uintptr_t>(e.v[5 + 2 * i]);
        h.root_vtable = in_image ? base + static_cast<uint32_t>(e.v[6 + 2 * i]) : 0;
        h.scale_bits = static_cast<uint32_t>(e.v[6 + 2 * i] >> 32);
    }
    return n;
}

// The vtable the drain attributes an event's value to: the source hit's, or
// the first hit's when the source is ambiguous (a custom vf 0x168); 0 when no
// open view explained it (not walked, unreadable, or no hit).
inline uintptr_t attributed_vtable(const Note& n) {
    if (!n.walked || !n.walk.ok || n.walk.hits == 0) return 0;
    const int i = n.walk.source >= 0 ? n.walk.source : 0;
    return n.walk.hit[static_cast<size_t>(i)].root_vtable;
}

// ---------------------------------------------------------------- which UI blocked how much

// The drain thread's per-UI ledger (one caller, no locking): episodes of a UI
// value and the frames the hook replaced during them, by the root's vtable
// (0 = not attributed: no open view explained the value).
class Ledger {
public:
    struct Row {
        uint64_t episodes = 0;        // transitions into a value this UI explained
        uint64_t frames_blocked = 0;  // first-call frames replaced by 1.0 while it was the source
        uint64_t frames_seen = 0;     // first-call frames it was the source (blocked or not)
        float min_scale = 1.0f;
    };
    void episode(uintptr_t vtable, float scale) {
        Row& r = rows_[vtable];
        ++r.episodes;
        if (!std::isnan(scale) && scale < r.min_scale) r.min_scale = scale;
    }
    void frames(uintptr_t vtable, uint64_t seen, uint64_t blocked) {
        if (!seen && !blocked) return;
        Row& r = rows_[vtable];
        r.frames_seen += seen;
        r.frames_blocked += blocked;
    }
    const std::map<uintptr_t, Row>& rows() const { return rows_; }
    uint64_t total_blocked() const {
        uint64_t n = 0;
        for (const auto& [vt, r] : rows_) n += r.frames_blocked;
        return n;
    }

private:
    std::map<uintptr_t, Row> rows_;
};

// The hook's per-episode frame counters (game thread writes, drain reads):
// an episode starts at each transition of the original value or change of
// its source view; the hook closes it by handing its counts to the event.
struct EpisodeCounter {
    std::atomic<uint64_t> seen{0}, blocked{0};
    // Game thread: one first-call frame.
    void frame(bool was_blocked) {
        seen.fetch_add(1, std::memory_order_relaxed);
        if (was_blocked) blocked.fetch_add(1, std::memory_order_relaxed);
    }
    // Game thread: close the episode, returning its counts.
    void close(uint64_t& s, uint64_t& b) {
        s = seen.exchange(0, std::memory_order_relaxed);
        b = blocked.exchange(0, std::memory_order_relaxed);
    }
};

}  // namespace cdmp::session2::uip
