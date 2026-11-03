// P8, menus without the pause while another player is connected
// (src/mod/session2/ui_pause_logic.h; docs/research/pendientes/p8-pausa-esc.md,
// section "Implementado").
//
// SCOPE: offline logic only. The ui_pause_block modes, the decision table, the
// session-live gating, a model of 0xAD1300's dt arithmetic (to show that a
// gameplay slow motion is untouched), the replay of 0x35CD990's walk of the
// open views on fake memory, the ring event packing and the per-UI ledger.
// They do NOT test the hook, its ABI, the byte checks against the running game
// nor the game's behaviour with the menus open (T0-T3, live).

#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>

#include "mod/session2/ui_pause_logic.h"

using namespace cdmp::session2;
using uip::Kind;
using uip::Mode;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// ---------------------------------------------------------------- the option

void test_parse_mode() {
    struct Case {
        const char* raw;
        Mode mode;
        bool known;
    };
    const Case cases[] = {
        {"", Mode::Off, true},        {"0", Mode::Off, true},          {"off", Mode::Off, true},
        {" OFF ", Mode::Off, true},   {"probe", Mode::Probe, true},    {"Probe", Mode::Probe, true},
        {"1", Mode::Session, true},   {"on", Mode::Session, true},     {"session", Mode::Session, true},
        {"force", Mode::Force, true}, {"FORCE\r", Mode::Force, true},  {"probe ; T0", Mode::Probe, true},
        {"force#T1", Mode::Force, true}, {"2", Mode::Off, false},      {"forced", Mode::Off, false},
        {"yes please", Mode::Off, false},
    };
    for (const Case& c : cases) {
        const uip::ParsedMode p = uip::parse_mode(c.raw);
        CHECK(p.mode == c.mode);
        CHECK(p.known == c.known);
        if (p.mode != c.mode || p.known != c.known) std::printf("  case '%s'\n", c.raw);
    }
    CHECK(std::strcmp(uip::mode_name(Mode::Probe), "probe") == 0);
}

// ---------------------------------------------------------------- the decision table

void test_classify() {
    CHECK(uip::classify(1.0f) == Kind::Neutral);
    CHECK(uip::classify(0.0f) == Kind::Pause);
    CHECK(uip::classify(-0.0f) == Kind::Pause);  // -0 is not "negative" (== 0)
    CHECK(uip::classify(0.049f) == Kind::Pause);
    CHECK(uip::classify(0.05f) == Kind::Slow);
    CHECK(uip::classify(0.3f) == Kind::Slow);
    CHECK(uip::classify(0.9999f) == Kind::Slow);
    CHECK(uip::classify(1.0001f) == Kind::Fast);
    CHECK(uip::classify(2.0f) == Kind::Fast);
    CHECK(uip::classify(-0.5f) == Kind::Invalid);
    CHECK(uip::classify(std::numeric_limits<float>::quiet_NaN()) == Kind::Invalid);
    CHECK(uip::classify(std::numeric_limits<float>::infinity()) == Kind::Invalid);
    CHECK(uip::blockable(Kind::Pause) && uip::blockable(Kind::Slow));
    CHECK(!uip::blockable(Kind::Neutral) && !uip::blockable(Kind::Fast) && !uip::blockable(Kind::Invalid));
}

