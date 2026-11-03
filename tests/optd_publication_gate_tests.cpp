// S35 fase A (docs/research/s25/s35-fase-a.md): the pure admission rule of publication_gate
// (publication_gate_logic.h). No game, no mod: synthetic memory for the CC chain's three states.
// The end-to-end exits (Timeout, Native, family, full tables) are in optd_s3_tests.cpp (child_hold's seams).
#include <cstdio>
#include <cstring>
#include <vector>

#include "mod/session2/observer_hold_acks_logic.h"
#include "mod/session2/publication_gate_logic.h"

using namespace cdmp::session2::pubg;

namespace {
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

// A block of memory with its own base address; reads outside it fail (an unmapped page).
struct Mem {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x4000, 0);
    uintptr_t base() const { return reinterpret_cast<uintptr_t>(bytes.data()); }
    template <class T> void put(uintptr_t at, T v) { std::memcpy(bytes.data() + (at - base()), &v, sizeof v); }
    bool read(uintptr_t at, void* out, size_t n) const {
        if (at < base() || at + n > base() + bytes.size()) return false;
        std::memcpy(out, bytes.data() + (at - base()), n);
        return true;
    }
};
// actor at +0x100, components at +0x400, CC at +0x800, restore state at +0xC00.
struct Chain {
    Mem m;
    uintptr_t actor, comps, cc, state;
    Chain() : actor(m.base() + 0x100), comps(m.base() + 0x400), cc(m.base() + 0x800), state(m.base() + 0xC00) {
        m.put<uintptr_t>(actor + 0x68, comps);
        m.put<uintptr_t>(comps + 0x40, cc);
        m.put<uintptr_t>(cc + 0x08, actor);
        m.put<uintptr_t>(cc + 0x40, state);
        m.put<uint8_t>(state + 0x30, 0);
    }
    CcRead read() const {
        return read_cc([this](uintptr_t a, void* o, size_t n) { return m.read(a, o, n); }, actor);
    }
};

void cc_three_states() {
    Chain c;
    CcRead r = c.read();
    CHECK(r.state == Cc::Present && r.restore == 0);
    c.m.put<uint8_t>(c.state + 0x30, 1);
    r = c.read();
    CHECK(r.state == Cc::Present && r.restore == 1);
    // Proven absent: the components are read and the CC slot reads exactly 0.
    c.m.put<uintptr_t>(c.comps + 0x40, 0);
    r = c.read();
    CHECK(r.state == Cc::Absent && r.slot == 0);
    // Could not read it: NEVER absent (child_hold.cpp:62-69 used to return nullopt for both).
    c.m.put<uintptr_t>(c.comps + 0x40, 0x1234);  // an implausible pointer
    CHECK(c.read().state == Cc::Unknown);
    c.m.put<uintptr_t>(c.comps + 0x40, c.cc);
    c.m.put<uintptr_t>(c.cc + 0x08, c.actor + 8);  // the slot's +8 is not this actor (another component)
    CHECK(c.read().state == Cc::Unknown);
    c.m.put<uintptr_t>(c.cc + 0x08, c.actor);
    c.m.put<uintptr_t>(c.cc + 0x40, 0);  // no restore state
    CHECK(c.read().state == Cc::Unknown);
    c.m.put<uintptr_t>(c.cc + 0x40, c.m.base() + 0x100000);  // restore state outside the mapped block
    CHECK(c.read().state == Cc::Unknown);
    c.m.put<uintptr_t>(c.cc + 0x40, c.state);
    c.m.put<uintptr_t>(c.actor + 0x68, c.m.base() + 0x200000);  // components unreadable
    CHECK(c.read().state == Cc::Unknown);
    c.m.put<uintptr_t>(c.actor + 0x68, 0);  // no components pointer at all: unknown, not absent
    CHECK(c.read().state == Cc::Unknown);
    CHECK(read_cc([](uintptr_t, void*, size_t) { return false; }, 0).state == Cc::Unknown);
}

Facts present(uint8_t restore, uint32_t controller = 0x90100000, uint32_t observer = 0x9010052B) {
    Facts f;
    f.actor = uintptr_t{0x10000000};
    f.controller = controller;
    f.observer_key = observer;
    f.cc.state = Cc::Present;
    f.cc.restore = restore;
    return f;
}

