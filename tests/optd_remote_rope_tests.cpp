// P8 session 25, the other player's grappling-hook rope (docs/research/s25/gancho-cuerda.md): the pure logic of
// remote_rope (src/mod/session2/remote_rope_logic.h): the modes, the decisions at RemoteCatchComponent_Apply's entry
// (F1, F3) and after the server's release (F2) with S25's real numbers, the 2471 payload, the transform math, the
// counters and the self-consistency of the verified build-2976 bytes.

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

#include "common/troc_frame.h"
#include "common/troc_script.h"
#include "mod/session2/remote_rope_logic.h"

using namespace cdmp::session2;
using rrp::ApplyDecision;
using rrp::ApplyInputs;
using rrp::Mode;
using rrp::Resolve;
using rrp::Send;
using rrp::Stale;
using rrp::Target;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// S25: the tree 0x090000DE6690FC82 of level 0x0100FE0F, as the 2763 carries it (+0x0E..+0x1D).
constexpr uint8_t kTreeFC82[16] = {0x82, 0xFC, 0x90, 0x66, 0xDE, 0x00, 0x00, 0x09,
                                   0x0F, 0xFE, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
constexpr uint64_t kGuestHandle = 0x800000000100069Full;  // the guest's body handle of that tree (G1 138.618)
constexpr uint64_t kHostHandle = 0x8000000001000C2Cull;   // the host's (its own throw, H 366.675)

// A 2763 as the apply sees it, everything readable: tree target, slot 0 empty.
ApplyInputs tree(uint8_t ctl, uint64_t handle) {
    ApplyInputs in;
    in.ctl_ok = in.slot_ok = in.slot_type_ok = in.kind_ok = in.target_ok = true;
    in.ctl = ctl;
    in.slot = 0;
    in.slot_type = rrp::kSlotEmpty;
    in.kind = 0;
    in.cls = 2;
    in.target_actor = 0;
    std::memcpy(in.scene, kTreeFC82, 16);
    in.handle = handle;
    // When the slot is busy, what it still holds: a tree (S25's stale slots).
    in.stale_cls_ok = in.stale_actor_ok = true;
    in.stale_cls = 2;
    in.stale_actor = 0;
    return in;
}

void test_modes() {
    CHECK(rrp::parse_mode("0").mode == Mode::Off);
    CHECK(rrp::parse_mode("probe").mode == Mode::Probe);
    CHECK(rrp::parse_mode("1").mode == Mode::On);
    CHECK(rrp::parse_mode(" probe ; S26 step 1").mode == Mode::Probe);
    CHECK(!rrp::parse_mode("2").known);
    CHECK(rrp::parse_mode("2").mode == Mode::Off);
}

// The S25 episodes (gancho-cuerda.md §1.3) seen on the host's client.
void test_apply_s25() {
    // H 310.014: the guest's rope; here the guest's character is remote, this PC maps the tree to its own handle.
    ApplyInputs in = tree(0, kGuestHandle);
    CHECK(rrp::wants_resolve(in));
    in.resolve = Resolve::Ok;
    in.local_handle = kHostHandle;
    ApplyDecision d = rrp::decide_apply(Mode::Probe, in);
    CHECK(d.target == Target::RemoteTranslate);
    CHECK(!d.write);  // probe: nothing changes
    CHECK(d.stale == Stale::None);
    CHECK(!d.release);
    d = rrp::decide_apply(Mode::On, in);
    CHECK(d.target == Target::RemoteTranslate);
    CHECK(d.write);
    CHECK(d.value == kHostHandle);  // this PC's handle, never the packet's
    // H 388.489: the guest's next rope while its slot still holds the first one (never released here): stale.
    in.slot_type = 0;
    d = rrp::decide_apply(Mode::Probe, in);
    CHECK(d.stale == Stale::WouldRelease);
    CHECK(!d.release);
    d = rrp::decide_apply(Mode::On, in);
    CHECK(d.stale == Stale::Release);
    CHECK(d.release);
    CHECK(d.write);  // F1 still applies to the new rope
    // H 366.675: the host's own rope: the check (the mapping must give the packet's handle), never written, and a
    // busy own slot is the native code's business.
    ApplyInputs own = tree(1, kHostHandle);
    own.resolve = Resolve::Ok;
    own.local_handle = kHostHandle;
    own.slot_type = 0;
    for (Mode m : {Mode::Probe, Mode::On}) {
        d = rrp::decide_apply(m, own);
        CHECK(d.target == Target::OwnMatch);
        CHECK(!d.write);
        CHECK(d.stale == Stale::None);
        CHECK(!d.release);
    }
    own.local_handle = kGuestHandle;
    CHECK(rrp::decide_apply(Mode::On, own).target == Target::OwnMismatch);
    CHECK(!rrp::decide_apply(Mode::On, own).write);
    own.resolve = Resolve::NotFound;
    CHECK(rrp::decide_apply(Mode::On, own).target == Target::OwnUnresolved);
    // H 2127.204 (G2 492.185): class 2, no scene id, handle 0x8000000002000F02: nothing to map, untouched.
    ApplyInputs g2 = tree(0, 0x8000000002000F02ull);
    std::memset(g2.scene, 0, 16);
    CHECK(!rrp::wants_resolve(g2));
    d = rrp::decide_apply(Mode::On, g2);
    CHECK(d.target == Target::NotTree);
    CHECK(!d.write);
}

void test_apply_pass_through() {
    // Remote thrower, the scene object not loaded here: passed through, counted.
    ApplyInputs in = tree(0, kGuestHandle);
    in.resolve = Resolve::NotFound;
    ApplyDecision d = rrp::decide_apply(Mode::On, in);
    CHECK(d.target == Target::RemoteUnresolved);
    CHECK(!d.write);
    for (Resolve r : {Resolve::NoManager, Resolve::NoBody, Resolve::NoHandle, Resolve::BadCode, Resolve::Faulted}) {
        in.resolve = r;
        CHECK(rrp::decide_apply(Mode::On, in).target == Target::RemoteUnresolved);
        CHECK(!rrp::decide_apply(Mode::On, in).write);
    }
    // Ok but -1: unresolved too.
    in.resolve = Resolve::Ok;
    in.local_handle = rrp::kNoHandle;
    CHECK(rrp::decide_apply(Mode::On, in).target == Target::RemoteUnresolved);
    // The same handle here (by chance): nothing written.
    in.local_handle = kGuestHandle;
    d = rrp::decide_apply(Mode::On, in);
    CHECK(d.target == Target::RemoteSame);
    CHECK(!d.write);
    // Not a tree: another kind, another class, no handle.
    ApplyInputs k = tree(0, kGuestHandle);
    k.kind = 4;
    CHECK(rrp::decide_apply(Mode::On, k).target == Target::NotTree);
    ApplyInputs c = tree(0, kGuestHandle);
    c.cls = 0;
    CHECK(rrp::decide_apply(Mode::On, c).target == Target::NotTree);
    ApplyInputs h = tree(0, rrp::kNoHandle);
    CHECK(rrp::decide_apply(Mode::On, h).target == Target::NotTree);
    // A not-a-tree 2763 of a remote thrower still gets its stale TREE slot released (the stale slot's own target
    // decides, not the new packet's).
    k.slot_type = 0;
    CHECK(rrp::decide_apply(Mode::On, k).release);
    // Unreadable control byte: nothing at all.
    ApplyInputs u = tree(0, kGuestHandle);
    u.ctl_ok = false;
    u.slot_type = 0;
    d = rrp::decide_apply(Mode::On, u);
    CHECK(d.target == Target::Unreadable);
    CHECK(d.stale == Stale::Unreadable);
    CHECK(!d.release);
    CHECK(!d.write);
    // Unreadable slot type / a slot out of range: no release.
    ApplyInputs s = tree(0, kGuestHandle);
    s.slot_type_ok = false;
    CHECK(!rrp::decide_apply(Mode::On, s).release);
    s = tree(0, kGuestHandle);
    s.slot = 2;
    s.slot_type = 0;
    CHECK(!rrp::decide_apply(Mode::On, s).release);
    // Off never writes nor releases.
    ApplyInputs o = tree(0, kGuestHandle);
    o.resolve = Resolve::Ok;
    o.local_handle = kHostHandle;
    o.slot_type = 0;
    d = rrp::decide_apply(Mode::Off, o);
    CHECK(!d.write);
    CHECK(!d.release);
}

// The review's scenario: user 2 hooked enemy E (the host's), the host hooked E too, user 2 released (no 2471 from the
// server) and throws again: on the host user 2's stale slot holds an ACTOR target. 0x5A7BC0 -> 0x1807C80 would zero
// E's holder key without checking it is user 2's (the host's own live rope): logged only, never released.
void test_stale_scope() {
    ApplyInputs in = tree(0, kGuestHandle);
    in.resolve = Resolve::Ok;
    in.local_handle = kHostHandle;
    in.slot_type = 1;  // the stale slot's kind: an actor catch
    in.stale_cls = 0;
    in.stale_actor = 0xB0203203;
    for (Mode m : {Mode::Probe, Mode::On}) {
        const ApplyDecision d = rrp::decide_apply(m, in);
        CHECK(d.stale == Stale::OutOfScope);
        CHECK(!d.release);
    }
    // Kind 0 but an actor target, or class 2 with an actor: still not the measured case.
    in.slot_type = 0;
    CHECK(rrp::decide_apply(Mode::On, in).stale == Stale::OutOfScope);
    in.stale_cls = 2;
    CHECK(rrp::decide_apply(Mode::On, in).stale == Stale::OutOfScope);
    // The stored target unreadable: not released.
    in.stale_actor = 0;
    in.stale_cls_ok = false;
    CHECK(rrp::decide_apply(Mode::On, in).stale == Stale::OutOfScope);
    CHECK(!rrp::decide_apply(Mode::On, in).release);
    in.stale_cls_ok = true;
    in.stale_actor_ok = false;
    CHECK(!rrp::decide_apply(Mode::On, in).release);
    // The measured case: released (1), logged (probe).
    in.stale_actor_ok = true;
    CHECK(rrp::stale_is_tree(in));
    CHECK(rrp::decide_apply(Mode::On, in).release);
    CHECK(rrp::decide_apply(Mode::Probe, in).stale == Stale::WouldRelease);
}

// The own-throw check stops after kOwnChecks matches in a row: own throws are no longer computed; remote ones still are.
void test_own_check_done() {
    ApplyInputs own = tree(1, kHostHandle);
    own.own_check_done = true;
    CHECK(!rrp::wants_resolve(own));
    CHECK(rrp::tree_target(own));
    const ApplyDecision d = rrp::decide_apply(Mode::On, own);
    CHECK(d.target == Target::OwnSkipped);
    CHECK(!d.write);
    ApplyInputs remote = tree(0, kGuestHandle);
    remote.own_check_done = true;
    CHECK(rrp::wants_resolve(remote));
    static_assert(rrp::kOwnChecks == 20);
}

void test_release() {
    rrp::ReleaseInputs in;
    in.pre_ok = in.err_ok = true;
    in.slot = 0;
    in.reason = 2;
    in.kind = 0;
    in.cls = 2;
    in.target_actor = 0;
    in.err = 0;
    // A tree released (D20295AD f3): the server leaves it without a 2471.
    CHECK(rrp::decide_release(Mode::Probe, in) == Send::WouldSend);
    CHECK(rrp::decide_release(Mode::On, in) == Send::Sent);
    rrp::ReleaseInputs a = in;
    a.target_actor = 0xB0203C38;  // an actor target: the native 0x2903950 path
    CHECK(rrp::decide_release(Mode::On, a) == Send::ActorTarget);
    a = in;
    a.err = 0x6CF70A4;
    CHECK(rrp::decide_release(Mode::On, a) == Send::NativeError);
    a = in;
    a.kind = rrp::kSlotEmpty;
    CHECK(rrp::decide_release(Mode::On, a) == Send::SlotEmpty);
    a = in;
    a.kind = 4;
    CHECK(rrp::decide_release(Mode::On, a) == Send::OutOfScope);
    a = in;
    a.cls = 1;
    CHECK(rrp::decide_release(Mode::On, a) == Send::OutOfScope);
    a = in;
    a.slot = 0xFF;
    CHECK(rrp::decide_release(Mode::On, a) == Send::NoSlot);
    a = in;
    a.pre_ok = false;
    CHECK(rrp::decide_release(Mode::On, a) == Send::Unreadable);
    a = in;
    a.err_ok = false;
    CHECK(rrp::decide_release(Mode::On, a) == Send::Unreadable);
}

// F2 at the launch (the host's run 10-12 19:11: catch #5 at t=158.75, launch 1F4EF471 at t=167.62, the release frame
// events EB340761 f0/f1 at t=168.22 and 170.98 found the server slot empty, err 0xD661CF29).
void test_action_release() {
    rrp::ActionSlot s;
    s.before_ok = s.after_ok = true;
    s.kind = 0;
    s.cls = 2;
    s.target_actor = 0;
    s.after_kind = rrp::kSlotEmpty;
    // The tree freed by the action change: the native code sent nothing.
    CHECK(rrp::decide_action_release(Mode::Probe, s) == rrp::ActRelease::WouldSend);
    CHECK(rrp::decide_action_release(Mode::On, s) == rrp::ActRelease::Sent);
    CHECK(rrp::act_logged(rrp::ActRelease::Sent));
    CHECK(rrp::act_logged(rrp::ActRelease::WouldSend));
    rrp::ActionSlot k = s;
    k.after_kind = 0;  // 563271FE every 1.5 s keeps it
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::Kept);
    CHECK(!rrp::act_logged(rrp::ActRelease::Kept));
    k = s;
    k.kind = rrp::kSlotEmpty;  // nothing hooked
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::NotHeld);
    k = s;
    k.target_actor = 0xB0100791;  // an NPC: the target's vf148 (0x2903950) sent the native 2471
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::ActorTarget);
    CHECK(!rrp::act_logged(rrp::ActRelease::ActorTarget));
    k = s;
    k.cls = 1;
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::OutOfScope);
    k = s;
    k.kind = 4;
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::OutOfScope);
    k = s;
    k.after_ok = false;
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::Unreadable);
    k = s;
    k.before_ok = false;
    CHECK(rrp::decide_action_release(Mode::On, k) == rrp::ActRelease::Unreadable);
    static_assert(rrp::kActReason == rrp::kStaleReason);
}

