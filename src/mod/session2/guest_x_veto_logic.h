#pragma once
// P8 session 9 (docs/research/pendientes/p8-s9-cinematica.md §7-§8): the GUEST's veto of what a stage of
// ANOTHER character's sequencer manager (X, the host's Kliff's ClientSequencerStageManager on this client)
// would do to THIS client: its field reload (StageChart function Loading -> 0x778AB0 -> [P+0x15C] = 1, the
// session 9 kick to the title), its camera / screen / input takes and (session 10, p8-s10.md §4) its dialogue and
// interface functions (the host's shop talk showing its menu on the guest). Pure logic, no game access (tests:
// tests/optd_x_veto_tests.cpp).
//
// Also B1 (§7.5, review 5): a save loaded on this PC's own server after a session on the host's server in the
// same run, ended only when seen stuck.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cdmp::session2::gxv {

// ---------------------------------------------------------------- keys

enum class Mode : uint8_t { Off = 0, Probe = 1, On = 2 };

inline Mode parse_mode(std::string_view s, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (s.empty() || s == "0") return Mode::Off;
    if (s == "probe") return Mode::Probe;
    if (s == "1") return Mode::On;
    if (bad) *bad = true;
    return Mode::Off;
}
inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "0";
        case Mode::Probe: return "probe";
        case Mode::On: return "1";
    }
    return "?";
}

// ---------------------------------------------------------------- whose stage is it?

constexpr uint32_t kCharClass = 0xA;  // key >> 28 of a character (0xA0100001, 0xA0100377)

// What the hook read for one call of a ClientSequencerStage_StageChartProcessor handler (rcx = the processor;
// G = [proc+8], the stage manager whose stage runs the function).
struct Facts {
    bool remote = false;      // guest_remote_live(): this connection plays on the host right now
    uint64_t g = 0;           // [proc+8]
    uint64_t g_vt = 0;        // [G]
    uint64_t want_vt = 0;     // exe + 0x55C9720 (pa::ClientSequencerStageManager)
    uint32_t g_key = 0;       // [[G+0x18]+0x60]: the character that owns G (0 = unreadable)
    uint64_t m = 0;           // this client's current M (a vf48 result seen lately on the main thread; 0 = unknown)
    uint32_t m_key = 0;       // M's live owner ([[M+0x18]+0x60]; 0 = unknown)
    uint32_t own_key = 0;     // this connection's own character (its 2756; 0 = not known yet)
    bool in_table = false;    // guest_seq_play's XTable knows (G, g_key) in this connection
};

enum class Verdict : uint8_t {
    NotRemote,        // not playing on the host: the game's own behaviour, always
    NoManager,        // [proc+8] null or unreadable
    NotStageManager,  // [G] is not a ClientSequencerStageManager
    OwnManager,       // G belongs to this client's own character (its M): never vetoed
    UnknownOwner,     // own character not known yet, G not in the table: not vetoed (fail open)
    NotCharacter,     // G's owner is not a character (or unreadable): not vetoed
    Veto,             // another character's manager (X): veto (probe: would veto)
    Count
};

inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::NotRemote: return "not remote";
        case Verdict::NoManager: return "no manager";
        case Verdict::NotStageManager: return "not a stage manager";
        case Verdict::OwnManager: return "own manager (M)";
        case Verdict::UnknownOwner: return "own character not known yet";
        case Verdict::NotCharacter: return "owner not a character";
        case Verdict::Veto: return "another character's manager (X)";
        case Verdict::Count: break;
    }
    return "?";
}

