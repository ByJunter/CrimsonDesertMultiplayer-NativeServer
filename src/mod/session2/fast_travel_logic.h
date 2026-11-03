#pragma once
// P8 session 15: user 2's fast travel in the shared field (docs/research/pendientes/p8-impl-viaje-rapido.md; the
// root cause: p8-s14-teleport-cierre.md). The pure logic of fast_travel.cpp and of user2_funnels.cpp's F3, with no
// game access, so the tests can run it. Build 1.0.0.2976.
//
// The fault [V static + S14 capture]: the guest's map teleport sends ReloadStageStartReq 2283 (53 bytes). The host's
// server handler 0x2A5F580 finds the sender's field and asks it `call [rax+0x48]` (0x2A5F7F9, ServerField::vf48
// 0x2B00CE0): the FIELD OWNER's (the host's) ServerSequencerStageManager. 0x2896120(manager, ...) works on
// r13 = [manager+0x18] (0x2896155), the manager's character, and 0x2BC4950 writes the destination into ITS
// TransformSync +0x550: the host's Kliff. User 2's 2437 then finds its own +0x550 empty (0x2BC9256) ->
// eErrNoInvalidFieldInfoKey -> 2679 -> the guest is logged out.
//
// Three pieces, each its own key:
//   F3 host  [session2] user2_fast_travel = 0 | probe | 1 (user2_funnels): inside user 2's 2283, the vf48 call that
//            returns to 0x2A5F7FC answers user 2's own ServerSequencerStageManager ([[[pj+0x68]+0xA8]+0x18], its
//            vtable and its back pointer [mgr+0x18] == user 2's character checked) instead of the owner's, or, when
//            that cannot be read, a block whose +0x18 is user 2's character (never the original with 1); and the
//            second vf48 of the same 2283 (0x2BC4C53, inside 0x2BC4950) gets the same real manager. probe changes
//            nothing (and F2 drops every 2283 with it: it only shows that F3 installs).
//   F2 host  always when F3 cannot take it: user 2's 2283 is dropped before the host's server sees it, while user 2
//            is not proven to be in a field of its own (so the host's +0x550 is never written by the guest).
//   F1 guest [session2] guest_fast_travel_veto = 0 | probe | 1: the client's fast-travel entry 0x655110 returns 0
//            without effect while the connection plays on the host (its three callers ignore the result).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace cdmp::session2::ftl {

enum class Mode : uint8_t { Off, Probe, On };

struct ParsedMode {
    Mode mode = Mode::Off;
    bool known = true;  // false: an unknown value, treated as Off (warned)
};

// "0" | "probe" | "1" (also off/no/false, on/yes/true); an inline comment ("probe ; S15") is not part of the value.
inline ParsedMode parse_mode(std::string_view raw) {
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) raw = raw.substr(0, c);
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }
    char v[8] = {};
    if (raw.size() >= sizeof v) return {Mode::Off, false};
    for (size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        v[i] = ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
    }
    const std::string_view s(v, raw.size());
    if (s.empty() || s == "0" || s == "off" || s == "no" || s == "false") return {Mode::Off, true};
    if (s == "probe") return {Mode::Probe, true};
    if (s == "1" || s == "on" || s == "yes" || s == "true") return {Mode::On, true};
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

// ---------------------------------------------------------------- the 2283 payload

inline constexpr uint16_t kOpReloadStageStartReq = 2283;
inline constexpr size_t kReloadPayload = 53;

// The server's handler 0x2A5F580 reads, in order (0x2A5F64B..0x2A5F77B): u32 a, u32 b, u32 c, float3 position
// (-> +0x24 of its transform), float4 rotation (0x141FA90 -> +0x14), float3 scale (-> +8), u8 flag (-> +0x90).
// Both client senders (0x655110 and 0xA9A860, through PostPacket 0xC04D20) write a = 0; b = the destination's
// FieldInfoKey (0x4A2010 of the current level, or the caller's r8d); c = an actor key to travel to (0x655110 with
// r9d != 0), else 0. 0x2896120 then: b == 0 -> reload in place (own transform); c != 0 -> to that actor; else the
// packet's position. There is NO sequencer id in it (observer_seq_copy's old assumption "u64 seq @0").
struct Reload {
    uint32_t a = 0;
    uint32_t field_info_key = 0;  // b
    uint32_t target_actor = 0;    // c
    float pos[3] = {};
    float rot[4] = {};
    float scale[3] = {};
    uint8_t flag = 0;
};