// One line per launch: the release events after it are folded until the next 2763 applied.
void test_launch_fold() {
    rrp::LaunchFold f;
    const uint32_t host = 0xA0100001, guest = 0xA0101FCD;
    CHECK(f.get(host, 0) == rrp::LaunchFold::kUnknown);
    // Catch #1 released by its frame event (D20295AD f3): nothing folded.
    f.on_apply(host, 0);
    f.on_release(host, 0);
    CHECK(!f.on_empty_release(host, 0));  // an empty release with no launch: logged once ...
    CHECK(f.on_empty_release(host, 0));   // ... the next ones folded
    // Catch #5, the launch, EB340761 f0/f1 twice: all folded into the launch's line.
    f.on_apply(host, 0);
    CHECK(f.get(host, 0) == rrp::LaunchFold::kBusy);
    f.on_action_free(host, 0);
    CHECK(f.on_empty_release(host, 0));
    CHECK(f.on_empty_release(host, 0));
    // Another thrower and the other slot are their own.
    CHECK(!f.on_empty_release(guest, 0));
    CHECK(!f.on_empty_release(host, 1));
    // A new catch: the next empty release is logged again.
    f.on_apply(host, 0);
    CHECK(!f.on_empty_release(host, 0));
    // No slot 0xFF entry.
    CHECK(!f.on_empty_release(host, 0xFF));
    CHECK(!f.on_empty_release(host, 0xFF));
    // Eight more throwers push the oldest out (it starts again from 'unknown').
    for (uint32_t i = 0; i < 8; ++i) f.on_apply(0xB0100000 + i, 0);
    CHECK(f.get(host, 0) == rrp::LaunchFold::kUnknown);
}