inline Verdict judge(const Facts& f) {
    if (!f.remote) return Verdict::NotRemote;
    if (!f.g) return Verdict::NoManager;
    if (!f.want_vt || f.g_vt != f.want_vt) return Verdict::NotStageManager;
    // The own M, by identity or by its live owner (review 4: what guest_seq_route's decide() compares), and the own
    // character pinned at login as a last resort.
    if (f.m && f.g == f.m) return Verdict::OwnManager;
    if (f.m_key && f.g_key == f.m_key) return Verdict::OwnManager;
    if (f.own_key && f.g_key == f.own_key) return Verdict::OwnManager;
    if (f.in_table && f.g_key) return Verdict::Veto;  // routed X of this connection (never the own keys: above)
    if (!f.own_key && !f.m_key) return Verdict::UnknownOwner;
    if (!f.g_key || (f.g_key >> 28) != kCharClass) return Verdict::NotCharacter;
    return Verdict::Veto;
}

// What the hook does with a verdict: acts only with On and a Veto; Probe only counts "would". Allow: a handler of
// guest_x_ui_veto_allow ran for X natively (fn_act only; act() never returns it).
enum class Act : uint8_t { None, Would, Veto, Allow };
inline Act act(Mode m, Verdict v) {
    if (v != Verdict::Veto || m == Mode::Off) return Act::None;
    return m == Mode::On ? Act::Veto : Act::Would;
}
// V0 (0x778AB0) never skips (review 2): neither P nor the request says whose stage asked for the load, so a context
// hit is only counted; V1 (Loading, whose G is known) is the veto.
inline Mode v0_effective(Mode stage) { return stage == Mode::Off ? Mode::Off : Mode::Probe; }

// ---------------------------------------------------------------- V0's context ("X context")

// Set around our own calls into the game for X (guest_seq_play's tick / live update) and around the routed 2981
// handler's call 0xA9E100(X) (0xC07749 .. 0xC07776). It records the stack pointer of the frame that set it and when:
// a call of 0x778AB0 is inside the context only from a deeper frame (a lower address) on the same thread, and only
// while the context is younger than max_age_us (review 3: a context whose clear point never ran ages out).
struct Context {
    uint64_t x = 0;
    uint64_t sp = 0;  // the setter's stack address
    uint64_t us = 0;  // when it was set (QPC microseconds)
};
constexpr uint64_t kContextMaxAgeUs = 500'000;  // a routed 2981 or one tick of X lasts far less (one frame)
inline bool stale(const Context& c, uint64_t now_us, uint64_t max_age_us = kContextMaxAgeUs) {
    return c.x != 0 && (now_us < c.us || now_us - c.us > max_age_us);
}
inline bool inside(const Context& c, uint64_t now_sp, uint64_t now_us, uint64_t max_age_us = kContextMaxAgeUs) {
    return c.x != 0 && c.sp != 0 && now_sp < c.sp && !stale(c, now_us, max_age_us);
}

// The per-thread slot (a thread_local in guest_x_veto.cpp). Every way out is a clear:
//  - clear_at(sp): the clear point 0xC07776 (the routed 2981's frame: rsp equal) or any frame at or above the
//    setter's (sp >= the setter's): a context set deeper that was never cleared;
//  - clear(): every guest_seq_route hook entry (a new handler), the SEH filters of our calls for X;
//  - check(): a stale context is cleared;
//  - exchange(): the RAII scopes (save the previous, restore it on exit).
class ContextSlot {
public:
    void set(uint64_t x, uint64_t sp, uint64_t us) { c_ = {x, sp, us}; }
    void clear() { c_ = {}; }
    bool clear_at(uint64_t sp) {
        if (c_.x == 0 || sp < c_.sp) return false;
        c_ = {};
        return true;
    }
    // inside() for a call at now_sp; a stale context is cleared here (and reported through *was_stale).
    bool check(uint64_t now_sp, uint64_t now_us, bool* was_stale = nullptr) {
        if (was_stale) *was_stale = false;
        if (stale(c_, now_us)) {
            c_ = {};
            if (was_stale) *was_stale = true;
            return false;
        }
        return inside(c_, now_sp, now_us);
    }
    Context exchange(const Context& n) {
        const Context old = c_;
        c_ = n;
        return old;
    }
    const Context& get() const { return c_; }

private:
    Context c_{};
};