void admission_matrix() {
    // The S34 exposures: a host-controlled NPC with restore 0 to Shadow, whatever the exit (Timeout, Native, family).
    CHECK(admit(present(0)) == Verdict::Restore0);
    CHECK(admit(present(1)) == Verdict::Publish);
    // Its controller always gets it (its own client builds the state), restore 0 or not.
    CHECK(admit(present(0, 0x9010052B, 0x9010052B)) == Verdict::Publish);
    // Indeterminate reads are never published.
    Facts f = present(1);
    f.actor.reset();
    CHECK(admit(f) == Verdict::ActorUnknown);
    f = present(1);
    f.controller.reset();
    CHECK(admit(f) == Verdict::ControllerUnknown);
    f = present(1);
    f.cc.state = Cc::Unknown;
    CHECK(admit(f) == Verdict::CcUnknown);
    f.observer_key = 0;  // unknown observer key: never taken for the controller
    f.cc.state = Cc::Present;
    f.cc.restore = 0;
    CHECK(admit(f) == Verdict::Restore0);
    // Gone (not in the SAM): nothing to publish, not retained.
    f = present(1);
    f.actor = uintptr_t{0};
    CHECK(admit(f) == Verdict::Gone && !retained(admit(f)));
    // No CC proven: published only with its dependency admissible.
    f = present(0);
    f.cc.state = Cc::Absent;
    CHECK(admit(f) == Verdict::Publish);
    for (Dep d : {Dep::Withheld, Dep::Unsafe, Dep::Unknown}) {
        f.dep = d;
        CHECK(admit(f) == Verdict::Dependency && retained(admit(f)));
    }
    f.dep = Dep::InBatch;
    CHECK(admit(f) == Verdict::ParentInBatch);
    f.dep = Dep::Ok;
    CHECK(admit(f) == Verdict::Publish);
    // A complete actor whose parent is withheld from that observer waits for it (no orphan by the gate).
    f = present(1);
    f.dep = Dep::Withheld;
    CHECK(admit(f) == Verdict::Dependency);
    // Parents first: the child of a parent refused in this batch never goes before it to a non-controller.
    f = present(1);
    f.dep = Dep::InBatch;
    CHECK(admit(f) == Verdict::ParentInBatch);
    // Review 9, one rule: the controller ALWAYS receives its actor, whatever its parent or its state.
    f = present(0, 0x9010052B, 0x9010052B);
    for (Dep d : {Dep::InBatch, Dep::Withheld, Dep::Unsafe, Dep::Unknown}) {
        f.dep = d;
        CHECK(admit(f) == Verdict::Publish);
    }
    // Review 4: an actor no user controls (CtrlNotUser) follows prepare's native rule: its own state is not judged,
    // its dependency still is.
    f = present(0, 0, 0x9010052B);
    f.host_key = 0x90100000;
    f.user2_key = 0x9010052B;
    f.cc.state = Cc::Unknown;
    CHECK(admit(f) == Verdict::Publish);
    f.dep = Dep::Withheld;
    CHECK(admit(f) == Verdict::Dependency);
    f = present(0, 0x90100777, 0x9010052B);  // a user nobody knows
    f.host_key = 0x90100000;
    f.user2_key = 0x9010052B;
    CHECK(admit(f) == Verdict::Publish);
    f.controller = 0x90100000u;  // the host's: judged
    CHECK(admit(f) == Verdict::Restore0);
    CHECK(user_controlled(5, 0, 0) && user_controlled(0, 0, 0));  // users unknown: fail closed
    CHECK(!user_controlled(0, 1, 2) && user_controlled(2, 1, 2));
}