inline std::optional<Reload> parse_reload(std::span<const uint8_t> p) {
    if (p.size() < kReloadPayload) return std::nullopt;
    Reload r;
    std::memcpy(&r.a, p.data() + 0, 4);
    std::memcpy(&r.field_info_key, p.data() + 4, 4);
    std::memcpy(&r.target_actor, p.data() + 8, 4);
    std::memcpy(r.pos, p.data() + 12, 12);
    std::memcpy(r.rot, p.data() + 24, 16);
    std::memcpy(r.scale, p.data() + 40, 12);
    r.flag = p[52];
    return r;
}

// ---------------------------------------------------------------- F2: the host's gate on user 2's 2283

// Where user 2's character is, read on the network thread just before the 2283 would be handed to the server:
// the owner (u32 +0x180) of the field its TransformSync's field mover holds (TS+0x4F0+0x10).
enum class Where : uint8_t { Unknown, HostField, OwnField, OtherField };

inline Where where_is(bool read_ok, uint32_t owner, uint32_t host_user, uint32_t user2_user) {
    if (!read_ok || !owner) return Where::Unknown;
    if (user2_user && owner == user2_user) return Where::OwnField;
    if (host_user && owner == host_user) return Where::HostField;
    return Where::OtherField;
}

enum class Gate : uint8_t {
    Pass,        // not a 2283, or user 2 is in a field of its own (the native code is right there)
    PassF3,      // F3 is live (user2_fast_travel=1) and its subject will recognise user 2's character: to the server
    Drop,        // F2: dropped, counted, a WRN line
    Count
};

struct GateIn {
    uint16_t opcode = 0;
    Mode f3 = Mode::Off;      // user2_fast_travel
    bool f3_live = false;     // F3's hooks are installed and its call sites verified
    bool subject_ok = false;  // user2_funnels' keys are live and ExecuteTroc's subject test passes on user 2's
                              // character now (key, controller, class): F3 will see the 2283 as user 2's
    Where where = Where::Unknown;
};

// F2 is on whenever F3 cannot take the 2283 (user2_fast_travel != 1 - probe included -, its hooks refused, or the
// subject would not recognise user 2 now): the host's +0x550 is never written by the guest. Unknown counts as
// shared (fail safe). With F3 taking it, user 2's manager need not be readable: F3 then hands 0x2896120 a block.
inline Gate decide_gate(const GateIn& in) {
    if (in.opcode != kOpReloadStageStartReq) return Gate::Pass;
    if (in.where == Where::OwnField) return Gate::Pass;
    if (in.f3 == Mode::On && in.f3_live && in.subject_ok) return Gate::PassF3;
    return Gate::Drop;
}

inline const char* gate_name(Gate g) {
    switch (g) {
        case Gate::Pass: return "pass";
        case Gate::PassF3: return "to the server (F3 redirects its vf48)";
        case Gate::Drop: return "DROPPED (F2)";
        default: return "?";
    }
}

inline const char* where_name(Where w) {
    switch (w) {
        case Where::Unknown: return "unknown (taken as the host's field)";
        case Where::HostField: return "the host's field (shared)";
        case Where::OwnField: return "its own field";
        case Where::OtherField: return "a field of another owner";
    }
    return "?";
}

// ---------------------------------------------------------------- F3: user 2's own sequencer manager

// [[[pj+0x68]+0xA8]+0x18] (the chain ServerField::vf48 0x2B00CE0 follows from the owner's character,
// 0x2B00D0D..0x2B00D24), checked: the manager's vtable is ServerSequencerStageManager (exe+0x5B17A58, RTTI
// .?AVServerSequencerStageManager@pa@@) and its back pointer [mgr+0x18] is the character itself (0x2896155 takes
// r13 = [mgr+0x18]: that is what decides whose TransformSync gets the destination).
inline constexpr uint32_t kActorComponents = 0x68;
inline constexpr uint32_t kComponentsSequencer = 0xA8;
inline constexpr uint32_t kSeqComponentManager = 0x18;
inline constexpr uint32_t kManagerActor = 0x18;
// The object the reload setter references (0x393650 Actor_Lock: lock inc [obj+8], its vf30 and vf18, no null check):
// a manager handed to the game must have one, with a vtable (review of 17c376b, 2).
inline constexpr uint32_t kManagerLockObject = 0x8;