// ---------------------------------------------------------------- guest_x_ui_veto: dialogue / UI / camera handlers

// The stage chart functions that can show something on THIS client when a stage of X runs them (p8-s10.md §3, §4):
// the dialogue candidates (StageChart_Function_X vf8 / vf9 that jump to their own code), the processor's interface
// slots and its camera slots (vtable 0x558C080). Hook points and native ways out: guest_x_veto.cpp, p8-s10.md §4.
enum class Family : uint8_t { Dialog, Ui, Camera };

enum class Fn : uint8_t {
    // dialogue (G = [proc+8]; proc = rcx, or [[rdx]+0x58] for the two interaction functions)
    SetQuestDialogAICategory,  // vf8 0x236A510 -> 0x204DC80 (thunk -> .xtls)
    TimelineEvent,             // vf8 0x2368F90 -> 0x204BB90                       vetoable
    TimelineEventEnd,          // vf9 0x2368FA0 -> 0x204C410 (thunk -> .xtls)
    BlockInteraction,          // vf8/vf9 0x235E1B0/0x235E1C0 -> 0x23698F0 (thunk -> .xtls)
    SetInteraction,            // vf8/vf9 0x235E0F0/0x235E100 -> 0x2369360         vetoable
    // interface (processor slots)
    InputBlock,              // +0x150 0x5E4E60                                    vetoable
    UIControl,               // +0x158 0x5E4EB0                                    vetoable
    ChangePhasePanelTag,     // +0x160 0x5E9750 (no way out)
    UIShowMinimap,           // +0x168 0x5ECD90                                    vetoable
    UIFindPath,              // +0x170 0x5ED9A0 (its way out destroys a lookup result)
    UIStageIcon,             // +0x1F8 0x5F2980 (thunk -> .xtls)
    UIStageGuide,            // +0x238 0x5F3A90                                    vetoable
    UIStageBackgroundGuide,  // +0x240 0x5F4050 (no way out)
    UIHint,                  // +0x248 0x5F4110 (thunk -> .xtls)
    UIHintEnd,               // +0x250 0x5F4200 (thunk -> .xtls)
    // camera / screen (processor slots; probe by design, p8-s9-cinematica.md §7.6)
    DeactiveSequencerCamera,             // +0x1B8 0x5EE210
    DeactiveSequencerCameraEnd,          // +0x1C0 0x5EE230
    SequencerCamera,                     // +0x1C8 0x5EE250 (thunk -> .xtls)
    DeactivateSequencerCameraFadeIn,     // +0x1D0 0x5EE0C0
    DeactivateSequencerCameraFadeInEnd,  // +0x1D8 0x5EE160 (thunk -> .xtls)
    CameraPreset,                        // +0x1E0 0x5EDD30
    CameraPresetEnd,                     // +0x1E8 0x5EE1F0
    ForceLockOn,                         // +0x1F0 0x5EAE90
    LetterBox,                           // +0x208 0x5F2970
    FadeInOut,                           // +0x210 0x5F52E0 (thunk -> .xtls)
    LookAt,                              // +0x220 0x5F3600
    LookAtEnd,                           // +0x228 0x5F3810
    PostProcess,                         // +0x260 0x5F4440
    Count
};
constexpr size_t kFns = static_cast<size_t>(Fn::Count);

struct FnInfo {
    const char* name;
    Family family;
    // The native early way out was read whole and the hook point reaches it with the same frame (p8-s10.md §4.2):
    // only these act with '1'; every other handler stays a probe.
    bool vetoable;
};

