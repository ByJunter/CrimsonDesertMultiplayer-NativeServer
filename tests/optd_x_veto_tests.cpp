// P8 session 9, guest_x_veto (docs/research/pendientes/p8-s9-cinematica.md §8, review §9).
//
// SCOPE: offline logic only (guest_x_veto_logic.h):
//  - the keys' values (stage / camera, V0 always count-only, guest_local_after_remote);
//  - gxv::judge: whose stage manager runs a stage chart function, every reason in order (not remote, no manager,
//    not a stage manager, the current M by identity or by its live owner, the own character pinned at login, X in
//    the table, nothing known, owner not a character, X by key) with the session 9 values;
//  - gxv::act per mode, the camera key's '1' acting as probe;
//  - guest_x_ui_veto: gxv::fn_info / fn_mode per handler and key (only the six read-whole ways out act with 1),
//    gxv::who for the per-handler X / M counters, gxv::stage_name;
//  - guest_x_ui_veto_allow: gxv::parse_allow, fn_mode / fn_held_as_probe / fn_act with a handler allowed (session 11:
//    SetInteraction runs natively for X, add and remove alike, while InputBlock / UIControl... stay vetoed), fn_dir;
//  - the X context (gxv::ContextSlot): set / deeper only / clear point at the same rsp / a frame above / a routing
//    hook's entry / the SEH filter (clear) / staleness / nested RAII scopes (exchange) / a clear point that never ran;
//  - B1: gxv::LocalLoadWatch with the session 9 timeline (3093 at 749.0, state 0x0C, no 2549) and a normal load.
// They do NOT test the hooks, the game's code nor the process end.

#include <cstdint>
#include <cstdio>
#include <string_view>

#include "mod/session2/guest_x_veto_logic.h"

using namespace cdmp::session2;
using gxv::Act;
using gxv::Mode;
using gxv::Verdict;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

constexpr uint64_t kVt = 0x1455C9720;         // exe + pa::ClientSequencerStageManager
constexpr uint32_t kHost = 0xA0100001;        // the host's Kliff (session 9)
constexpr uint32_t kOwn = 0xA0100377;         // the guest's Kliff (session 9)
constexpr uint64_t kX = 0x37E00FF7500;        // X (session 9 guest log)
constexpr uint64_t kM = 0x37E4ABF1180;        // M (session 9 guest log)

// Session 9, 15:26:07.608: the 2981 of 0x213 state 2 routed to X ran Loading for the host's Kliff's manager.
gxv::Facts camp_cutscene() {
    gxv::Facts f;
    f.remote = true;
    f.g = kX;
    f.g_vt = f.want_vt = kVt;
    f.g_key = kHost;
    f.m = kM;
    f.m_key = kOwn;
    f.own_key = kOwn;
    f.in_table = true;
    return f;
}

void test_modes() {
    bool bad = true;
    CHECK(gxv::parse_mode("", &bad) == Mode::Off && !bad);
    CHECK(gxv::parse_mode("0", &bad) == Mode::Off && !bad);
    CHECK(gxv::parse_mode("probe", &bad) == Mode::Probe && !bad);
    CHECK(gxv::parse_mode("1", &bad) == Mode::On && !bad);
    CHECK(gxv::parse_mode("yes", &bad) == Mode::Off && bad);
    CHECK(gxv::parse_mode("2", &bad) == Mode::Off && bad);
    CHECK(gxv::camera_effective(Mode::On) == Mode::Probe);
    CHECK(gxv::camera_effective(Mode::Probe) == Mode::Probe);
    CHECK(gxv::camera_effective(Mode::Off) == Mode::Off);
    // Review 2: V0 never skips, whatever the stage key.
    CHECK(gxv::v0_effective(Mode::On) == Mode::Probe);
    CHECK(gxv::v0_effective(Mode::Probe) == Mode::Probe);
    CHECK(gxv::v0_effective(Mode::Off) == Mode::Off);
    CHECK(gxv::act(gxv::v0_effective(Mode::On), Verdict::Veto) == Act::Would);
}

