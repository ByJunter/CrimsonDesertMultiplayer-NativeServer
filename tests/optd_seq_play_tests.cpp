// P8 guest_seq_play (docs/research/pendientes/p8-impl-escenas-play.md).
//
// SCOPE: offline logic only (guest_seq_play_logic.h, and the G5 state types of guest_seq_route_logic.h):
//  - the key's values;
//  - gsp::plan: every reason, in order, for one X in one frame (field mode 2, own load not ended, the field,
//    X's identity: vtable, character, its own manager, [X+8] and its class), the vf40 question (ON only, once),
//    the 2 -> 3 step (X at 2, nothing loading, own M at 3, something loaded, once per routed 2322), the live
//    update (only with _live, the native gate byte, and X at 3);
//  - gsp::WriteMark (once per X and per routed 2322), gsp::consumed;
//  - gsp::XTable: claim by key, a new X for the same character, the 2322 count, full, and notes from
//    several threads at once.
// They do NOT test the hook, the calls into the game, its threads nor its behaviour.

#include <cstdint>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

#include "mod/session2/guest_seq_play_logic.h"
#include "mod/session2/guest_seq_route_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr uint32_t kHost = 0xA0100001;     // the host's Kliff (session 8)
constexpr uint64_t kVt = 0x1455C9720;      // exe + pa::ClientSequencerStageManager
constexpr uint64_t kCtxVt = 0x145600000;   // the class of [M+8] / [X+8]

// One X that passes everything, in state 2 with 22 instances waiting in +0x1B0 (session 8's end state).
gsp::Facts ready_x(gsp::Mode mode = gsp::Mode::On) {
    gsp::Facts f;
    f.mode = mode;
    f.live = true;
    f.code_ok = true;
    f.field_mode = 0;
    f.m = 0x4887B77BD00;  // M of user 2 (session 8)
    f.m_ready = 3;
    f.m_ctx_vt = kCtxVt;
    f.q = 0x48800001000;  // the field (same object in the routing handler and in 0x8B6060)
    f.q_is_field = true;
    f.owner_q = 1;
    f.live_gate = true;
    f.epoch = 3;
    f.x = 0x488A52C8100;  // X (session 8)
    f.key = kHost;
    f.slot_field = f.q;
    f.slot_epoch = 3;
    f.x_vt = f.want_vt = kVt;
    f.x_key = kHost;
    f.x_own = f.x;
    f.actor_live = true;
    f.x_ctx = 0x4880000;
    f.x_ctx_vt = kCtxVt;
    f.x_ready = 2;
    f.x_loading = 0;
    f.n1b0 = 22;
    f.n1c0 = 0;
    f.armed = true;
    f.skip_hook_ok = true;  // phase 2's skip hook at 0xAA4B27 in; X's +0x2A4/+0x288/+0x2A8/+0x2D4/+0x2CC idle
    return f;
}