inline FnInfo fn_info(Fn f) {
    switch (f) {
        case Fn::SetQuestDialogAICategory: return {"SetQuestDialogAICategory", Family::Dialog, false};
        case Fn::TimelineEvent: return {"TimelineEvent", Family::Dialog, true};
        case Fn::TimelineEventEnd: return {"TimelineEvent(vf9)", Family::Dialog, false};
        case Fn::BlockInteraction: return {"BlockInteraction", Family::Dialog, false};
        case Fn::SetInteraction: return {"SetInteraction", Family::Dialog, true};
        case Fn::InputBlock: return {"InputBlock", Family::Ui, true};
        case Fn::UIControl: return {"UIControl", Family::Ui, true};
        case Fn::ChangePhasePanelTag: return {"ChangePhasePanelTag", Family::Ui, false};
        case Fn::UIShowMinimap: return {"UIShowMinimap", Family::Ui, true};
        case Fn::UIFindPath: return {"UIFindPath", Family::Ui, false};
        case Fn::UIStageIcon: return {"UIStageIcon", Family::Ui, false};
        case Fn::UIStageGuide: return {"UIStageGuide", Family::Ui, true};
        case Fn::UIStageBackgroundGuide: return {"UIStageBackgroundGuide", Family::Ui, false};
        case Fn::UIHint: return {"UIHint", Family::Ui, false};
        case Fn::UIHintEnd: return {"UIHint(+0x250)", Family::Ui, false};
        case Fn::DeactiveSequencerCamera: return {"DeactiveSequencerCamera", Family::Camera, false};
        case Fn::DeactiveSequencerCameraEnd: return {"DeactiveSequencerCamera(+0x1C0)", Family::Camera, false};
        case Fn::SequencerCamera: return {"SequencerCamera", Family::Camera, false};
        case Fn::DeactivateSequencerCameraFadeIn: return {"DeactivateSequencerCameraFadeIn", Family::Camera, false};
        case Fn::DeactivateSequencerCameraFadeInEnd:
            return {"DeactivateSequencerCameraFadeIn(+0x1D8)", Family::Camera, false};
        case Fn::CameraPreset: return {"CameraPreset", Family::Camera, false};
        case Fn::CameraPresetEnd: return {"CameraPreset(+0x1E8)", Family::Camera, false};
        case Fn::ForceLockOn: return {"ForceLockOn", Family::Camera, false};
        case Fn::LetterBox: return {"LetterBox", Family::Camera, false};
        case Fn::FadeInOut: return {"FadeInOut", Family::Camera, false};
        case Fn::LookAt: return {"LookAt", Family::Camera, false};
        case Fn::LookAtEnd: return {"LookAt(+0x228)", Family::Camera, false};
        case Fn::PostProcess: return {"PostProcess", Family::Camera, false};
        case Fn::Count: break;
    }
    return {"?", Family::Camera, false};
}
inline const char* family_name(Family f) {
    switch (f) {
        case Family::Dialog: return "dialogue";
        case Family::Ui: return "interface";
        case Family::Camera: return "camera";
    }
    return "?";
}

// The camera key alone: '1' is not applied (each handler must be read whole first, §7.6): it acts as probe.
inline Mode camera_effective(Mode asked) { return asked == Mode::On ? Mode::Probe : asked; }

// One handler's mode. guest_x_ui_veto covers every family; without it, guest_x_camera_veto (probe at most) still
// covers the camera family, as before. '1' acts only on a vetoable handler that is not allowed (guest_x_ui_veto_allow,
// below); the rest stay probe (and the log says so).
inline Mode fn_mode(Fn f, Mode ui, Mode camera_asked, bool allowed = false) {
    const FnInfo i = fn_info(f);
    Mode m = ui;
    if (m == Mode::Off && i.family == Family::Camera) m = camera_effective(camera_asked);
    if (m == Mode::On && (!i.vetoable || allowed)) m = Mode::Probe;
    return m;
}
// '1' was asked for this handler but it stays a probe (not vetoable, and not allowed on purpose).
inline bool fn_held_as_probe(Fn f, Mode ui, bool allowed = false) {
    return ui == Mode::On && !fn_info(f).vetoable && !allowed;
}