void test_decision_table() {
    const float values[] = {0.0f, 0.02f, 0.3f, 1.0f, 1.5f, -1.0f, std::numeric_limits<float>::quiet_NaN()};
    const Mode modes[] = {Mode::Off, Mode::Probe, Mode::Session, Mode::Force};
    for (Mode m : modes) {
        for (bool live : {false, true}) {
            for (float v : values) {
                const uip::Decision d = uip::decide(m, live, v);
                const bool blockable = uip::blockable(uip::classify(v));
                const bool expect_block = blockable && (m == Mode::Force || (m == Mode::Session && live));
                CHECK(d.would == blockable);
                CHECK(d.blocked == expect_block);
                if (expect_block) {
                    CHECK(d.value == 1.0f);
                } else if (std::isnan(v)) {
                    CHECK(std::isnan(d.value));  // garbage goes through untouched
                } else {
                    CHECK(d.value == v);  // the original, bit for bit
                }
            }
        }
    }
    // The named rows of the research (p8-pausa-esc.md section 4.a).
    CHECK(!uip::decide(Mode::Probe, true, 0.0f).blocked);   // probe never changes anything
    CHECK(uip::decide(Mode::Probe, true, 0.0f).would);      // ...but says what 1/force would do
    CHECK(!uip::decide(Mode::Session, false, 0.0f).blocked);  // single player: the menu pauses as usual
    CHECK(uip::decide(Mode::Session, true, 0.0f).blocked);    // a P8 session: no pause
    CHECK(uip::decide(Mode::Force, false, 0.0f).blocked);     // force: always
    CHECK(uip::decide(Mode::Session, true, 0.25f).blocked);   // a UI slowdown is a UI request too
    CHECK(!uip::decide(Mode::Force, true, 1.0f).blocked);     // nothing open: nothing to replace
    CHECK(!uip::decide(Mode::Force, true, 1.5f).blocked);     // > 1 is not a pause
    CHECK(!uip::decide(Mode::Off, true, 0.0f).blocked);
}

// ---------------------------------------------------------------- the session gating

void test_session_live() {
    // Guest: remote AND tunnel up AND not broken.
    for (int bits = 0; bits < 8; ++bits) {
        const bool remote = bits & 1, tunnel = bits & 2, broken = bits & 4;
        CHECK(uip::guest_live(remote, tunnel, broken) == (remote && tunnel && !broken));
    }
    uip::LiveInputs host;
    host.role = uip::Role::Host;
    CHECK(!uip::session_live(host));
    host.host_user2 = true;
    CHECK(uip::session_live(host));
    host.guest_remote = false;  // the guest's input never counts on the host
    CHECK(uip::session_live(host));
    uip::LiveInputs guest;
    guest.role = uip::Role::Guest;
    guest.host_user2 = true;  // nor the host's on the guest
    CHECK(!uip::session_live(guest));
    guest.guest_remote = true;
    CHECK(uip::session_live(guest));
    // Mode 1 follows the session: user 2 logs in, a menu opens, user 2 logs out with the menu still open.
    CHECK(!uip::decide(Mode::Session, uip::session_live(uip::LiveInputs{uip::Role::Host, false, false}), 0.0f).blocked);
    CHECK(uip::decide(Mode::Session, uip::session_live(uip::LiveInputs{uip::Role::Host, true, false}), 0.0f).blocked);
    CHECK(!uip::decide(Mode::Session, uip::session_live(uip::LiveInputs{uip::Role::Host, false, false}), 0.0f).blocked);
}

// ---------------------------------------------------------------- 0xAD1300's dt with a gameplay slow motion

void test_slowmo_passthrough() {
    const float dt = 1.0f / 60.0f;
    // A menu with timeScale 0 and a gameplay slow motion (channel +0xCE0) of 0.25 at the same time.
    uip::FrameModel native, hooked;
    float native_world = 0.0f, hooked_world = 0.0f;
    for (int i = 0; i < 600; ++i) {  // 10 s
        const float ui = 0.0f, slowmo = 0.25f;
        native_world += native.step(dt, uip::decide(Mode::Off, true, ui).value, slowmo);
        hooked_world += hooked.step(dt, uip::decide(Mode::Session, true, ui).value, slowmo);
    }
    // Native: the world stops (after the smoothing reaches 0).
    CHECK(native.smoothed == 0.0f && native.effective() == 0.0f);
    CHECK(native_world < 0.5f);
    // Hooked: the UI never scales, the slow motion still does: exactly dt * 0.25 per frame.
    CHECK(hooked.smoothed == 1.0f);
    CHECK(std::fabs(hooked_world - 600 * dt * 0.25f) < 1e-3f);
    // No menu, slow motion only: the hook changes nothing at all.
    uip::FrameModel a, b;
    for (int i = 0; i < 120; ++i) {
        const float wa = a.step(dt, uip::decide(Mode::Off, true, 1.0f).value, 0.5f);
        const float wb = b.step(dt, uip::decide(Mode::Force, true, 1.0f).value, 0.5f);
        CHECK(wa == wb);
    }
    // Turned on with a menu already open (the smoothed value was 0): back near 1 within a second.
    uip::FrameModel c;
    for (int i = 0; i < 600; ++i) c.step(dt, 0.0f, 1.0f);
    CHECK(c.smoothed == 0.0f);
    float prev = c.smoothed;
    for (int i = 0; i < 60; ++i) {
        c.step(dt, uip::decide(Mode::Force, false, 0.0f).value, 1.0f);
        CHECK(c.smoothed >= prev);  // monotonic towards 1
        prev = c.smoothed;
    }
    CHECK(c.smoothed > 0.9f);
}