// Phase 2 (p8-impl-escenas-play.md §13.3): 0xAA3E70(X) only with the skip hook in and X's fields that drive
// 0xAA5040 / 0xAA5400 idle; the tick and the 3 do not depend on either.
void test_live_gate() {
    const gsp::Facts f = ready_x();
    CHECK(gsp::live_fields_idle(f));
    gsp::Plan p = gsp::plan(f);
    CHECK(p.live && !p.live_held && !p.live_no_hook);
    gsp::Facts g = f;
    g.skip_hook_ok = false;
    p = gsp::plan(g);
    CHECK(!p.live && p.live_held && p.live_no_hook && p.tick && p.write3);
    g = f, g.x_2a4 = 1;  // 0xAA5040 would write 1.0f at [[World+0xA8]+0x208]
    p = gsp::plan(g);
    CHECK(!gsp::live_fields_idle(g) && !p.live && p.live_held && !p.live_no_hook && p.tick);
    g = f, g.x_288 = 0x4880000;  // the _blendRatio block
    CHECK(!gsp::plan(g).live && gsp::plan(g).live_held);
    g = f, g.x_2a8 = 0x4880000;  // the handle with its timer running (+0x2B0 >= 0): 0xAA5040 would work on it
    CHECK(!gsp::plan(g).live && gsp::plan(g).live_held);
    // Session 9 (p8-s9-npc.md §5): +0x2A8 is a live handle that stays; idle when its timer +0x2B0 < 0.
    g = f, g.x_2a8 = 0x471A8C166E0, g.x_2b0 = -0.5f;  // GL2's X: handle set, timer spent
    CHECK(gsp::live_fields_idle(g) && gsp::plan(g).live);
    g = f, g.x_2a8 = 0x471A8C166E0, g.x_2b0 = 0.0f;  // timer at 0: 0xAA5089 jb not taken -> still active
    CHECK(!gsp::live_fields_idle(g) && gsp::plan(g).live_held);
    g = f, g.x_2a8 = 0x471A8C166E0, g.x_2b0 = 2.0f;
    CHECK(!gsp::live_fields_idle(g));
    g = f, g.x_2a8 = 0x471A8C166E0, g.x_2b0 = std::numeric_limits<float>::quiet_NaN();  // NaN: not idle (safe side)
    CHECK(!gsp::live_fields_idle(g));
    g = f, g.x_2a8 = 0, g.x_2b0 = 5.0f;  // no handle: the timer alone does nothing
    CHECK(gsp::live_fields_idle(g));
    g = f, g.x_2a8 = 0x471A8C166E0, g.x_2b0 = -1.0f, g.x_2a4 = 1;  // the other fields still hold
    CHECK(!gsp::live_fields_idle(g));
    g = f, g.x_2d4 = 1;
    CHECK(!gsp::plan(g).live && gsp::plan(g).live_held);
    g = f, g.x_2cc = 0.5f;  // the _chapterTransition timer runs
    CHECK(!gsp::plan(g).live && gsp::plan(g).live_held);
    g = f, g.x_2cc = -1.0f;  // expired / idle
    CHECK(gsp::plan(g).live);
    g = f, g.x_2cc = 0.0f;
    CHECK(gsp::plan(g).live);
    g = f, g.x_2cc = std::numeric_limits<float>::quiet_NaN();  // review of 0383f70, 4: NaN is NOT idle
    CHECK(!gsp::live_fields_idle(g) && !gsp::plan(g).live && gsp::plan(g).live_held);
    g = f, g.x_2cc = -std::numeric_limits<float>::quiet_NaN();
    CHECK(!gsp::live_fields_idle(g));
    g = f, g.x_2cc = std::numeric_limits<float>::infinity();
    CHECK(!gsp::live_fields_idle(g));
    g = f, g.x_2cc = -std::numeric_limits<float>::infinity();
    CHECK(gsp::live_fields_idle(g));
    // Not due at all (live off, X not at 3, gate byte): not "held" either.
    g = f, g.live = false, g.x_2a4 = 1, g.skip_hook_ok = false;
    p = gsp::plan(g);
    CHECK(!p.live && !p.live_held);
    g = f, g.x_ready = 1, g.skip_hook_ok = false;
    CHECK(!gsp::plan(g).live && !gsp::plan(g).live_held);
    g = f, g.live_gate = false, g.x_2cc = 3.0f;
    CHECK(!gsp::plan(g).live && !gsp::plan(g).live_held);
    // probe decides the same way.
    gsp::Facts q = ready_x(gsp::Mode::Probe);
    q.x_288 = 1;
    CHECK(!gsp::plan(q).live && gsp::plan(q).live_held && gsp::plan(q).tick);
}

void test_effective_mode() {
    using M = gsp::Mode;
    CHECK(gsp::effective_mode(M::On, M::On) == M::On);
    CHECK(gsp::effective_mode(M::On, M::Probe) == M::Off);  // review 3: 1 refused without guest_seq_route=1
    CHECK(gsp::effective_mode(M::On, M::Off) == M::Off);
    CHECK(gsp::effective_mode(M::Probe, M::On) == M::Probe);
    CHECK(gsp::effective_mode(M::Probe, M::Probe) == M::Probe);
    CHECK(gsp::effective_mode(M::Probe, M::Off) == M::Off);
    CHECK(gsp::effective_mode(M::Off, M::On) == M::Off);
}