void test_judge() {
    gxv::Facts f = camp_cutscene();
    CHECK(gxv::judge(f) == Verdict::Veto);

    // Order of the reasons.
    f = camp_cutscene();
    f.remote = false;
    CHECK(gxv::judge(f) == Verdict::NotRemote);  // single player / local connection: never
    f = camp_cutscene();
    f.g = 0;
    CHECK(gxv::judge(f) == Verdict::NoManager);
    f = camp_cutscene();
    f.g_vt = 0x145600000;
    CHECK(gxv::judge(f) == Verdict::NotStageManager);
    f = camp_cutscene();
    f.want_vt = 0;
    f.g_vt = 0;
    CHECK(gxv::judge(f) == Verdict::NotStageManager);  // an unset vtable never matches

    // Review 4: the current M by identity (whatever its owner reads), even if it were in the table.
    f = camp_cutscene();
    f.g = kM;
    f.g_key = 0;
    CHECK(gxv::judge(f) == Verdict::OwnManager);
    f.g_key = kHost;  // a torn / odd owner read: still M
    CHECK(gxv::judge(f) == Verdict::OwnManager);
    // M's live owner (a new M after a reload: identity differs, owner the same).
    f = camp_cutscene();
    f.g = 0x37E86FCA800;
    f.g_key = kOwn;
    f.own_key = 0;  // the login-pinned key not known: the live owner is enough
    CHECK(gxv::judge(f) == Verdict::OwnManager);
    // M unknown (guest_seq_play off, nothing fresh): the own character pinned at login.
    f = camp_cutscene();
    f.m = 0;
    f.m_key = 0;
    f.g = kM;
    f.g_key = kOwn;
    CHECK(gxv::judge(f) == Verdict::OwnManager);
    // A reconnection with another own character: M's live owner wins over a stale login key.
    f = camp_cutscene();
    f.own_key = 0xA0100300;
    f.g = 0x37E86FCA800;
    f.g_key = kOwn;
    CHECK(gxv::judge(f) == Verdict::OwnManager);

    // Nothing known about the own side: only a routed X (in the table) is vetoed.
    f = camp_cutscene();
    f.m = 0;
    f.m_key = 0;
    f.own_key = 0;
    CHECK(gxv::judge(f) == Verdict::Veto);
    f.in_table = false;
    CHECK(gxv::judge(f) == Verdict::UnknownOwner);
    // Only M's live owner known: X by key.
    f.m_key = kOwn;
    CHECK(gxv::judge(f) == Verdict::Veto);

    // Not in the table (evicted / another connection): another character's manager by key.
    f = camp_cutscene();
    f.in_table = false;
    CHECK(gxv::judge(f) == Verdict::Veto);
    f.g_key = 0xB0101DF3;  // an NPC
    CHECK(gxv::judge(f) == Verdict::NotCharacter);
    f.g_key = 0;  // unreadable owner
    CHECK(gxv::judge(f) == Verdict::NotCharacter);
    f.in_table = true;  // a table hit needs a key
    CHECK(gxv::judge(f) == Verdict::NotCharacter);
}

void test_act() {
    CHECK(gxv::act(Mode::On, Verdict::Veto) == Act::Veto);
    CHECK(gxv::act(Mode::Probe, Verdict::Veto) == Act::Would);
    CHECK(gxv::act(Mode::Off, Verdict::Veto) == Act::None);
    for (uint8_t v = 0; v < static_cast<uint8_t>(Verdict::Count); ++v) {
        const auto verdict = static_cast<Verdict>(v);
        if (verdict == Verdict::Veto) continue;
        CHECK(gxv::act(Mode::On, verdict) == Act::None);
        CHECK(gxv::act(Mode::Probe, verdict) == Act::None);
        CHECK(gxv::verdict_name(verdict)[0] != '?');
    }
}