void test_flight() {
    // 0x5A12A0 sets state 2 with the flight; 3 is attached; 1 is right after the apply (the 0.000 s of 10-12 19:11).
    CHECK(!rrp::flight_ready(1));
    CHECK(rrp::flight_ready(2));
    CHECK(rrp::flight_ready(3));
    CHECK(!rrp::flight_ready(5));
    CHECK(!rrp::flight_ready(7));
    static_assert(rrp::kFlightWaitMs >= 1000);
}

void test_payload() {
    // A 2471 as 0x29688C0 writes it: u32 key, u8 slot, u8 reason; framed as the handler sees it (total = 5 + 6).
    uint8_t buf[11] = {0xA7, 0x09, 0xFF, 0x06, 0x00, 0x4E, 0x03, 0x10, 0xA0, 0x00, 0x02};
    rrp::Payload2471 p;
    CHECK(rrp::parse_2471(11, buf, sizeof buf, p));
    CHECK(p.key == 0xA010034Eu);
    CHECK(p.slot == 0);
    CHECK(p.reason == 2);
    CHECK(!rrp::parse_2471(12, buf, sizeof buf, p));  // total and length disagree
    buf[3] = 7;
    CHECK(!rrp::parse_2471(12, buf, sizeof buf, p));  // not 6 bytes
    CHECK(!rrp::parse_2471(11, buf, 10, p));          // short buffer
    static_assert(rrp::kOp2471 == 0x09A7);
}