void parent_dependency() {
    ParentFacts p;
    const uint32_t obs = 0x9010052B;
    CHECK(parent_dep(true, 0, p, obs) == Dep::None);
    CHECK(parent_dep(false, 0, p, obs) == Dep::Unknown);  // the link itself unreadable
    p.actor.reset();
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Unknown);
    p.actor = uintptr_t{0};
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Ok);  // gone: the client's orphan route
    // S34 C: B0100578 (no restore of its own) queries B01004BF's layer through the hierarchy; B01004BF host-
    // controlled with restore 0: the piece is NOT admissible for Shadow while its parent is so.
    p.actor = uintptr_t{0x20000000};
    p.controller = 0x90100000u;
    p.cc.state = Cc::Present;
    p.cc.restore = 0;
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Unsafe);
    p.cc.restore = 1;
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Ok);
    p.cc.state = Cc::Unknown;
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Unknown);
    p.cc.state = Cc::Absent;
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Ok);
    p.controller = obs;  // the observer controls the parent: its own state
    p.cc.state = Cc::Present;
    p.cc.restore = 0;
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Ok);
    p.controller.reset();
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Unknown);
    p.controller = 0x90100000u;
    p.cc.restore = 1;
    p.withheld = true;  // present at the server and complete, but withheld from that observer
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::Withheld);
    p.in_batch = true;
    CHECK(parent_dep(true, 0xB01004BF, p, obs) == Dep::InBatch);
    // Review 4: a parent controlled by no user (the server's, 0) is published natively by the gate itself: Ok as a
    // dependency even with its CC unreadable (gimmick pieces stay visible).
    ParentFacts srv;
    srv.actor = uintptr_t{0x30000000};
    srv.controller = 0u;
    srv.cc.state = Cc::Unknown;
    CHECK(parent_dep(true, 0xB0100700, srv, obs, 0x90100000, obs) == Dep::Ok);
    CHECK(parent_dep(true, 0xB0100700, srv, obs) == Dep::Unknown);  // users unknown: fail closed
    srv.controller = 0x90100000u;  // the host's, CC unreadable: still unknown
    CHECK(parent_dep(true, 0xB0100700, srv, obs, 0x90100000, obs) == Dep::Unknown);
}
// Review 5: an old Ack. A retention longer than observer_hold_acks' max_hold_ms (100 s) never delivers the kept
// packets late: they are discarded (the client never had the key), and the later 2005 releases nothing stale.
void old_ack_beyond_max_hold() {
    cdmp::session2::oha::AckHold h;
    const uint32_t key = 0xB01002A0;
    std::vector<std::vector<uint8_t>> spill;
    CHECK(h.begin(key, 1, 0) == cdmp::session2::oha::Begin::Started);
    std::vector<uint8_t> p = {0x7B, 0x0B, 0, 4, 0, 0xA0, 0x02, 0x10, 0xB0};  // 2991 for the key
    CHECK(h.offer(2991, key, p, 10, spill) == cdmp::session2::oha::Offer::Held && spill.empty());
    std::vector<cdmp::session2::oha::AckHold::Gone> gone;
    CHECK(h.expire(h.limits().max_hold_ms - 1, &gone) == 0);
    CHECK(h.expire(h.limits().max_hold_ms, &gone) == 1 && gone[0].why == cdmp::session2::oha::Discard::Expired);
    CHECK(h.announce(std::vector<uint32_t>{key}, h.limits().max_hold_ms + 60000).empty());  // the late 2005
    // While the gate still retains it (no hold any more), its next packets pass: the client drops an unknown key.
    std::vector<uint8_t> q = p;
    CHECK(h.offer(2991, key, q, h.limits().max_hold_ms + 1, spill) == cdmp::session2::oha::Offer::Pass);
}

void bounded_retention() {
    // Retained until the bound, then dropped WITHOUT publishing; never published by expiry.
    CHECK(retention(0, 1000, 60000, true) == Retention::Retain);  // first refusal
    CHECK(retention(1000, 60999, 60000, true) == Retention::Retain);
    CHECK(retention(1000, 61000, 60000, true) == Retention::DropExpired);
    CHECK(retention(1000, 2000, 60000, false) == Retention::DropNoRoom);
    CHECK(retention(1000, 61000, 60000, false) == Retention::DropExpired);
    CHECK(retention(5000, 1000, 60000, true) == Retention::Retain);  // a clock that went back: no early drop
    CHECK(withhold_without_entry(Mode::On) && !withhold_without_entry(Mode::Probe) &&
          !withhold_without_entry(Mode::Off));
    for (uint8_t v = 0; v < static_cast<uint8_t>(Verdict::Count); ++v) {
        CHECK(verdict_name(static_cast<Verdict>(v))[0] != '?');
    }
}
}  // namespace

int main() {
    cc_three_states();
    admission_matrix();
    parent_dependency();
    bounded_retention();
    old_ack_beyond_max_hold();
    std::printf("S35 publication gate: %d failures\n", failures);
    return failures ? 1 : 0;
}