// guest_x_ui_veto (p8-s10.md section 4): which handlers act with each key, the per-handler counters, the stage name.
void test_fn() {
    using gxv::Fn;
    // Every handler has a name; exactly the six whose native way out was read whole are vetoable.
    size_t vetoable = 0, dialog = 0, ui = 0, camera = 0;
    for (size_t i = 0; i < gxv::kFns; ++i) {
        const gxv::FnInfo info = gxv::fn_info(static_cast<Fn>(i));
        CHECK(info.name[0] != '?');
        vetoable += info.vetoable ? 1 : 0;
        dialog += info.family == gxv::Family::Dialog ? 1 : 0;
        ui += info.family == gxv::Family::Ui ? 1 : 0;
        camera += info.family == gxv::Family::Camera ? 1 : 0;
        CHECK(gxv::family_name(info.family)[0] != '?');
    }
    CHECK(gxv::kFns == 28 && dialog == 5 && ui == 10 && camera == 13);
    CHECK(vetoable == 6);
    for (Fn f : {Fn::TimelineEvent, Fn::SetInteraction, Fn::InputBlock, Fn::UIControl, Fn::UIShowMinimap,
                 Fn::UIStageGuide}) {
        CHECK(gxv::fn_info(f).vetoable && gxv::fn_info(f).family != gxv::Family::Camera);
    }
    // .xtls thunks and handlers without a way out are never vetoable.
    for (Fn f : {Fn::SetQuestDialogAICategory, Fn::TimelineEventEnd, Fn::BlockInteraction, Fn::UIStageIcon,
                 Fn::UIHint, Fn::UIHintEnd, Fn::ChangePhasePanelTag, Fn::UIStageBackgroundGuide, Fn::UIFindPath,
                 Fn::SequencerCamera, Fn::FadeInOut}) {
        CHECK(!gxv::fn_info(f).vetoable);
    }

    // guest_x_ui_veto=1: vetoable -> 1, the rest probe (held), camera included.
    CHECK(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off) == Mode::On);
    CHECK(gxv::fn_mode(Fn::BlockInteraction, Mode::On, Mode::Off) == Mode::Probe);
    CHECK(gxv::fn_held_as_probe(Fn::BlockInteraction, Mode::On));
    CHECK(!gxv::fn_held_as_probe(Fn::SetInteraction, Mode::On));
    CHECK(!gxv::fn_held_as_probe(Fn::BlockInteraction, Mode::Probe));
    CHECK(gxv::fn_mode(Fn::SequencerCamera, Mode::On, Mode::Probe) == Mode::Probe);
    // probe: everything counted, nothing acts.
    CHECK(gxv::fn_mode(Fn::UIControl, Mode::Probe, Mode::Off) == Mode::Probe);
    CHECK(gxv::act(gxv::fn_mode(Fn::UIControl, Mode::Probe, Mode::Off), Verdict::Veto) == Act::Would);
    // ui off: the camera key alone covers only the camera family, and never above probe (session 9 behaviour).
    CHECK(gxv::fn_mode(Fn::LetterBox, Mode::Off, Mode::On) == Mode::Probe);
    CHECK(gxv::fn_mode(Fn::LetterBox, Mode::Off, Mode::Probe) == Mode::Probe);
    CHECK(gxv::fn_mode(Fn::LetterBox, Mode::Off, Mode::Off) == Mode::Off);
    CHECK(gxv::fn_mode(Fn::InputBlock, Mode::Off, Mode::On) == Mode::Off);
    CHECK(gxv::fn_mode(Fn::TimelineEvent, Mode::Off, Mode::On) == Mode::Off);
    for (size_t i = 0; i < gxv::kFns; ++i) {
        const auto f = static_cast<Fn>(i);
        CHECK(gxv::fn_mode(f, Mode::Off, Mode::Off) == Mode::Off);
        // Only a vetoable handler ever reaches On, and only with guest_x_ui_veto=1.
        for (Mode u : {Mode::Off, Mode::Probe, Mode::On}) {
            for (Mode c : {Mode::Off, Mode::Probe, Mode::On}) {
                if (gxv::fn_mode(f, u, c) == Mode::On) CHECK(u == Mode::On && gxv::fn_info(f).vetoable);
            }
        }
    }

    // The counters: X is the veto's target, the own M never is.
    CHECK(gxv::who(Verdict::Veto) == gxv::Who::X);
    CHECK(gxv::who(Verdict::OwnManager) == gxv::Who::M);
    for (uint8_t v = 0; v < static_cast<uint8_t>(Verdict::Count); ++v) {
        const auto verdict = static_cast<Verdict>(v);
        if (verdict != Verdict::Veto && verdict != Verdict::OwnManager) CHECK(gxv::who(verdict) == gxv::Who::Other);
    }
    // The host's shop talk of session 10 (sequencer 0x197 in X, branch 6): a vetoable handler acts on X only.
    gxv::Facts f = camp_cutscene();
    CHECK(gxv::act(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off), gxv::judge(f)) == Act::Veto);
    f.g = kM;
    CHECK(gxv::act(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off), gxv::judge(f)) == Act::None);

    // The stage name read raw: printable up to the NUL, '?' for the rest, bounded by the buffer.
    char out[32];
    const uint64_t name[3] = {0x6C6C6542656B6152ull /* "RakeBell" */, 0x00000000000041ull, 0};
    CHECK(gxv::stage_name(name, 3, out, sizeof out) == 9 && std::string_view(out) == "RakeBellA");
    const uint64_t odd[3] = {0x0000000000094142ull, 0, 0};  // "BA	"
    CHECK(gxv::stage_name(odd, 3, out, sizeof out) == 3 && std::string_view(out) == "BA?");
    const uint64_t full[3] = {0x4141414141414141ull, 0x4141414141414141ull, 0x4141414141414141ull};
    CHECK(gxv::stage_name(full, 3, out, sizeof out) == 24);
    CHECK(gxv::stage_name(full, 3, out, 5) == 4 && std::string_view(out) == "AAAA");
    CHECK(gxv::stage_name(full, 0, out, sizeof out) == 0 && out[0] == 0);
}