void test_math() {
    const rrp::Vec3 p{-0.0196f, 8.80f, -0.10f};
    rrp::Xf id;
    rrp::Vec3 w = rrp::world_point(id, p);
    CHECK(approx(w.x, p.x) && approx(w.y, p.y) && approx(w.z, p.z));
    // The world-space case (…084E / F883 in the guest): identity body, the point IS the world point.
    const rrp::Vec3 world{-10683.52f, 618.46f, -3895.78f};
    w = rrp::world_point(id, world);
    CHECK(approx(w.x, world.x, 1e-2f) && approx(w.z, world.z, 1e-2f));
    // Translation only.
    rrp::Xf t;
    t.trans[0] = -10747.0f;
    t.trans[1] = 606.0f;
    t.trans[2] = -3950.0f;
    w = rrp::world_point(t, p);
    CHECK(approx(w.x, -10747.0196f, 1e-2f) && approx(w.y, 614.80f, 1e-2f) && approx(w.z, -3950.10f, 1e-2f));
    // 90 degrees about Y: (1,0,0) -> (0,0,-1).
    rrp::Xf r;
    const float s = std::sqrt(0.5f);
    r.quat[1] = s;
    r.quat[3] = s;
    w = rrp::world_point(r, {1, 0, 0});
    CHECK(approx(w.x, 0, 1e-5f) && approx(w.y, 0, 1e-5f) && approx(w.z, -1, 1e-5f));
    // Scale before the rotation.
    r.scale[0] = 2;
    w = rrp::world_point(r, {1, 0, 0});
    CHECK(approx(w.z, -2, 1e-5f));
    CHECK(approx(rrp::distance({0, 0, 0}, {3, 4, 0}), 5));
    // Packing.
    const uint64_t v = rrp::pack2(-10683.52f, 45.0f);
    CHECK(rrp::unpack_lo(v) == -10683.52f);
    CHECK(rrp::unpack_hi(v) == 45.0f);
    CHECK(rrp::opt8(false, 7) == 0);
    CHECK(rrp::opt8_ok(rrp::opt8(true, 0)));
    CHECK(rrp::opt8_val(rrp::opt8(true, 6)) == 6);
    // Scene ids: the natives look at the first three dwords only.
    CHECK(rrp::scene_nonzero(kTreeFC82));
    uint8_t zero[16] = {};
    CHECK(!rrp::scene_nonzero(zero));
    zero[12] = 1;
    CHECK(!rrp::scene_nonzero(zero));
    zero[8] = 1;
    CHECK(rrp::scene_nonzero(zero));
}