// Review of 0383f70, 5: a held gate is reported once per episode after the limit; idle starts a new episode.
void test_gate_watch() {
    gsp::GateWatch w;
    constexpr uint64_t kLimit = 10'000'000;
    CHECK(!w.update(false, 1'000, kLimit) && w.since_us == 0);
    CHECK(!w.update(true, 2'000'000, kLimit) && w.since_us == 2'000'000);
    CHECK(!w.update(true, 11'999'999, kLimit));  // 9.999999 s
    CHECK(w.update(true, 12'000'000, kLimit));   // 10 s: reported once
    CHECK(!w.update(true, 30'000'000, kLimit) && w.reported);
    CHECK(!w.update(false, 31'000'000, kLimit) && w.since_us == 0 && !w.reported);  // idle: a new episode
    CHECK(!w.update(true, 40'000'000, kLimit));
    CHECK(w.update(true, 50'000'000, kLimit));
    gsp::GateWatch z;  // a clock at 0 still starts the episode
    CHECK(!z.update(true, 0, kLimit) && z.since_us == 1 && z.update(true, kLimit + 1, kLimit));
}

void test_mode() {
    bool bad = true;
    CHECK(gsp::parse_mode("", &bad) == gsp::Mode::Off && !bad);
    CHECK(gsp::parse_mode("0", &bad) == gsp::Mode::Off && !bad);
    CHECK(gsp::parse_mode("probe", &bad) == gsp::Mode::Probe && !bad);
    CHECK(gsp::parse_mode("1", &bad) == gsp::Mode::On && !bad);
    CHECK(gsp::parse_mode("2", &bad) == gsp::Mode::Off && bad);
    CHECK(gsp::parse_mode("on", &bad) == gsp::Mode::Off && bad);
}

void test_plan_reasons() {
    const gsp::Facts f = ready_x();
    gsp::Plan p = gsp::plan(f);
    CHECK(p.skip == gsp::Skip::None && p.tick && p.write3 && p.live && !p.needs_owner);
    gsp::Facts g = f;
    g.mode = gsp::Mode::Off;
    CHECK(gsp::plan(g).skip == gsp::Skip::Off && !gsp::plan(g).tick);
    g = f, g.code_ok = false;
    CHECK(gsp::plan(g).skip == gsp::Skip::NotInstalled);
    g = f, g.field_mode = 2;  // the game does not tick M either (0xA953E4, 0x8B26A2)
    CHECK(gsp::plan(g).skip == gsp::Skip::FieldModeNoTick);
    g = f, g.field_mode = 1;
    CHECK(gsp::plan(g).skip == gsp::Skip::None);
    g = f, g.m = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::NoManager);
    g = f, g.m_ready = 2;  // this client's own load not ended (R2)
    CHECK(gsp::plan(g).skip == gsp::Skip::OwnNotReady);
    g = f, g.q_is_field = false;
    CHECK(gsp::plan(g).skip == gsp::Skip::NotField);
    g = f, g.x = 0;  // a key claimed, its X not written yet
    CHECK(gsp::plan(g).skip == gsp::Skip::EmptySlot);
    g = f, g.key = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::EmptySlot);
    g = f, g.x = g.m, g.x_own = g.m;
    CHECK(gsp::plan(g).skip == gsp::Skip::XIsM);
    g = f, g.x_vt = 0x1455C9000;  // destroyed (a base class's vtable) or another object
    CHECK(gsp::plan(g).skip == gsp::Skip::NotStageManager);
    g = f, g.want_vt = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::NotStageManager);
    g = f, g.x_key = 0xA0100002;
    CHECK(gsp::plan(g).skip == gsp::Skip::XKeyMismatch);
    g = f, g.x_key = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::XKeyMismatch);
    g = f, g.x_own = 0x488A52C9000;  // the character got another manager
    CHECK(gsp::plan(g).skip == gsp::Skip::XNotActorsOwn);
    // Review 1 / 10: an earlier connection, another field, the character gone from the actor table.
    g = f, g.slot_epoch = 2;
    CHECK(gsp::plan(g).skip == gsp::Skip::StaleEpoch && gsp::clears_slot(gsp::plan(g).skip));
    g = f, g.slot_field = 0x48800002000;
    CHECK(gsp::plan(g).skip == gsp::Skip::OtherField && gsp::clears_slot(gsp::plan(g).skip));
    g = f, g.actor_live = false;
    CHECK(gsp::plan(g).skip == gsp::Skip::ActorGone && !gsp::clears_slot(gsp::plan(g).skip));
    CHECK(!gsp::clears_slot(gsp::Skip::NotStageManager) && !gsp::clears_slot(gsp::Skip::None));
    g = f, g.slot_epoch = 2, g.x_vt = 0;  // the stale connection first: the slot goes even if X is dead
    CHECK(gsp::plan(g).skip == gsp::Skip::StaleEpoch);
    g = f, g.x_ctx = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::NoContext);
    g = f, g.x_ctx_vt = 0x145600008;
    CHECK(gsp::plan(g).skip == gsp::Skip::ContextClass);
    g = f, g.m_ctx_vt = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::ContextClass);
    g = f, g.owner_q = 0;  // 0x602940 would fault at 0x602997
    p = gsp::plan(g);
    CHECK(p.skip == gsp::Skip::OwnerMissionsNull && !p.tick && !p.write3);
    // The order: the first that fails is the reason.
    g = f, g.field_mode = 2, g.x_vt = 0, g.owner_q = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::FieldModeNoTick);
    g = f, g.m_ready = 1, g.x = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::OwnNotReady);
    g = f, g.x_key = 0, g.x_ctx = 0;
    CHECK(gsp::plan(g).skip == gsp::Skip::XKeyMismatch);
    for (size_t k = 0; k < static_cast<size_t>(gsp::Skip::Count); ++k) {
        CHECK(std::string_view(gsp::skip_name(static_cast<gsp::Skip>(k))) != "?");
    }
}