// guest_x_ui_veto_allow (p8-impl-veto-interaccion.md): SetInteraction runs natively for X by default, both
// directions; the other vetoable handlers keep acting with 1.
void test_allow() {
    using gxv::Fn;
    // Parsing: the default, case, separators, the "nothing" spellings, unknown names, names with parentheses.
    gxv::AllowList a = gxv::parse_allow(gxv::kDefaultAllow);
    CHECK(a.set == gxv::fn_bit(Fn::SetInteraction) && a.bad == 0);
    a = gxv::parse_allow("setinteraction ,BlockInteraction;; TimelineEvent(vf9)\tUIHint(+0x250)");
    CHECK(a.bad == 0);
    CHECK(a.set == (gxv::fn_bit(Fn::SetInteraction) | gxv::fn_bit(Fn::BlockInteraction) |
                    gxv::fn_bit(Fn::TimelineEventEnd) | gxv::fn_bit(Fn::UIHintEnd)));
    for (std::string_view none : {"", "0", "none", "NONE", " , ;"}) {
        a = gxv::parse_allow(none);
        CHECK(a.set == 0 && a.bad == 0);
    }
    a = gxv::parse_allow("SetInteraction, Shop, InputBlok");
    CHECK(a.set == gxv::fn_bit(Fn::SetInteraction) && a.bad == 2 && a.first_bad == "Shop");
    a = gxv::parse_allow("SetInteractio");  // a prefix is not a name
    CHECK(a.set == 0 && a.bad == 1);
    // Every handler's own name parses to itself alone.
    for (size_t i = 0; i < gxv::kFns; ++i) {
        const auto f = static_cast<Fn>(i);
        a = gxv::parse_allow(gxv::fn_info(f).name);
        CHECK(a.set == gxv::fn_bit(f) && a.bad == 0);
    }

    // The mode: an allowed vetoable handler never reaches On (so its way out is never needed), and is not "held".
    CHECK(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off, true) == Mode::Probe);
    CHECK(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off, false) == Mode::On);
    CHECK(!gxv::fn_held_as_probe(Fn::SetInteraction, Mode::On, true));
    CHECK(!gxv::fn_held_as_probe(Fn::BlockInteraction, Mode::On, true));
    CHECK(gxv::fn_held_as_probe(Fn::BlockInteraction, Mode::On, false));
    CHECK(gxv::fn_mode(Fn::SetInteraction, Mode::Off, Mode::On, true) == Mode::Off);  // ui off: not even hooked
    for (size_t i = 0; i < gxv::kFns; ++i) {
        const auto f = static_cast<Fn>(i);
        for (Mode u : {Mode::Off, Mode::Probe, Mode::On}) {
            for (Mode c : {Mode::Off, Mode::Probe, Mode::On}) {
                CHECK(gxv::fn_mode(f, u, c, true) != Mode::On);
                // Allowing never hooks more or less: the same handlers are hooked (mode != Off) either way.
                CHECK((gxv::fn_mode(f, u, c, true) == Mode::Off) == (gxv::fn_mode(f, u, c, false) == Mode::Off));
            }
        }
    }

    // Session 11: the guest's X (the host's Kliff's manager) registers Renee's and Delkin's interactions (add) and
    // removes them later (remove). With the default list both run; InputBlock / UIControl of the same scene stay vetoed.
    const gxv::FnSet def = gxv::parse_allow(gxv::kDefaultAllow).set;
    const gxv::Facts x = camp_cutscene();
    const Verdict vx = gxv::judge(x);
    CHECK(vx == Verdict::Veto);
    for (Fn f : {Fn::SetInteraction, Fn::TimelineEvent, Fn::InputBlock, Fn::UIControl, Fn::UIShowMinimap,
                 Fn::UIStageGuide}) {
        const bool allowed = gxv::fn_in(def, f);
        const Act act = gxv::fn_act(gxv::fn_mode(f, Mode::On, Mode::Probe, allowed), allowed, vx);
        CHECK(act == (f == Fn::SetInteraction ? Act::Allow : Act::Veto));
    }
    // Both directions get the same decision (the hook sees r8b = 1 add / 0 remove; the verdict never reads it).
    for (uint64_t r8 : {0x1ull, 0x0ull, 0xFFFFFF01ull, 0xFFFFFF00ull}) {
        const bool allowed = gxv::fn_in(def, Fn::SetInteraction);
        CHECK(gxv::fn_act(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Probe, allowed), allowed, vx) == Act::Allow);
        CHECK(gxv::fn_dir(Fn::SetInteraction, r8) == ((r8 & 0xFF) ? gxv::Dir::On : gxv::Dir::Off));
    }
    // probe: an allowed handler is reported as allowed, not as "would veto".
    CHECK(gxv::fn_act(gxv::fn_mode(Fn::SetInteraction, Mode::Probe, Mode::Off, true), true, vx) == Act::Allow);
    CHECK(gxv::fn_act(gxv::fn_mode(Fn::SetInteraction, Mode::Probe, Mode::Off, false), false, vx) == Act::Would);
    // The own M and anything not judged X: nothing, allowed or not.
    gxv::Facts m = camp_cutscene();
    m.g = kM;
    for (bool allowed : {false, true}) {
        CHECK(gxv::fn_act(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off, allowed), allowed, gxv::judge(m)) ==
              Act::None);
        for (uint8_t v = 0; v < static_cast<uint8_t>(Verdict::Count); ++v) {
            if (static_cast<Verdict>(v) == Verdict::Veto) continue;
            CHECK(gxv::fn_act(Mode::On, allowed, static_cast<Verdict>(v)) == Act::None);
        }
        CHECK(gxv::fn_act(Mode::Off, allowed, Verdict::Veto) == Act::None);
    }
    // An empty list restores session 11's behaviour (SetInteraction vetoed for X).
    const gxv::FnSet none = gxv::parse_allow("").set;
    CHECK(gxv::fn_act(gxv::fn_mode(Fn::SetInteraction, Mode::On, Mode::Off, gxv::fn_in(none, Fn::SetInteraction)),
                      gxv::fn_in(none, Fn::SetInteraction), vx) == Act::Veto);

    // Directions: only SetInteraction and BlockInteraction carry one (their wrappers' r8b).
    CHECK(gxv::fn_dir(Fn::BlockInteraction, 1) == gxv::Dir::On && gxv::fn_dir(Fn::BlockInteraction, 0) == gxv::Dir::Off);
    CHECK(std::string_view(gxv::dir_name(Fn::SetInteraction, gxv::Dir::On)) == "add");
    CHECK(std::string_view(gxv::dir_name(Fn::SetInteraction, gxv::Dir::Off)) == "remove");
    CHECK(std::string_view(gxv::dir_name(Fn::BlockInteraction, gxv::Dir::Off)) == "off");
    for (size_t i = 0; i < gxv::kFns; ++i) {
        const auto f = static_cast<Fn>(i);
        if (f == Fn::SetInteraction || f == Fn::BlockInteraction) continue;
        CHECK(!gxv::fn_has_dir(f) && gxv::fn_dir(f, 1) == gxv::Dir::None && gxv::dir_name(f, gxv::Dir::None)[0] == 0);
    }
}