void test_tally() {
    rrp::Tally t;
    ApplyInputs in = tree(0, kGuestHandle);
    in.resolve = Resolve::Ok;
    in.local_handle = kHostHandle;
    in.slot_type = 0;
    ApplyDecision d = rrp::decide_apply(Mode::On, in);
    t.note_apply(in, d, true, true);
    ApplyInputs own = tree(1, kHostHandle);
    own.resolve = Resolve::Ok;
    own.local_handle = kHostHandle;
    t.note_apply(own, rrp::decide_apply(Mode::On, own), false, false);
    const auto n = [](const std::atomic<uint64_t>& c) { return c.load(); };
    CHECK(n(t.applies) == 2);
    CHECK(n(t.unexpected_caller) == 1);
    CHECK(n(t.remote) == 1);
    CHECK(n(t.own) == 1);
    CHECK(n(t.remote_translate) == 1);
    CHECK(n(t.translated) == 1);
    CHECK(n(t.write_failed) == 0);
    CHECK(n(t.own_match) == 1);
    CHECK(n(t.stale) == 1);
}

// The verified bytes say what the constants say.
void test_constants() {
    // F1 / F3 hook: the entry, its only caller and the return address.
    CHECK(rrp::call_targets(rrp::kApplyCall, rrp::kApplyCallBytes, rrp::kApply));
    static_assert(rrp::kApplyReturn == rrp::kApplyCall + 5);
    CHECK(rrp::kApplyBoundaries[std::size(rrp::kApplyBoundaries) - 1] == sizeof rrp::kApplyHead);
    CHECK(rrp::kApplyBoundaries[0] >= 5);
    // The target block: [rbp+0x5F] with rbp = entry rsp - 0x2F (push rbp, lea rbp,[rsp-0x27]) is argument 6;
    // +4 actor, +8 scene, +0x18 handle.
    const uint8_t* ic = rrp::kInfoCopyBytes;
    CHECK(ic[3] == 0x5F);
    CHECK((ic[3] - 0x2F) / 8 == rrp::kArgTarget);
    CHECK(ic[14] == rrp::kInfo_Actor);
    CHECK(ic[24] == rrp::kInfo_Scene);
    CHECK(ic[33] == rrp::kInfo_Handle);
    // The mapping site: WorldSystem, +0xC8, +0x80, the lookup call, obj = [att+8], +0x58, +0x15, +0xE0, vf70, the
    // attacher's ScopeAttacherBase vtable.
    const uint8_t* m = rrp::kMapSiteBytes;
    CHECK(rrp::rip_target(rrp::kMapSite, m, 3, 7) == rrp::kWorldSystem);
    CHECK(m[10] == rrp::kWs_SceneRoot);
    CHECK(m[28] == rrp::kSceneRoot_Mgr);
    CHECK(rrp::call_targets(0x5947E7, m + (0x5947E7 - rrp::kMapSite), rrp::kResolve));
    CHECK(m[0x5947ED - rrp::kMapSite + 3] == 0x60 + rrp::kAtt_Obj);  // mov rdx,[rbp+0x68], the attacher at [rbp+0x60]
    CHECK(m[0x5947DC - rrp::kMapSite + 3] == 0x60);
    CHECK(m[0x5947F1 - rrp::kMapSite + 3] == rrp::kObj_Body);
    CHECK(m[0x5947FA - rrp::kMapSite + 2] == rrp::kBody_Gone);
    CHECK(m[0x594814 - rrp::kMapSite + 3] == rrp::kBody_Phys);
    CHECK(m[0x594823 - rrp::kMapSite + 2] == rrp::kPhys_vfHandle);
    CHECK(rrp::rip_target(0x594840, m + (0x594840 - rrp::kMapSite), 3, 7) == rrp::kScopeAttacherBaseVtbl);
    static_assert(sizeof rrp::kMapSiteBytes == 0x59484F - rrp::kMapSite);
    // The release request's main-thread check, and the 2471 handler's call it mirrors.
    CHECK(rrp::rip_target(rrp::kReleaseThreadCheck, rrp::kReleaseThreadCheckBytes, 2, 6) == rrp::kMainThreadId);
    const uint8_t* r = rrp::k2471ReleaseBytes;
    CHECK(r[3] == rrp::kActor_Comps);
    CHECK(r[24] == rrp::kComps_RemoteCatch);
    CHECK(r[8] == 1);  // its fifth argument
    CHECK(rrp::call_targets(0xB5BF5D, r + (0xB5BF5D - rrp::k2471Release), rrp::kRelease));
    CHECK(rrp::k2471HandlerBoundaries[std::size(rrp::k2471HandlerBoundaries) - 1] == sizeof rrp::k2471HandlerHead);
    // F2: the server hook's argument reads ([rbp+0x7F] with rbp = entry rsp - 0x57 is argument 5; [r8+8]; [r14+8]).
    const uint8_t* a = rrp::kSrvArgsBytes;
    CHECK(a[8] == 0x7F);
    CHECK((a[8] - 0x57) / 8 == 5);
    CHECK(a[15] == rrp::kEv_Data);
    CHECK(a[20] == rrp::kData_Slot);
    CHECK(rrp::kSrvReleaseBoundaries[std::size(rrp::kSrvReleaseBoundaries) - 1] == sizeof rrp::kSrvReleaseHead);
    // 0x2903950's call to the writer: the ToNear vtable, the finder's flag and actor, the call.
    const uint8_t* w = rrp::kWriterSiteBytes;
    CHECK(rrp::rip_target(0x2903A8A, w + (0x2903A8A - rrp::kWriterSite), 3, 7) == rrp::kToNearVtbl);
    CHECK(w[0x2903A85 - rrp::kWriterSite + 3] - 0x20 == static_cast<int>(rrp::kFinder_Flag));
    CHECK(w[0x2903A96 - rrp::kWriterSite + 4] - 0x20 == static_cast<int>(rrp::kFinder_Actor));
    CHECK(rrp::call_targets(0x2903AAD, w + (0x2903AAD - rrp::kWriterSite), rrp::kWriter));
    static_assert(rrp::kFinderSize >= rrp::kFinder_Actor + 8);
    // Slot addressing (0x1808100: slot * 0x58 + 0x20; 0x5A12A0: slot * 0x1A0 + 0x9BC / +0xA94).
    static_assert(rrp::slot_target(0x1000, 1) == 0x1000 + 0x20 + 0x58);
    static_assert(rrp::slot_state(0x1000, 1) == 0x1000 + 0x9BC + 0x1A0);
    static_assert(rrp::slot_flight(0x1000, 0) == 0x1000 + 0xA94);
    static_assert(rrp::kSlot_Handle == 0x40 - rrp::kSlotBase);  // 0x1808100: +0x40 <- struct +0x20 (the handle)
    // F2 at the launch: vf140 0x1807390, its vtable slot, the dispatch's call on [[actor+0x68]+0x78], the keep flag,
    // the target's vf148 with reason 2 and the thrower, its own vf158 with (slot, 2, 0, 1).
    CHECK(rrp::kActReleaseBoundaries[std::size(rrp::kActReleaseBoundaries) - 1] == sizeof rrp::kActReleaseHead);
    static_assert(rrp::kActReleaseSlot == rrp::kServerRemoteCatchVtbl + 0x140);
    static_assert(rrp::kSrvCompVf158Slot == rrp::kServerRemoteCatchVtbl + 0x158);
    CHECK(rrp::call_targets(rrp::kActClientCall, rrp::kActClientCallBytes, rrp::kActRelease));
    // The writer's pre-check reads what ToNear::vf1 and 0x2BC4F70 read: [[finder+0x10]+0x68]+0x1A0, [ts+8]->vfE8.
    const uint8_t* tn = rrp::kToNearVf1Bytes;
    CHECK(tn[3] == rrp::kFinder_Actor);
    CHECK(tn[7] == rrp::kActor_Comps);
    CHECK(tn[11] == (rrp::kComps_TransformSync & 0xFF) && tn[12] == (rrp::kComps_TransformSync >> 8));
    const uint8_t* to = rrp::kTsOwnerCallBytes;
    CHECK(to[3] == rrp::kTs_Owner);
    CHECK(to[9] == rrp::kOwnerVf[0]);
    const uint8_t* dp = rrp::kActDispatchBytes;
    CHECK(dp[7] == rrp::kActor_Comps);
    CHECK(dp[11] == rrp::kComps_RemoteCatch);
    CHECK(dp[24] == 0xFF && dp[25] == 0x90 && dp[26] == 0x40 && dp[27] == 0x01);
    const uint8_t* kt = rrp::kActKeepTestBytes;
    CHECK(kt[4] == 9 && kt[7] == 4);
    const uint8_t* nt = rrp::kActNotifyBytes;
    CHECK(nt[3] == rrp::kComps_RemoteCatch);
    CHECK(nt[9] == rrp::kActReason);
    CHECK(nt[13] == rrp::kComp_Owner);  // mov r8,[rsi+8]: the thrower
    CHECK(nt[20] == 0x48 && nt[21] == 0x01);
    const uint8_t* sr = rrp::kActSelfReleaseBytes;
    CHECK(sr[7] == 1);                // its fifth argument
    CHECK(sr[13] == rrp::kActReason);
    CHECK(sr[23] == 0x58 && sr[24] == 0x01);
}