enum class MgrFail : uint8_t { None, Components, Component, Manager, ManagerClass, BackPointer, LockObject, Count };

inline const char* mgr_fail_name(MgrFail f) {
    switch (f) {
        case MgrFail::None: return "ok";
        case MgrFail::Components: return "no component table";
        case MgrFail::Component: return "no sequencer component";
        case MgrFail::Manager: return "no sequencer manager";
        case MgrFail::ManagerClass: return "manager of another class";
        case MgrFail::BackPointer: return "the manager's +0x18 is not the character";
        case MgrFail::LockObject: return "the manager's +0x8 object (or its vtable) is missing";
        default: return "?";
    }
}

// Mem: bool ptr(uintptr_t addr, uintptr_t& out) (guarded).
template <class Mem>
std::optional<uintptr_t> seq_manager_of(const Mem& m, uintptr_t actor, uintptr_t mgr_vt, MgrFail* why = nullptr) {
    auto fail = [why](MgrFail f) -> std::optional<uintptr_t> {
        if (why) *why = f;
        return std::nullopt;
    };
    uintptr_t comps = 0, comp = 0, mgr = 0, vt = 0, back = 0, obj = 0, obj_vt = 0;
    if (!actor || !m.ptr(actor + kActorComponents, comps) || !comps) return fail(MgrFail::Components);
    if (!m.ptr(comps + kComponentsSequencer, comp) || !comp) return fail(MgrFail::Component);
    if (!m.ptr(comp + kSeqComponentManager, mgr) || !mgr) return fail(MgrFail::Manager);
    if (!m.ptr(mgr, vt) || vt != mgr_vt) return fail(MgrFail::ManagerClass);
    if (!m.ptr(mgr + kManagerActor, back) || back != actor) return fail(MgrFail::BackPointer);
    if (!m.ptr(mgr + kManagerLockObject, obj) || !obj || !m.ptr(obj, obj_vt) || !obj_vt) return fail(MgrFail::LockObject);
    if (why) *why = MgrFail::None;
    return mgr;
}

// The vf48 call of the 2283 handler: `mov rcx,[rbp-0x28] ; mov rax,[rcx] ; call [rax+0x48]` at 0x2A5F7F2, the
// return address 0x2A5F7FC, then `mov rcx,rax ; call 0x2896120` (0x2A5F829 / 0x2A5F82C).
inline constexpr uint32_t kReloadVf48Ret = 0x2A5F7FC;
// The second vf48 of the same 2283, inside 0x2BC4950 (called from 0x2896974): `mov rcx,[rbp-0x21] ; mov rax,[rcx] ;
// call [rax+0x48]` at 0x2BC4C49 on the field of the TRAVELLER's field mover (0x2BBF340(TS+0x4F0) at 0x2BC4C3D: for
// user 2, the shared field -> the OWNER's manager), then `mov rbx,rax`, a reference on [mgr+8] (0x393650: lock xadd
// [obj+8], vf30, vf18; [mgr+8] must be a live object, a null manager crashes at 0x2BC4C69) and
// `mov byte [rbx+0x6A0],0` (0x2BC4C80). It always runs for a 2283: 0x2896120 builds the destination with +0x19 = 1
// and 0x2BC4C23 takes this block when it is not 0.
inline constexpr uint32_t kApplyVf48Ret = 0x2BC4C53;

// The block F3 hands 0x2896120 when user 2's own manager cannot be read: 0x2896120 reads its manager once,
// `mov r13,[rcx+0x18]` at 0x2896155, and never keeps rcx (overwritten at 0x289615E); the handler only does
// `mov rcx,rax` with vf48's result (0x2A5F7FC..0x2A5F82B). +0x18 = the subject (== [params] == the handler's rsi).
inline constexpr size_t kSubstituteBlock = 0x40;

enum class F3Out : uint8_t {
    NotHere,          // another caller / no subject: the original, not counted here
    Off,              // user2_fast_travel = 0 (installed for another key): the original - ERR (F2 should have dropped it)
    Probe,            // probe: the original - ERR (F2 drops every 2283 with probe)
    Substitute,       // 1: user 2's own manager (vtable and back pointer checked)
    SubstituteBlock,  // 1: user 2's manager not readable: the block (+0x18 = user 2's character) - WRN
    Count
};