// ---------------------------------------------------------------- guest_x_ui_veto_allow: handlers left native for X
//
// [session2] guest_x_ui_veto_allow = <handler names> (default "SetInteraction"; p8-impl-veto-interaccion.md): the
// handlers that run natively for X whatever guest_x_ui_veto says. Each is still hooked and counted (G = X / own M,
// on / off) and logged ALLOWED, never vetoed nor counted as "would". SetInteraction (0x2369360) adds (r8b = 1) or
// removes (r8b = 0) the dynamic interaction entries of the actors its stage binds (the NPC's talk / shop): world state,
// not this client's camera / input / interface (session 11: Renee's and Delkin's registrations were vetoed and the
// guest never sent its 2720 to them).
//
// The decision is per handler, never per direction: a handler that does both ends (SetInteraction add / remove,
// BlockInteraction on / off, InputBlock block / unblock) is allowed or vetoed as a whole, so an add is never let
// through without its remove. Names are fn_info's, case-insensitive, separated by commas, semicolons or blanks;
// "", "0" or "none" = nothing allowed (every vetoable handler acts with '1', as before).
using FnSet = uint32_t;
static_assert(kFns <= 32, "FnSet is a 32-bit mask over gxv::Fn");
constexpr FnSet fn_bit(Fn f) { return FnSet{1} << static_cast<unsigned>(f); }
inline bool fn_in(FnSet s, Fn f) { return (s & fn_bit(f)) != 0; }
constexpr std::string_view kDefaultAllow = "SetInteraction";

struct AllowList {
    FnSet set = 0;
    size_t bad = 0;               // tokens that name no handler
    std::string_view first_bad;   // the first of them (a view into the parsed string)
};

inline bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

inline AllowList parse_allow(std::string_view s) {
    AllowList r;
    const auto sep = [](char c) { return c == ',' || c == ';' || c == ' ' || c == '\t'; };
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && sep(s[i])) ++i;
        size_t j = i;
        while (j < s.size() && !sep(s[j])) ++j;
        const std::string_view tok = s.substr(i, j - i);
        i = j;
        if (tok.empty() || tok == "0" || iequal(tok, "none")) continue;
        bool found = false;
        for (size_t k = 0; k < kFns; ++k) {
            if (iequal(tok, fn_info(static_cast<Fn>(k)).name)) {
                r.set |= fn_bit(static_cast<Fn>(k));
                found = true;
                break;
            }
        }
        if (!found) {
            if (r.bad++ == 0) r.first_bad = tok;
        }
    }
    return r;
}

// What one call of a handler does: act(), except that an allowed handler's would / veto becomes Allow (it runs).
inline Act fn_act(Mode m, bool allowed, Verdict v) {
    const Act a = act(m, v);
    return a != Act::None && allowed ? Act::Allow : a;
}

// The two handlers whose direction is r8b at the hook (their vf8 / vf9 wrappers load mov r8b,1 / xor r8d,r8d):
// SetInteraction 0x236938A (before its movzx r13d,r8b) and BlockInteraction 0x23698F0 (the thunk's entry). Counted
// per direction for X, so the log shows whether X's adds and removes pair up.
enum class Dir : uint8_t { None = 0, On = 1, Off = 2 };
inline bool fn_has_dir(Fn f) { return f == Fn::SetInteraction || f == Fn::BlockInteraction; }
inline Dir fn_dir(Fn f, uint64_t r8) {
    if (!fn_has_dir(f)) return Dir::None;
    return (r8 & 0xFF) != 0 ? Dir::On : Dir::Off;
}
inline const char* dir_name(Fn f, Dir d) {
    if (d == Dir::None) return "";
    if (f == Fn::SetInteraction) return d == Dir::On ? "add" : "remove";
    return d == Dir::On ? "on" : "off";
}

// Whose manager ran it, for the per-handler counters: X (another character's manager: the veto's target), the own M
// (or the own character's manager), or anything else (not remote, not a stage manager, owner unknown...).
enum class Who : uint8_t { X, M, Other };
inline Who who(Verdict v) {
    if (v == Verdict::Veto) return Who::X;
    if (v == Verdict::OwnManager) return Who::M;
    return Who::Other;
}