// The replay scripts cut from S25's G1 (docs/research/data/grapple_s25_g1_*.troc, make_grapple_script.py) against a fake
// server, start to goodbye: every Req of the character carries the learned PLAYER_KEY, the 636-byte DoFrameEventReq
// (variant 6) keeps the tree's scene id and Shadow's own handle, and the release frame event D20295AD f3 is there.
using Bytes = std::vector<uint8_t>;

void script_flow(const char* file, size_t want_catches, size_t want_releases, const uint8_t (&scene)[16],
                 uint64_t handle) {
    std::ifstream f(std::filesystem::path(std::string(CDMP_SOURCE_DIR)) / "docs/research/data" / file);
    CHECK(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    auto s = cdmp::troc::parse_script(ss.str(), &err);
    CHECK(s.has_value());
    if (!s) {
        std::printf("  %s: %s\n", file, err.c_str());
        return;
    }
    cdmp::troc::ScriptPlayer p(*s);
    p.set_var("SAVE_SLOT", 108);
    const Bytes user_ack = {0x4B, 0x0A, 0x10, 0x90, 0, 0, 0, 0};
    const Bytes player_ack = {0x4C, 0x0A, 0x10, 0xA0, 0, 0, 0, 0};  // user 2's character: 0xA0100A4C
    const uint32_t key = 0xA0100A4C;
    std::vector<Bytes> sent;
    int lc = 0;
    bool forced = false;
    for (uint32_t t = 0; t < 400000 && p.state() == cdmp::troc::ScriptPlayer::State::Running; t += 5) {
        for (auto& o : p.tick(t)) {
            auto v = cdmp::troc::parse_wire(o, cdmp::troc::Dir::ToHostServer);
            CHECK(v.has_value());
            if (!v) continue;
            sent.push_back(o);
            switch (v->opcode) {
                case 2319: p.on_server_packet(cdmp::troc::make(2840, -1, Bytes(4, 0)), t); break;
                case 2490:
                    p.on_server_packet(cdmp::troc::make(2646, -1, user_ack), t);
                    p.on_server_packet(cdmp::troc::make(2869, -1, Bytes(13, 0x7F)), t);
                    p.on_server_packet(cdmp::troc::make(2716, -1, {}), t);
                    break;
                case 3093:
                    p.on_server_packet(cdmp::troc::make(2756, -1, player_ack), t);
                    p.on_server_packet(cdmp::troc::make(2174, -1, Bytes(65, 0)), t);
                    break;
                case 2437: p.on_server_packet(cdmp::troc::make(2174, -1, Bytes(65, 0)), t); break;
                case 2549: p.on_server_packet(cdmp::troc::make(2144, 0, {}), t); break;
                case 2445:
                    p.on_server_packet(cdmp::troc::make(2762, 0, {}), t);
                    ++lc;
                    break;
                case 2223:
                    // The 19:04 run: the server forced an action at the first replayed StartActionReq
                    // (ChangeActionSessionIDAck 2954 {u32 actor, u32 user, u64 session}); the script must echo it
                    // (a session above 32 bits here: the echo carries all 8 bytes).
                    if (lc >= 2 && !forced) {
                        forced = true;
                        p.on_server_packet(cdmp::troc::make(2954, -1, Bytes{0x4C, 0x0A, 0x10, 0xA0, 0x4B, 0x0A, 0x10, 0x90,
                                                                           0x02, 0, 0, 0, 0x01, 0, 0, 0}),
                                           t);
                    }
                    break;
                case 2001:
                    p.on_server_packet(Bytes{0xD3, 0x07, 0xFF, 0x04, 0x00, 0x00, 0x00}, t);
                    p.on_server_packet(Bytes{0xD2, 0x07, 0xFF, 0x04, 0x00, 0x00, 0x00}, t);
                    break;
                default: break;
            }
        }
    }
    CHECK(p.state() == cdmp::troc::ScriptPlayer::State::Done);
    if (p.state() != cdmp::troc::ScriptPlayer::State::Done) std::printf("  %s: %s\n", file, p.describe().c_str());
    size_t catches = 0, releases = 0, keyed = 0, foreign = 0, echoes = 0;
    uint8_t hb[8];
    std::memcpy(hb, &handle, 8);
    for (const Bytes& o : sent) {
        const auto v = cdmp::troc::parse(o);
        if (!v) continue;
        const auto& pl = v->payload;
        uint32_t k = 0;
        if ((v->opcode == 2223 || v->opcode == 2189 || v->opcode == 2556 || v->opcode == 2828) && pl.size() >= 4) {
            std::memcpy(&k, pl.data(), 4);
            (k == key ? keyed : foreign)++;
        }
        if (v->opcode == 2350) {
            const Bytes want = {0x4C, 0x0A, 0x10, 0xA0, 0x02, 0, 0, 0, 0x01, 0, 0, 0};
            CHECK(Bytes(pl.begin(), pl.end()) == want);  // EchoActionSessionIDReq {u32 actor, u64 session}
            ++echoes;
        }
        if (v->opcode == 2380 && pl.size() > 0x203) {
            std::memcpy(&k, pl.data() + 1, 4);
            (k == key ? keyed : foreign)++;
            uint32_t hash = 0, frame = 0;
            std::memcpy(&hash, pl.data() + 0x1F9, 4);
            std::memcpy(&frame, pl.data() + 0x1FF, 4);
            if (pl.size() == 636 && pl[0x203] == 6) {
                if (catches++ == 0) {
                    CHECK(std::memcmp(pl.data() + 0x20B, scene, 16) == 0);  // the first tree's scene id
                    CHECK(std::memcmp(pl.data() + 0x21B, hb, 8) == 0);      // Shadow's handle, as captured
                }
            }
            if (hash == 0xD20295AD && frame == 3) ++releases;
        }
    }
    CHECK(forced);
    CHECK(echoes == 1);
    CHECK(catches == want_catches);
    CHECK(releases == want_releases);
    CHECK(keyed > 0);
    CHECK(foreign == 0);
    if (!sent.empty()) {
        const auto last = cdmp::troc::parse_wire(sent.back(), cdmp::troc::Dir::ToHostServer);
        CHECK(last && last->opcode == 2001);
    }
}

void test_scripts() {
    constexpr uint8_t kTree3AD9[16] = {0xD9, 0x3A, 0x55, 0x2C, 0xDA, 0x00, 0x00, 0x09,
                                       0x0F, 0xFE, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    constexpr uint8_t kTreeF883[16] = {0x83, 0xF8, 0x90, 0x66, 0xDE, 0x00, 0x00, 0x09,
                                       0x0F, 0xFE, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    script_flow("grapple_s25_g1_138.troc", 1, 1, kTreeFC82, kGuestHandle);
    // The walk from slot 108's start (the S25 guest's own start) to the tree, then two ropes (138 on FC82, 217 on 3AD9
    // = handle 0x8000000001000AD5), each released with D20295AD f3: the second one is the 'slot still busy' failure on
    // an unfixed observer.
    script_flow("grapple_s25_g1_walk_138_217.troc", 2, 2, kTreeFC82, kGuestHandle);
    (void)kTree3AD9;
    script_flow("grapple_s25_g1_981.troc", 1, 1, kTreeF883, 0x800000000100084Eull);
}

}  // namespace

int main() {
    test_modes();
    test_apply_s25();
    test_apply_pass_through();
    test_stale_scope();
    test_own_check_done();
    test_release();
    test_action_release();
    test_launch_fold();
    test_flight();
    test_payload();
    test_math();
    test_tally();
    test_constants();
    test_scripts();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_remote_rope: all passed\n");
    return 0;
}