void test_context() {
    constexpr uint64_t kSp = 0x14FE00;  // the routed 2981 handler's rsp at 0xC07749 (and at 0xC07776)
    constexpr uint64_t kT = 1'000'000'000;
    gxv::ContextSlot c;
    CHECK(!c.check(0x1000, kT));  // empty

    // Routed 2981: set, a deeper 0x778AB0 call is inside; the same frame, a frame above are not.
    c.set(kX, kSp, kT);
    CHECK(c.check(0x14F000, kT + 1000));
    CHECK(!c.check(kSp, kT + 1000));
    CHECK(!c.check(0x14FF00, kT + 1000));
    // The clear point 0xC07776: same rsp -> cleared.
    CHECK(c.clear_at(kSp) && c.get().x == 0);
    CHECK(!c.clear_at(kSp));  // nothing left

    // A clear point deeper than the setter (another frame below) keeps it; a frame above clears it (review 3).
    c.set(kX, kSp, kT);
    CHECK(!c.clear_at(0x14F000) && c.get().x == kX);
    CHECK(c.clear_at(0x14FF00) && c.get().x == 0);

    // The clear point never ran: a routing hook's entry (next handler) clears it.
    c.set(kX, kSp, kT);
    c.clear();
    CHECK(!c.check(0x14F000, kT + 1000) && c.get().x == 0);

    // Staleness: an old context is dropped at the check (and reported), even for a deeper call.
    c.set(kX, kSp, kT);
    bool stale = false;
    CHECK(c.check(0x14F000, kT + gxv::kContextMaxAgeUs, &stale) && !stale);
    CHECK(!c.check(0x14F000, kT + gxv::kContextMaxAgeUs + 1, &stale) && stale && c.get().x == 0);
    c.set(kX, kSp, kT);
    CHECK(!c.check(0x14F000, kT - 1, &stale) && stale);  // a clock going back: stale too
    CHECK(gxv::stale(gxv::Context{kX, kSp, kT}, kT + gxv::kContextMaxAgeUs + 1));
    CHECK(!gxv::stale(gxv::Context{}, kT + gxv::kContextMaxAgeUs + 1));
    CHECK(!gxv::inside(gxv::Context{kX, 0, kT}, 0x1000, kT));
    CHECK(!gxv::inside(gxv::Context{0, kSp, kT}, 0x1000, kT));

    // Nested RAII scopes (guest_seq_play's tick inside nothing; a live call inside a tick): exchange and restore.
    c.clear();
    const gxv::Context outer = c.exchange({kX, 0x14E000, kT});      // tick scope
    CHECK(outer.x == 0 && c.check(0x14D000, kT + 10));
    const gxv::Context inner = c.exchange({kX + 8, 0x14D800, kT});  // a scope deeper
    CHECK(inner.x == kX && c.get().x == kX + 8);
    c.exchange(inner);  // inner scope's destructor
    CHECK(c.get().x == kX && c.get().sp == 0x14E000);
    c.exchange(outer);  // outer scope's destructor
    CHECK(c.get().x == 0);

    // The SEH filter of our call clears it before any unwind.
    c.set(kX, kSp, kT);
    c.clear();
    CHECK(c.get().x == 0 && c.get().sp == 0 && c.get().us == 0);
}