void test_owner_question() {
    // ON: X passes everything but vf40 was not asked yet this frame -> ask, then plan again.
    gsp::Facts f = ready_x();
    f.owner_q = -1;
    gsp::Plan p = gsp::plan(f);
    CHECK(p.needs_owner && p.skip == gsp::Skip::None && !p.tick && !p.write3 && !p.live);
    f.owner_q = 1;
    p = gsp::plan(f);
    CHECK(!p.needs_owner && p.tick);
    // Not asked when an earlier check fails (no call into the game for a dead X).
    f.owner_q = -1;
    f.x_vt = 0;
    CHECK(!gsp::plan(f).needs_owner && gsp::plan(f).skip == gsp::Skip::NotStageManager);
    // probe: never asked (no call at all), the decision is "would tick".
    gsp::Facts q = ready_x(gsp::Mode::Probe);
    q.owner_q = -1;
    p = gsp::plan(q);
    CHECK(!p.needs_owner && p.tick && p.write3 && p.live);
}

void test_write3() {
    const gsp::Facts f = ready_x();
    CHECK(gsp::write3_due(f));
    gsp::Facts g = f;
    g.x_ready = 1;  // before the copied 2322
    CHECK(!gsp::write3_due(g) && gsp::plan(g).tick && !gsp::plan(g).write3);
    g = f, g.x_ready = 3;  // already at 3
    CHECK(!gsp::write3_due(g) && gsp::plan(g).tick);
    g = f, g.x_loading = 17;  // SequencerLoadingTask in flight (0xA97366)
    CHECK(!gsp::write3_due(g) && gsp::plan(g).tick);
    g = f, g.m_ready = 3, g.n1b0 = 0, g.n1c0 = 0;  // nothing loaded yet
    CHECK(!gsp::write3_due(g));
    g = f, g.n1b0 = 0, g.n1c0 = 4;  // preloaded by earlier ticks in state 2
    CHECK(gsp::write3_due(g));
    g = f, g.armed = false;  // already written for this 2322
    CHECK(!gsp::write3_due(g) && gsp::plan(g).tick && !gsp::plan(g).write3);
    // M at 3 is checked by the plan before (OwnNotReady), and by write3_due too.
    g = f, g.m_ready = 2;
    CHECK(!gsp::write3_due(g));
}

void test_live() {
    gsp::Facts f = ready_x();
    CHECK(gsp::plan(f).live);  // X at 2 but written to 3 this frame
    f.live = false;
    CHECK(!gsp::plan(f).live && gsp::plan(f).tick);
    f = ready_x(), f.live_gate = false;  // byte [0x6CC0218] != 0: the game skips its own 0x8B2660 too
    CHECK(!gsp::plan(f).live && gsp::plan(f).tick);
    f = ready_x(), f.armed = false;  // at 2, not written: no live scene can exist
    CHECK(!gsp::plan(f).live && gsp::plan(f).tick);
    f = ready_x(), f.x_ready = 3, f.armed = false;
    CHECK(gsp::plan(f).live && !gsp::plan(f).write3);
    f = ready_x(), f.x_ready = 1;
    CHECK(!gsp::plan(f).live);
}

