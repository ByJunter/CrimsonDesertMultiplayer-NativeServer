#pragma once
// S35 fase A (docs/research/s25/s35-fase-a.md; AUDITORIA.md §1 "Cambios obligatorios", §6 "Problemas adicionales"
// 3; INFORME-A1 "R2"), build 1.0.0.2976. Pure: no game memory is read here except through the caller's `read`
// policy (protected reads), nothing is written.
//
// The safe publication invariant, as an ADMISSION rule: no exit of observer_hold / observer_hold_family announces to
// an observer that does not control it an actor whose state is not proven complete. Every exit is evaluated, not
// only Timeout -> Keep: the releases Native (child_hold.cpp npc_release_step), Timeout, Restore1 / ByControl (read
// again), the family releases (MaxAge, Orphan, OrphanTimeout, AfterParent, WithParent), "no room: released anyway",
// and the native exits of a full table. Three CC states, never two: Unknown (could not read it) is not Absent
// (proven: the component slot reads 0). A parent present at the observer does not prove its layer usable: the
// dependency is read again (its own state), and a parent withheld or retained in the same batch blocks its children.
#include <cstddef>
#include <cstdint>
#include <optional>

namespace cdmp::session2::pubg {

enum class Mode : uint8_t { Off = 0, Probe = 1, On = 2 };  // u2f::Mode's values (session2.cpp parses the key)

enum class Cc : uint8_t { Unknown, Absent, Present };
struct CcRead {
    Cc state = Cc::Unknown;
    uint8_t restore = 0;     // Present only: [[cc+0x40]+0x30]
    uintptr_t slot = 0;      // what the components slot held (log only)
};
// The offsets of the CC chain (child_hold.h hold::, build 2976): actor+0x68 -> components, +0x40 -> CharacterControl
// (0x296CEFE), cc+0x08 -> its actor (0x2A78CCB), cc+0x40 -> restore state (0x2A78DAF), +0x30 restore byte (0x2328894).
struct CcOffsets {
    uint32_t components = 0x68, cc = 0x40, cc_actor = 0x08, cc_restore = 0x40, restore_byte = 0x30;
};
// read(address, out, size) -> bool: a protected read. Absent ONLY when the components pointer is read and is
// plausible and its CC slot reads exactly 0. Anything else that fails (unreadable, a slot whose +8 is not this actor,
// no restore state) is Unknown: "could not read it" never becomes "has no CC".
template <class Read>
CcRead read_cc(Read&& read, uintptr_t actor, const CcOffsets& o = {}) noexcept {
    CcRead r;
    if (actor < 0x10000) return r;
    uintptr_t comps = 0, slot = 0, back = 0, state = 0;
    if (!read(actor + o.components, &comps, sizeof comps) || comps < 0x10000) return r;
    if (!read(comps + o.cc, &slot, sizeof slot)) return r;
    r.slot = slot;
    if (!slot) {
        r.state = Cc::Absent;
        return r;
    }
    if (slot < 0x10000 || !read(slot + o.cc_actor, &back, sizeof back) || back != actor) return r;
    if (!read(slot + o.cc_restore, &state, sizeof state) || state < 0x10000) return r;
    uint8_t b = 0;
    if (!read(state + o.restore_byte, &b, sizeof b)) return r;
    r.state = Cc::Present;
    r.restore = b;
    return r;
}

// The state of the actor's dependency (its parent: the +0xAC key the 2005 names, else its hierarchy's).
enum class Dep : uint8_t {
    None,      // no parent
    Ok,        // the parent is admissible for this observer now (or proven gone: the native orphan route)
    Withheld,  // the parent has an entry withheld from this observer (any table)
    InBatch,   // the parent was refused in this same release batch (parents are decided first)
    Unsafe,    // the parent's own state is incomplete for this observer (restore 0, not its controller)
    Unknown,   // the parent link or the parent could not be read
};
struct Facts {
    std::optional<uintptr_t> actor;          // nullopt: unreadable; 0: not in the SAM (gone)
    std::optional<uint32_t> controller;      // [actor+0x90]
    uint32_t observer_key = 0;               // the observer's user key (0: unknown)
    CcRead cc;
    Dep dep = Dep::None;
    // The users' keys (0 = unknown). A controller that is neither (the server's own NPC, 0, or a user nobody knows)
    // is what child_hold's prepare leaves native (HoldWhy::CtrlNotUser): the gate follows that same rule (review 4).
    uint32_t host_key = 0, user2_key = 0;
};
// Controlled by one of the two users? Both keys unknown: assumed so (fail closed).
inline bool user_controlled(uint32_t controller, uint32_t host_key, uint32_t user2_key) noexcept {
    if (!host_key && !user2_key) return true;
    return controller && (controller == host_key || controller == user2_key);
}
enum class Verdict : uint8_t {
    Publish,
    Gone,                // not in the SAM: nothing to announce (the entry goes, nothing is published)
    ActorUnknown,        // retained
    ControllerUnknown,   // retained
    Restore0,            // retained: CC present, restore 0, the observer is not its controller
    CcUnknown,           // retained: the CC could not be read (not proven absent)
    Dependency,          // retained: its parent is withheld / unsafe / unknown
    ParentInBatch,       // retained: its parent was refused in this batch
    Count
};
inline const char* verdict_name(Verdict v) noexcept {
    switch (v) {
        case Verdict::Publish: return "publish";
        case Verdict::Gone: return "gone";
        case Verdict::ActorUnknown: return "actor unreadable";
        case Verdict::ControllerUnknown: return "controller unreadable";
        case Verdict::Restore0: return "restore 0 (observer is not its controller)";
        case Verdict::CcUnknown: return "CC unreadable (not proven absent)";
        case Verdict::Dependency: return "its parent is withheld, unsafe or unreadable";
        case Verdict::ParentInBatch: return "its parent was refused in this release";
        default: return "?";
    }
}
inline bool retained(Verdict v) noexcept { return v != Verdict::Publish && v != Verdict::Gone; }
// The admission rule (review 9: one rule). The observer that controls the actor ALWAYS gets it (its own client
// builds its state), whatever its parent. Any other observer gets it only with its CC present and restore != 0, or
// with the CC proven absent, or when no user controls it (the native rule of prepare, CtrlNotUser); in every case
// only when its dependency is admissible too (a parent refused in this batch, withheld, unsafe or unreadable).
inline Verdict admit(const Facts& f) noexcept {
    if (!f.actor) return Verdict::ActorUnknown;
    if (!*f.actor) return Verdict::Gone;
    if (!f.controller) return Verdict::ControllerUnknown;
    if (f.observer_key && *f.controller == f.observer_key) return Verdict::Publish;
    if (f.dep == Dep::InBatch) return Verdict::ParentInBatch;
    if (user_controlled(*f.controller, f.host_key, f.user2_key)) {
        if (f.cc.state == Cc::Unknown) return Verdict::CcUnknown;
        if (f.cc.state == Cc::Present && f.cc.restore == 0) return Verdict::Restore0;
    }
    if (f.dep == Dep::Withheld || f.dep == Dep::Unsafe || f.dep == Dep::Unknown) return Verdict::Dependency;
    return Verdict::Publish;
}
// The parent's own admissibility for the same observer (one level: a parent without CC counts as Ok here; its own
// parent was checked when it was published).
struct ParentFacts {
    std::optional<uintptr_t> actor;      // nullopt unreadable, 0 gone
    std::optional<uint32_t> controller;
    CcRead cc;
    bool withheld = false;               // an entry withheld from this observer
    bool in_batch = false;               // refused in this batch
};
// Review 4: a parent no user controls (CtrlNotUser) is one the gate itself leaves native: it is Ok as a dependency,
// whatever its CC reads (else gimmick pieces of server-controlled parents became invisible).
inline Dep parent_dep(bool has_link, uint32_t parent, const ParentFacts& p, uint32_t observer_key,
                      uint32_t host_key = 0, uint32_t user2_key = 0) noexcept {
    if (!has_link) return Dep::Unknown;  // the link could not be read
    if (!parent) return Dep::None;
    if (p.in_batch) return Dep::InBatch;
    if (p.withheld) return Dep::Withheld;
    if (!p.actor) return Dep::Unknown;
    if (!*p.actor) return Dep::Ok;  // gone: the client's native orphan route, as without the mod
    if (!p.controller) return Dep::Unknown;
    if (observer_key && *p.controller == observer_key) return Dep::Ok;
    if (!user_controlled(*p.controller, host_key, user2_key)) return Dep::Ok;
    if (p.cc.state == Cc::Unknown) return Dep::Unknown;
    if (p.cc.state == Cc::Present && p.cc.restore == 0) return Dep::Unsafe;
    return Dep::Ok;
}

// What happens to a refused release: retained in its table (bounded), or dropped WITHOUT being published once its
// retention is over (or when its table has no room). Never published by expiry.
enum class Retention : uint8_t { Retain, DropExpired, DropNoRoom };
inline constexpr uint64_t kRetryMs = 1000;  // a retained entry is decided again at most this often
inline Retention retention(uint64_t unsafe_since_ms, uint64_t now_ms, uint64_t retain_max_ms, bool room) noexcept {
    if (unsafe_since_ms && now_ms >= unsafe_since_ms && now_ms - unsafe_since_ms >= retain_max_ms) {
        return Retention::DropExpired;
    }
    return room ? Retention::Retain : Retention::DropNoRoom;
}
// The native exits (prepare): a key the gate would have let through natively for want of room in its table is
// withheld WITHOUT an entry (invisible to that observer until the engine announces it again), never announced.
inline bool withhold_without_entry(Mode m) noexcept { return m == Mode::On; }

}  // namespace cdmp::session2::pubg