// ---------------------------------------------------------------- the walk of 0x35CD990 on fake memory

struct FakeMem {
    std::map<uintptr_t, uint8_t> bytes;
    void put8(uintptr_t a, uint8_t v) { bytes[a] = v; }
    void put32(uintptr_t a, uint32_t v) {
        for (int i = 0; i < 4; ++i) bytes[a + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void put64(uintptr_t a, uint64_t v) {
        for (int i = 0; i < 8; ++i) bytes[a + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    void putf(uintptr_t a, float f) { put32(a, std::bit_cast<uint32_t>(f)); }
    bool get(uintptr_t a, size_t n, uint64_t& v) const {
        v = 0;
        for (size_t i = 0; i < n; ++i) {
            auto it = bytes.find(a + i);
            if (it == bytes.end()) return false;
            v |= static_cast<uint64_t>(it->second) << (8 * i);
        }
        return true;
    }
    bool u8(uintptr_t a, uint8_t& v) const {
        uint64_t x = 0;
        if (!get(a, 1, x)) return false;
        v = static_cast<uint8_t>(x);
        return true;
    }
    bool u32(uintptr_t a, uint32_t& v) const {
        uint64_t x = 0;
        if (!get(a, 4, x)) return false;
        v = static_cast<uint32_t>(x);
        return true;
    }
    bool ptr(uintptr_t a, uintptr_t& v) const {
        uint64_t x = 0;
        if (!get(a, 8, x)) return false;
        v = static_cast<uintptr_t>(x);
        return true;
    }
};

using L = uip::ViewLayout;
constexpr uintptr_t kBase = 0x140000000;
constexpr uintptr_t kTrivial = kBase + 0x63DA70;
constexpr uintptr_t kWorld = 0x10000000, kList = 0x10001000, kData = 0x10002000;

// A view with a holder, a root (vtable vt whose slot 0x168 is fn) and a scale.
void add_view(FakeMem& m, uint32_t i, uintptr_t addr, uint8_t state, float scale, uintptr_t vt, uintptr_t fn) {
    const uintptr_t entry = addr, holder = addr + 0x100, view = addr + 0x200, root = addr + 0x600;
    m.put64(kData + 8ull * i, entry);
    m.put64(entry + L::kEntryHolder, holder);
    m.put64(holder + L::kHolderView, view);
    m.put8(view + L::kViewState, state);
    m.putf(view + L::kViewScale, scale);
    if (vt) {
        m.put64(view + L::kViewRoot, root);
        m.put64(root, vt);
        m.put64(vt + L::kRootScaleSlot, fn);
    } else {
        m.put64(view + L::kViewRoot, 0);
    }
}

FakeMem world_with(uint32_t count) {
    FakeMem m;
    m.put64(kWorld + L::kWorldList, kList);
    m.put64(kList, kData);
    m.put32(kList + L::kListCount, count);
    return m;
}

void test_walk() {
    const uintptr_t vtA = kBase + 0x568BC08, vtB = kBase + 0x569C6B8, vtC = kBase + 0x5700000;
    {
        // 5 entries: a null one, an open view at 1, a hidden one at 0, ESC (open, 0), ExtraReward (open, 0).
        FakeMem m = world_with(5);
        m.put64(kData + 0, 0);
        add_view(m, 1, 0x20000000, 0, 1.0f, vtA, kTrivial);
        add_view(m, 2, 0x20010000, 2, 0.0f, vtA, kTrivial);  // hiding: not counted
        add_view(m, 3, 0x20020000, 1, 0.0f, vtB, kTrivial);
        add_view(m, 4, 0x20030000, 0, 0.0f, vtA, kTrivial);
        const uip::ViewWalk w = uip::walk_views(m, kWorld, kTrivial);
        CHECK(w.ok && !w.truncated);
        CHECK(w.count == 5 && w.open == 3 && w.hits == 2);
        CHECK(w.source == 0);
        CHECK(w.hit[0].index == 3 && w.hit[0].root_vtable == vtB && w.hit[0].scale() == 0.0f && w.hit[0].state == 1);
        CHECK(w.hit[1].index == 4 && w.hit[1].root_vtable == vtA);
        CHECK(w.hit[0].view == 0x20020000 + 0x200);
    }
    {
        // A root with its own vf 0x168 first: ambiguous source, the custom view still listed first.
        FakeMem m = world_with(2);
        add_view(m, 0, 0x20000000, 0, 1.0f, vtC, kBase + 0x123456);
        add_view(m, 1, 0x20010000, 0, 0.5f, vtA, kTrivial);
        const uip::ViewWalk w = uip::walk_views(m, kWorld, kTrivial);
        CHECK(w.hits == 2 && w.source == -1 && w.hit[0].custom_vf && !w.hit[1].custom_vf);
    }
    {
        // A view without a root reads +0x23C only (the original's je 0x35CDA1B).
        FakeMem m = world_with(1);
        add_view(m, 0, 0x20000000, 1, 0.0f, 0, 0);
        const uip::ViewWalk w = uip::walk_views(m, kWorld, kTrivial);
        CHECK(w.hits == 1 && w.source == 0 && !w.hit[0].custom_vf && w.hit[0].root_vtable == 0);
    }
    {
        // More than kMaxHits asking views: all counted, the first three kept.
        FakeMem m = world_with(5);
        for (uint32_t i = 0; i < 5; ++i) add_view(m, i, 0x20000000 + 0x10000ull * i, 0, 0.0f, vtA, kTrivial);
        const uip::ViewWalk w = uip::walk_views(m, kWorld, kTrivial);
        CHECK(w.hits == 5 && w.hit[2].index == 2);
    }
    {
        // Garbage count, unreadable list, unreadable entry.
        FakeMem m = world_with(L::kMaxEntries + 1);
        uip::ViewWalk w = uip::walk_views(m, kWorld, kTrivial);
        CHECK(w.ok && w.truncated && w.hits == 0);
        FakeMem empty;
        w = uip::walk_views(empty, kWorld, kTrivial);
        CHECK(!w.ok);
        w = uip::walk_views(empty, 0, kTrivial);
        CHECK(!w.ok);
        FakeMem part = world_with(3);
        add_view(part, 0, 0x20000000, 0, 0.0f, vtA, kTrivial);  // entries 1, 2 missing
        w = uip::walk_views(part, kWorld, kTrivial);
        CHECK(w.ok && w.truncated && w.hits == 1);
    }
    {
        // Nothing open: no hit, the original returns 1.
        FakeMem m = world_with(2);
        add_view(m, 0, 0x20000000, 3, 0.0f, vtA, kTrivial);
        add_view(m, 1, 0x20010000, 0, 1.0f, vtA, kTrivial);
        const uip::ViewWalk w = uip::walk_views(m, kWorld, kTrivial);
        CHECK(w.ok && w.open == 1 && w.hits == 0 && w.source == -1);
    }
}

// ---------------------------------------------------------------- the ring event

void test_pack() {
    uip::Note n;
    n.kind = uip::kEvSource;
    n.old_bits = std::bit_cast<uint32_t>(1.0f);
    n.new_bits = std::bit_cast<uint32_t>(0.0f);
    n.d = uip::decide(Mode::Session, true, 0.0f);
    n.live = true;
    n.mode = Mode::Session;
    n.site = uip::Site::Second;
    n.ep_seen = 12345;
    n.ep_blocked = 12000;
    n.walked = true;
    n.walk.ok = true;
    n.walk.truncated = true;
    n.walk.count = 57;
    n.walk.open = 12;
    n.walk.hits = 4;
    n.walk.source = -1;
    for (size_t i = 0; i < uip::kMaxHits; ++i) {
        uip::ViewHit& h = n.walk.hit[i];
        h.index = static_cast<uint32_t>(10 + i);
        h.state = static_cast<uint8_t>(i & 1);
        h.custom_vf = i == 0;
        h.view = 0x7FF612340000ull + i * 0x100;
        h.root_vtable = kBase + 0x568BC08 + i * 8;
        h.scale_bits = std::bit_cast<uint32_t>(0.25f * static_cast<float>(i));
    }
    p8p::Event e;
    uip::pack(n, kBase, e);
    const uip::Note u = uip::unpack(e, kBase);
    CHECK(u.kind == n.kind && u.old_bits == n.old_bits && u.new_bits == n.new_bits);
    CHECK(u.d.blocked && u.d.would && u.d.value == 1.0f && u.live && u.mode == Mode::Session);
    CHECK(u.site == uip::Site::Second);
    CHECK(u.ep_seen == 12345 && u.ep_blocked == 12000);
    CHECK(u.walked && u.walk.ok && u.walk.truncated && u.walk.count == 57 && u.walk.open == 12 && u.walk.hits == 4);
    CHECK(u.walk.source == -1);
    for (size_t i = 0; i < uip::kMaxHits; ++i) {
        const uip::ViewHit &a = n.walk.hit[i], &b = u.walk.hit[i];
        CHECK(a.index == b.index && a.state == b.state && a.custom_vf == b.custom_vf && a.view == b.view &&
              a.root_vtable == b.root_vtable && a.scale_bits == b.scale_bits);
    }
    // Ambiguous source: attributed to the first hit.
    CHECK(uip::attributed_vtable(u) == kBase + 0x568BC08);
    // A probe event: nothing blocked, the original value reported.
    uip::Note p;
    p.new_bits = std::bit_cast<uint32_t>(0.0f);
    p.d = uip::decide(Mode::Probe, false, 0.0f);
    p.mode = Mode::Probe;
    uip::pack(p, kBase, e);
    const uip::Note q = uip::unpack(e, kBase);
    CHECK(!q.d.blocked && q.d.would && q.d.value == 0.0f && q.mode == Mode::Probe && !q.walked);
    CHECK(uip::attributed_vtable(q) == 0);
    // A vtable outside the image is not packed as an RVA.
    n.walk.hit[0].root_vtable = 0x12345678;
    uip::pack(n, kBase, e);
    CHECK(uip::unpack(e, kBase).walk.hit[0].root_vtable == 0);
}

// ---------------------------------------------------------------- per-UI counts

void test_ledger_and_episodes() {
    uip::EpisodeCounter ep;
    for (int i = 0; i < 100; ++i) ep.frame(i % 4 != 0);
    uint64_t s = 0, b = 0;
    ep.close(s, b);
    CHECK(s == 100 && b == 75);
    ep.close(s, b);
    CHECK(s == 0 && b == 0);  // a new episode starts empty

    uip::Ledger l;
    const uintptr_t esc = kBase + 0x1000, reward = kBase + 0x2000;
    l.episode(esc, 0.0f);
    l.frames(esc, 600, 600);
    l.episode(reward, 0.0f);
    l.frames(reward, 120, 0);
    l.episode(esc, 0.5f);
    l.frames(esc, 60, 60);
    l.frames(0, 0, 0);  // nothing: no row
    CHECK(l.rows().size() == 2);
    CHECK(l.rows().at(esc).episodes == 2 && l.rows().at(esc).frames_blocked == 660 && l.rows().at(esc).frames_seen == 660);
    CHECK(l.rows().at(esc).min_scale == 0.0f);
    CHECK(l.rows().at(reward).frames_blocked == 0 && l.rows().at(reward).frames_seen == 120);
    CHECK(l.total_blocked() == 660);
}

}  // namespace

int main() {
    test_parse_mode();
    test_classify();
    test_decision_table();
    test_session_live();
    test_slowmo_passthrough();
    test_walk();
    test_pack();
    test_ledger_and_episodes();
    if (g_failures) {
        std::printf("optd_ui_pause: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_ui_pause: all passed\n");
    return 0;
}