// The stage's name for the log lines (the qwords read raw at [[[proc+0x10]+0x48]+0x1B8]): printable ASCII up to the
// first NUL, anything else '?'; empty when nothing was read. Returns the length written (out is NUL-terminated).
inline size_t stage_name(const uint64_t* q, size_t nq, char* out, size_t cap) {
    if (!cap) return 0;
    size_t n = 0;
    for (size_t i = 0; i < nq * 8 && n + 1 < cap; ++i) {
        const auto c = static_cast<unsigned char>(q[i / 8] >> (8 * (i % 8)));
        if (c == 0) break;
        out[n++] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out[n] = 0;
    return n;
}

// ---------------------------------------------------------------- B1: a local save after a remote session

// [session2] guest_local_after_remote = allow | stuck (default allow; review 5): with stuck, a save loaded on this
// PC's own server after this run already played on the host's server ends the game only when that load is SEEN
// stuck (session 9: the local 3093 at 749.0, its 2174 at 750.7, then loading state 0x0C and no MoveFieldCompleteReq
// 2549 until Alt+F4 97 s later; a normal load sends its 2549 ~15 s after its 2174).
enum class LocalAfterRemote : uint8_t { Allow, Stuck };
inline LocalAfterRemote parse_local_after_remote(std::string_view s, bool* bad = nullptr) {
    if (bad) *bad = false;
    if (s.empty() || s == "allow") return LocalAfterRemote::Allow;
    if (s == "stuck") return LocalAfterRemote::Stuck;
    if (bad) *bad = true;
    return LocalAfterRemote::Allow;
}

constexpr uint16_t kOpLoginCharacterToFieldServerReq = 3093;  // the title's Play / Load: a save is loaded
constexpr uint16_t kOpMoveFieldCompleteReq = 2549;            // the client finished its field load
constexpr uint8_t kStateInWorld = 0x18;                       // [P+0x15C] in the world

// The watch: guest_send notes the frames of a local connection, the diagnostics thread samples [P+0x15C] every 5 s.
class LocalLoadWatch {
public:
    static constexpr uint64_t kStuckMs = 45'000;  // no 2549 this long after the 3093 (3x a normal load)...
    static constexpr uint64_t kStillMs = 20'000;  // ...and the same loading state this long

    // A frame of a LOCAL connection, after a remote one in this run (had_remote).
    void note_frame(LocalAfterRemote policy, bool had_remote, uint16_t opcode, uint64_t now_ms) {
        if (policy != LocalAfterRemote::Stuck || !had_remote) return;
        if (opcode == kOpLoginCharacterToFieldServerReq) {
            armed_ = true;
            since_ms_ = now_ms;
            state_ = 0xFF;
            state_since_ms_ = now_ms;
        } else if (opcode == kOpMoveFieldCompleteReq) {
            armed_ = false;  // the load got through
        }
    }
    // True once: the load is stuck (the caller ends the game).
    bool sample(uint64_t now_ms, uint8_t state, bool state_ok) {
        if (!armed_) return false;
        if (state_ok && state == kStateInWorld) {
            armed_ = false;  // in the world: not stuck
            return false;
        }
        if (!state_ok || state != state_) {
            state_ = state_ok ? state : 0xFF;
            state_since_ms_ = now_ms;
            return false;
        }
        if (now_ms - since_ms_ >= kStuckMs && now_ms - state_since_ms_ >= kStillMs) {
            armed_ = false;
            return true;
        }
        return false;
    }
    bool armed() const { return armed_; }
    uint64_t since_ms() const { return since_ms_; }
    uint8_t state() const { return state_; }

private:
    bool armed_ = false;
    uint64_t since_ms_ = 0;
    uint8_t state_ = 0xFF;
    uint64_t state_since_ms_ = 0;
};

}  // namespace cdmp::session2::gxv