struct F3In {
    Mode mode = Mode::Off;
    bool subject = false;     // inside a Req of user 2 (ExecuteTroc's subject)
    uint16_t opcode = 0;      // that Req's opcode (logged only)
    uint32_t ret_rva = 0;     // vf48's return address - exe
    bool mgr_ok = false;      // seq_manager_of(user 2's character) succeeded
};

// With 1, a 2283 of user 2 NEVER gets the original vf48 at 0x2A5F7FC, whatever the field (in user 2's own field the
// original would be its own manager anyway), the keys or the owner read: the destination always goes to the subject.
inline F3Out decide_f3(const F3In& in) {
    // The return address alone says it is the 2283's handler (0x2A5F7FC is inside 0x2A5F580 only); the opcode is
    // only logged (a Troc outside the handler table reads as 0 and must not let the owner's manager through).
    if (!in.subject || in.ret_rva != kReloadVf48Ret) return F3Out::NotHere;
    if (in.mode == Mode::Off) return F3Out::Off;
    if (in.mode == Mode::Probe) return F3Out::Probe;
    return in.mgr_ok ? F3Out::Substitute : F3Out::SubstituteBlock;
}

inline const char* f3_out_name(F3Out o) {
    switch (o) {
        case F3Out::NotHere: return "not the 2283's vf48";
        case F3Out::Off: return "user2_fast_travel=0: the OWNER's (F2 should have dropped this 2283)";
        case F3Out::Probe: return "probe: the OWNER's (F2 drops every 2283 with probe)";
        case F3Out::Substitute: return "user 2's manager";
        case F3Out::SubstituteBlock: return "user 2's manager NOT readable: a block with +0x18 = user 2's character";
        default: return "?";
    }
}

// The second vf48 (0x2BC4C53) inside the same Troc, after F3 answered the first one.
enum class ApplyOut : uint8_t {
    NotHere,   // another return address, or F3 did not answer this Troc's 0x2A5F7FC: the original
    Real,      // user 2's real manager (the one F3 returned): its [mgr+8] reference and its +0x6A0
    Original,  // F3 used the block (no real manager): the original, the OWNER's +0x6A0 is zeroed - ERR
    Count
};

inline ApplyOut decide_apply(uint32_t ret_rva, uintptr_t armed_mgr, bool block_used) {
    if (ret_rva != kApplyVf48Ret || (!armed_mgr && !block_used)) return ApplyOut::NotHere;
    return armed_mgr ? ApplyOut::Real : ApplyOut::Original;
}

// ---------------------------------------------------------------- F4: user 2's revive (P8 S28)
//
// [session2] user2_revive_manager = 0 | probe | 1, HOST (docs/research/s25/u0-resultados-s28.md §1, "F4"). The
// fault [V, S28 step 2]: user 2's ReviveLoadingReq 2362 (handler 0x298C490 -> .xtls 0x1048FFC0 ->
// ReviveLoading_SetPendingReload 0x2947E00) calls the reload setter 0x2BC4950 (six call sites, 0x2948313 ..
// 0x2949728); inside it the vf48 call at 0x2BC4C50 (return 0x2BC4C53) on the TRAVELLER's field gives the field
// OWNER's ServerSequencerStageManager, which the setter only takes a reference on ([mgr+8]) and resets with
// `mov byte [rbx+0x6A0],0` (0x2BC4C80): the host's manager drops to state 0 and stops evaluating stages until the
// host's own next load (TravelRevive "host manager +0x6A0 2 -> 0 ... HOST TOUCHED", 0 stage creations after it).
// F4 answers that vf48 with user 2's OWN manager (vtable and back pointer checked, as F3): its +0x6A0 (already 0 in
// the host's field) is the one reset, as the game does for the owner's own revive. Same hooked function as F3
// (ServerField::vf48's inline hook at its entry); nothing else is patched.
inline constexpr uint16_t kOpReviveLoadingReq = 2362;

enum class F4Out : uint8_t {
    NotHere,          // another return address, no subject, or F3 answered this Troc: not F4's
    OtherReq,         // user2_revive_manager = 0: another Req of user 2 in a field it does not own (the owner's is reset)
    OwnField,         // user 2 owns the field: the original is its own manager already
    Off,              // user2_revive_manager = 0 (vf48 hooked for another key): the original, the owner's +0x6A0 is zeroed
    Probe,            // probe: the original (logged: what 1 would return)
    Substitute,       // 1: user 2's own manager
    NoManager,        // 1: user 2's manager not readable: the original (ERR: the owner's +0x6A0 is zeroed)
    OtherProbe,       // probe, another Req of user 2 (not a 2362) in a field it does not own: the original
    OtherSubstitute,  // 1, another Req of user 2 in a field it does not own: user 2's own manager
    Count
};