void test_mark_consumed() {
    gsp::WriteMark m;
    CHECK(m.armed(0x1000, 0, 2));  // never written
    CHECK(m.armed(0x1000, 1, 2));
    CHECK(!m.mark(0x1000, 1));  // written after the first routed 2322 (gen 1)
    CHECK(!m.armed(0x1000, 1, 3));  // X at 3: done
    CHECK(!m.armed(0x1000, 1, 1));
    CHECK(m.armed(0x1000, 2, 2));   // a second 2322 put X back to 2
    CHECK(m.armed(0x2000, 1, 2));   // another X for the character (re-created)
    // Review 4: X reads 2 again with the same gen (a native 2 after our 3 without a new routed 2322): due again,
    // at most kMaxRearms times.
    CHECK(m.armed(0x1000, 1, 2));
    CHECK(m.mark(0x1000, 1) && m.rearms == 1);
    CHECK(m.armed(0x1000, 1, 2) && m.mark(0x1000, 1) && m.rearms == 2);
    CHECK(m.armed(0x1000, 1, 2) && m.mark(0x1000, 1) && m.rearms == 3);
    CHECK(!m.armed(0x1000, 1, 2));  // capped
    CHECK(!m.mark(0x1000, 2) && m.rearms == 0);  // a new 2322: the count starts again
    CHECK(gsp::consumed(22, 21) == 1 && gsp::consumed(22, 22) == 0 && gsp::consumed(3, 5) == 0);
}

void test_table() {
    using N = gsp::XTable<4>::Noted;
    constexpr uint64_t kField = 0x48800001000, kField2 = 0x48800002000;
    gsp::XTable<4> t;
    CHECK(t.used() == 0 && t.size() == 4);
    CHECK(t.note(0, kHost, kField, 1, false) == N::Bad && t.note(0x1000, 0, kField, 1, false) == N::Bad && t.used() == 0);
    // The 2661s of the snapshot, then the 2322 (a gen unique in the table's life), then live 2661s (no change).
    CHECK(t.note(0x1000, kHost, kField, 1, false) == N::Added);
    CHECK(t.note(0x1000, kHost, kField, 1, false) == N::Updated);
    CHECK(t.used() == 1);
    auto v = t.get(0);
    CHECK(v.x == 0x1000 && v.key == kHost && v.gen == 0 && v.field == kField && v.epoch == 1);
    CHECK(t.note(0x1000, kHost, kField, 1, true) == N::Updated);
    v = t.get(0);
    const uint64_t g1 = v.gen;
    CHECK(g1 != 0);
    CHECK(t.knows(0x1000, kHost, 1) && !t.knows(0x1000, kHost, 2) && !t.knows(0x2000, kHost, 1) &&
          !t.knows(0x1000, 0xA0100002, 1));
    // The host's Kliff re-created: a new X for the same key replaces it in place, gen kept (no new 2322).
    CHECK(t.note(0x2000, kHost, kField, 1, false) == N::Updated);
    v = t.get(0);
    CHECK(v.x == 0x2000 && v.gen == g1 && t.used() == 1);
    CHECK(t.note(0x2000, kHost, kField, 1, true) == N::Updated && t.get(0).gen != g1 && t.get(0).gen != 0);
    // Another field: rebound by the next routed packet.
    CHECK(t.note(0x2000, kHost, kField2, 1, false) == N::Updated && t.get(0).field == kField2);
    // Other characters (a third player...), then full.
    CHECK(t.note(0x3000, 0xA0100002, kField, 1, false) == N::Added && t.note(0x4000, 0xA0100003, kField, 1, true) == N::Added &&
          t.note(0x5000, 0xA0100004, kField, 1, false) == N::Added);
    CHECK(t.used() == 4 && t.get(3).key == 0xA0100004 && t.get(2).gen != 0 && t.get(1).gen == 0);
    CHECK(t.note(0x6000, 0xA0100005, kField, 1, false) == N::Full && t.full() == 1);
    CHECK(t.note(0x2000, kHost, kField2, 1, false) == N::Updated);  // a known key still updates when full
    // A new connection: a full table reuses a slot of the earlier one; the slot starts again (gen 0).
    CHECK(t.note(0x6000, 0xA0100005, kField, 2, false) == N::Added && t.get(0).key == 0xA0100005 &&
          t.get(0).gen == 0 && t.get(0).epoch == 2);
    // The same key on the new connection: fresh (gen 0), even if its X address is the same.
    CHECK(t.note(0x4000, 0xA0100003, kField, 2, false) == N::Added && t.get(2).gen == 0);
    // clear: only if the slot still holds that key.
    CHECK(!t.clear(1, kHost) && t.clear(1, 0xA0100002) && t.get(1).key == 0 && t.cleared() == 1 && t.used() == 3);
    CHECK(t.note(0x7000, 0xA0100006, kField, 2, false) == N::Added && t.get(1).key == 0xA0100006);
    CHECK(t.get(9).key == 0 && t.get(9).x == 0 && !t.clear(9, kHost));
    CHECK(t.busy() == 0 && t.unstable() == 0);
}