void test_b1() {
    bool bad = true;
    CHECK(gxv::parse_local_after_remote("", &bad) == gxv::LocalAfterRemote::Allow && !bad);
    CHECK(gxv::parse_local_after_remote("allow", &bad) == gxv::LocalAfterRemote::Allow && !bad);
    CHECK(gxv::parse_local_after_remote("stuck", &bad) == gxv::LocalAfterRemote::Stuck && !bad);
    CHECK(gxv::parse_local_after_remote("end", &bad) == gxv::LocalAfterRemote::Allow && bad);

    using W = gxv::LocalLoadWatch;
    const auto stuck = gxv::LocalAfterRemote::Stuck, allow = gxv::LocalAfterRemote::Allow;

    // Session 9: 3093 at 749.012 (local connection after the remote one), state 0x0C from ~757 s, no 2549 until Alt+F4.
    {
        W w;
        w.note_frame(stuck, true, 2490, 746'634);  // the title's login: nothing
        CHECK(!w.armed());
        w.note_frame(stuck, true, 3093, 749'012);
        CHECK(w.armed());
        uint64_t t = 749'012;
        bool tripped = false;
        uint64_t when = 0;
        uint8_t state = 0x04;
        for (; t < 860'000 && !tripped; t += 5'000) {
            if (t > 757'000) state = 0x0C;
            tripped = w.sample(t, state, true);
            if (tripped) when = t;
        }
        CHECK(tripped && when - 749'012 >= W::kStuckMs && when < 860'000);
        CHECK(!w.armed() && !w.sample(when + 5'000, 0x0C, true));  // once
    }
    // A normal local load: the 2549 ~15 s after (a first trip of session 9: 2174 at 31.2, 2549 at 46.2).
    {
        W w;
        w.note_frame(stuck, true, 3093, 0);
        CHECK(!w.sample(5'000, 0x0C, true));
        w.note_frame(stuck, true, 2549, 16'000);
        CHECK(!w.armed());
        CHECK(!w.sample(60'000, 0x0C, true));
    }
    // A slow load that keeps moving (the state changes): never stuck while it moves.
    {
        W w;
        w.note_frame(stuck, true, 3093, 0);
        const uint8_t states[] = {0x04, 0x0C, 0x0E, 0x11, 0x12, 0x12, 0x12, 0x13, 0x14, 0x16, 0x17, 0x17};
        uint64_t t = 0;
        bool tripped = false;
        for (uint8_t s : states) tripped |= w.sample(t += 5'000, s, true);
        CHECK(!tripped);
        CHECK(!w.sample(t += 5'000, 0x18, true) && !w.armed());  // in the world: done
    }
    // Unreadable state: never a trip on that sample (and the "still" clock restarts).
    {
        W w;
        w.note_frame(stuck, true, 3093, 0);
        bool tripped = false;
        for (uint64_t t = 5'000; t <= 120'000; t += 5'000) tripped |= w.sample(t, 0, false);
        CHECK(!tripped && w.armed());
    }
    // Not armed: allow, or no remote session in this run, or other frames.
    {
        W w;
        w.note_frame(allow, true, 3093, 0);
        CHECK(!w.armed());
        w.note_frame(stuck, false, 3093, 0);
        CHECK(!w.armed());
        w.note_frame(stuck, true, 2001, 0);
        CHECK(!w.armed() && !w.sample(100'000, 0x0C, true));
    }
}

}  // namespace

int main() {
    test_modes();
    test_judge();
    test_act();
    test_fn();
    test_allow();
    test_context();
    test_b1();
    if (g_failures) {
        std::printf("optd_x_veto: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("optd_x_veto: all passed\n");
    return 0;
}