struct F4In {
    Mode mode = Mode::Off;
    bool subject = false;    // inside a Req of user 2 (ExecuteTroc's subject)
    uint16_t opcode = 0;     // that Req's opcode
    uint32_t ret_rva = 0;    // vf48's return address - exe
    bool f3_armed = false;   // F3 answered this Troc's 2283 (it owns the second vf48)
    bool own_field = false;  // the field's owner is user 2
    bool mgr_ok = false;     // seq_manager_of(user 2's character) succeeded
};

// Decided by the field, not the opcode (review of 17c376b, 1): the setter only references the manager vf48 returns
// and zeroes its +0x6A0, whatever Req reached it, so ANY Req of user 2 there in a field it does not own resets the
// owner's manager (measured: a 2601 at user 2's login with the host still in its first load, 06:42:22 on 10-13).
inline F4Out decide_f4(const F4In& in) {
    if (!in.subject || in.ret_rva != kApplyVf48Ret || in.f3_armed) return F4Out::NotHere;
    if (in.own_field) return F4Out::OwnField;
    const bool other = in.opcode != kOpReviveLoadingReq;
    if (in.mode == Mode::Off) return other ? F4Out::OtherReq : F4Out::Off;
    if (in.mode == Mode::Probe) return other ? F4Out::OtherProbe : F4Out::Probe;
    if (!in.mgr_ok) return F4Out::NoManager;
    return other ? F4Out::OtherSubstitute : F4Out::Substitute;
}

inline const char* f4_out_name(F4Out o) {
    switch (o) {
        case F4Out::NotHere: return "not the setter's vf48 of a 2362";
        case F4Out::OtherReq:
            return "user2_revive_manager=0, another Req of user 2: the ORIGINAL (the owner's +0x6A0 is zeroed)";
        case F4Out::OwnField: return "user 2 owns the field: the original (its own manager)";
        case F4Out::Off: return "user2_revive_manager=0: the ORIGINAL (the owner's +0x6A0 is zeroed)";
        case F4Out::Probe: return "probe: the ORIGINAL (the owner's +0x6A0 is zeroed); 1 would return user 2's";
        case F4Out::Substitute: return "F4 ACTED: user 2's own manager (its +0x6A0 is zeroed, not the host's)";
        case F4Out::NoManager: return "user 2's manager NOT readable: the ORIGINAL (the owner's +0x6A0 is zeroed)";
        case F4Out::OtherProbe:
            return "probe, another Req of user 2: the ORIGINAL (the owner's +0x6A0 is zeroed); 1 would return user 2's";
        case F4Out::OtherSubstitute:
            return "F4 ACTED on another Req of user 2: user 2's own manager (its +0x6A0 is zeroed, not the host's)";
        default: return "?";
    }
}

// ---------------------------------------------------------------- F1: the guest's veto

// 0x655110(rcx, edx point, r8d index, r9d actor): r9d != 0 -> travel to that actor (0x778AB0, then 2283 with c);
// else a fast-travel point (edx != -1, r8d < count) -> 0xA9A860 (0x778AB0, then 2283). Returns al 1 = sent, 0 = not.
// Its three callers (0xD9E7D8, 0xDBC73D, 0xDBC768: the world map's travel) ignore the result and clear their
// pending request either way, so a 0 at the entry leaves no request behind and no loading started (0x778AB0 is what
// writes [P+0x15C] = 1 and starts the field-change machine).
enum class GuestAct : uint8_t { Native, NativeLogged, Veto, Count };

inline GuestAct decide_guest(Mode mode, bool remote_live) {
    if (mode == Mode::Off || !remote_live) return GuestAct::Native;
    return mode == Mode::On ? GuestAct::Veto : GuestAct::NativeLogged;
}

inline const char* guest_act_name(GuestAct a) {
    switch (a) {
        case GuestAct::Native: return "native";
        case GuestAct::NativeLogged: return "native (probe: logged only)";
        case GuestAct::Veto: return "VETOED (returned 0, nothing sent)";
        default: return "?";
    }
}

}  // namespace cdmp::session2::ftl