// Every gen handed out is unique: a write mark of a slot that was cleared and added again never matches.
void test_gen_unique() {
    gsp::XTable<2> t;
    t.note(0x1000, kHost, 1, 1, true);
    const uint64_t a = t.get(0).gen;
    t.clear(0, kHost);
    t.note(0x1000, kHost, 1, 1, false);
    CHECK(t.get(0).gen == 0);
    t.note(0x1000, kHost, 1, 1, true);
    const uint64_t b = t.get(0).gen;
    CHECK(a != 0 && b != 0 && a != b);
    gsp::WriteMark m;
    m.mark(0x1000, a);
    CHECK(m.armed(0x1000, b, 2));
}

void test_table_threads() {
    // Two routing threads (in the game the packet handlers run on one; each character's packets on one) and a
    // reader at once: every key in exactly one slot, its X, its 2322 count.
    // Writers serialize on the try-lock (a busy one gives up and is counted); the reader never sees a torn slot.
    using N = gsp::XTable<8>::Noted;
    gsp::XTable<8> t;
    std::atomic<uint64_t> done{0}, busy{0};
    auto writer = [&](uint32_t first, int keys) {
        for (int i = 0; i < 4000; ++i) {
            const uint32_t k = first + static_cast<uint32_t>(i % keys);
            const N r = t.note(0x1000 + (k - 0xA0100001) * 0x10, k, 0x9000 + (k - 0xA0100001), 1, (i % 50) == 0);
            (r == N::Busy ? busy : done).fetch_add(1);
        }
    };
    std::thread a(writer, 0xA0100001u, 4);
    std::thread b(writer, 0xA0100003u, 4);  // two keys shared with a: the same key from two threads at once
    std::thread r([&] {
        for (int i = 0; i < 4000; ++i) {
            for (size_t s = 0; s < 8; ++s) {
                const auto v = t.get(s);
                if (!v.key) continue;
                CHECK(v.x == 0x1000 + (v.key - 0xA0100001) * 0x10 && v.field == 0x9000 + (v.key - 0xA0100001) &&
                      v.epoch == 1);
            }
        }
    });
    a.join();
    b.join();
    r.join();
    CHECK(done.load() + busy.load() == 8000 && busy.load() == t.busy());
    uint32_t seen = 0;
    size_t used = 0;
    for (size_t s = 0; s < 8; ++s) {
        const auto v = t.get(s);
        if (!v.key) continue;
        ++used;
        CHECK(!(seen & (1u << (v.key - 0xA0100001))));  // never two slots for one key
        seen |= 1u << (v.key - 0xA0100001);
    }
    CHECK(seen == 0x3F && used == 6 && t.full() == 0);
}

void test_g5_types() {
    gsr::NpcState a, b;
    CHECK(a == b);
    b.flag_3d8 = 1;
    CHECK(!(a == b));
    b = a, b.parent = 0xB0100F35, b.parent_found = true, b.parent_flag = 1;
    CHECK(!(a == b));
    gsr::XState x, y;
    CHECK(x == y);
    y.live = 1;
    CHECK(!(x == y));
    y = x, y.engage = 2;
    CHECK(!(x == y));
}

}  // namespace

int main() {
    test_mode();
    test_effective_mode();
    test_gen_unique();
    test_live_gate();
    test_gate_watch();
    test_plan_reasons();
    test_owner_question();
    test_write3();
    test_live();
    test_mark_consumed();
    test_table();
    test_table_threads();
    test_g5_types();
    if (g_failures) {
        std::printf("optd_seq_play: %d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_seq_play: all passed\n");
    return 0;
}
