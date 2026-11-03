#pragma once
// P8 phase 2 (docs/research/pendientes/p8-observador-escenas.md, "Fase 2"): the
// pure logic of the GUEST's read-only probe [session2] guest_stage_diag=1 and
// the correlation token both PCs print. No game access: the tests run it on the
// packets the captures kept.
//
// What the probe watches on the guest's client (build 1.0.0.2976, static
// analysis of the document's §13):
//   2661 PrepareSequencerAck  handler 0xC05BB0: u8 kind | u32 addressed actor |
//                             86-byte play data (u32 owner actor +0, u64 seq +8)
//                             | n x 11-byte bindings (u32 node, u32 actor, u8
//                             type, 2 x u8). The manager is NOT chosen from the
//                             payload: 0x17FA6C0 -> ClientTransformSyncActor-
//                             Component::vf140 (0x9F8560) returns the client's
//                             one current ClientField; ClientField::vf48 is
//                             0x9A5BE0 = the stage manager of the character of
//                             the field's owner user (field+0x180, which the
//                             host's 2174 rewrite makes user 2 on the guest).
//   2981 / 2810 / 2264 / 2322 the same routing (0xC07746, 0xC0659B, 0xC0E236,
//                             0xB87A64).
//   0xA9CA40                  creates the instance in that manager: every
//                             binding of type 4/5 must name an actor present on
//                             the client (else the whole sequencer is dropped,
//                             0xA9CE81) and a sequencer id already in the
//                             manager (+0x170 map) is refused.
//   0x9ED290                  ClientSequencerPlayDataActorComponent::vf13 (the
//                             NPC side): looks the instance up in the manager of
//                             the actor comp+0x2D8 ([[[A+0x68]+0xA8]+0x18]+0x148),
//                             the sequencer owner that the NPC's 2005 names.
// So on the guest a 2661 of the host's sequencer would sit in user 2's own
// manager while its NPCs search the host character's manager: classify_route()
// names that case.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "mod/session2/observer_stage_logic.h"

namespace cdmp::session2::gsd {

using obs::le32;
using obs::le64;

// ---------------------------------------------------------------- payloads

struct WireBinding {
    uint32_t node = 0;
    uint32_t actor = 0;
    uint8_t type = 0;  // 0xA9CB87: 4 or 5 = an actor the client must already have
    uint8_t b1 = 0, b2 = 0;
};

struct Prepare {
    uint8_t kind = 0;
    uint32_t actor = 0;        // addressed actor (payload +1)
    uint32_t owner_actor = 0;  // play data +0
    uint64_t seq = 0;          // play data +8
    std::vector<WireBinding> bindings;
};

inline std::optional<Prepare> parse_prepare(std::span<const uint8_t> p) {
    if (p.size() < obs::kPreparePrefix || (p.size() - obs::kPreparePrefix) % obs::kBindingWire != 0) return std::nullopt;
    Prepare r;
    r.kind = p[0];
    r.actor = le32(&p[1]);
    r.owner_actor = le32(&p[5]);
    r.seq = le64(&p[9]);
    const size_t n = (p.size() - obs::kPreparePrefix) / obs::kBindingWire;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* b = &p[obs::kPreparePrefix + i * obs::kBindingWire];
        r.bindings.push_back({le32(b), le32(b + 4), b[8], b[9], b[10]});
    }
    return r;
}

// Bindings the client must resolve before it creates the instance (types 4/5).
inline size_t required_actors(const Prepare& p) {
    return static_cast<size_t>(
        std::count_if(p.bindings.begin(), p.bindings.end(), [](const WireBinding& b) { return b.type == 4 || b.type == 5; }));
}

// StageBranchStateAck 2981: u8 kind | u32 actor | u64 seq | u32 value | rest
// (29 or 37 bytes in the captures). `value` is the u32 the server's serializer
// writes from [R9] (0x2A4F41F), the one the host's line prints.
struct Branch {
    uint8_t kind = 0;
    uint32_t actor = 0;
    uint64_t seq = 0;
    uint32_t value = 0;
    size_t size = 0;
};
inline constexpr size_t kBranchMin = 17;
inline std::optional<Branch> parse_branch(std::span<const uint8_t> p) {
    if (p.size() < kBranchMin) return std::nullopt;
    return Branch{p[0], le32(&p[1]), le64(&p[5]), le32(&p[13]), p.size()};
}

// ---------------------------------------------------------------- correlation

// The same text on the host (observer_stage.cpp, from the serializer's
// arguments) and on the guest (from the bytes its handler received), so a
// grep of one token finds both ends: "corr=2981:A0100001:0x164:0x100".
// 2661: value = number of bindings; 2810: the u32 after the sequencer; 2322:
// seq 0, value 0.
inline std::string corr_token(uint16_t opcode, uint32_t actor, uint64_t seq, uint32_t value) {
    return std::format("corr={}:{:08X}:0x{:X}:0x{:X}", opcode, actor, seq, value);
}

// FNV-1a 64 over a byte span: the guest prints it for every 2661 it handles so
// two sessions (or the capture) can be matched byte for byte.
inline uint64_t fnv1a64(std::span<const uint8_t> b) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (uint8_t c : b) {
        h ^= c;
        h *= 0x100000001B3ull;
    }
    return h;
}

// ---------------------------------------------------------------- routing verdict

struct RouteFacts {
    uint64_t manager = 0;          // what the handler's vf48 returned (0 = no field context)
    uint32_t manager_actor = 0;    // [[manager+0x18]+0x60]: the character that manager belongs to
    uint32_t field_owner = 0;      // [field+0x180] (the client's view of the owner user)
    uint32_t seq_owner_actor = 0;  // play data +0 (2661) / the addressed actor (2981, 2810)
    uint64_t seq_owner_manager = 0;  // [[[A+0x68]+0xA8]+0x18] of that actor, 0 = unknown
};

enum class Route : uint8_t {
    NoManager,         // no field context: the handler did nothing
    OwnersManager,     // the manager IS the sequencer owner's: what the NPCs search (native owner case)
    ObserverManager,   // another character's manager: the NPCs bound by this sequencer will not find it
};

inline Route classify_route(const RouteFacts& f) {
    if (!f.manager) return Route::NoManager;
    if (f.seq_owner_manager && f.seq_owner_manager == f.manager) return Route::OwnersManager;
    if (!f.seq_owner_manager && f.seq_owner_actor && f.seq_owner_actor == f.manager_actor) return Route::OwnersManager;
    return Route::ObserverManager;
}

inline const char* route_text(Route r) {
    switch (r) {
        case Route::NoManager: return "NO MANAGER (no field context: ignored by the handler)";
        case Route::OwnersManager: return "the sequencer OWNER's manager (where its NPCs look: native case)";
        case Route::ObserverManager:
            return "ANOTHER character's manager (the field owner's in this client's view): the NPCs this sequencer "
                   "binds look in the owner actor's manager (0x9ED290) and will NOT find it";
    }
    return "?";
}

// 0xA9CA40's result ([rbp-0x50] of 0xC05BB0 after the call): 0 = created; the
// error codes are globals of the game, read at run time and passed in.
enum class Result : uint8_t { Created, MissingBoundActor, Duplicate, Other };
inline Result classify_result(uint32_t code, uint32_t duplicate_code, uint32_t missing_code) {
    if (code == 0) return Result::Created;
    if (missing_code && code == missing_code) return Result::MissingBoundActor;
    if (duplicate_code && code == duplicate_code) return Result::Duplicate;
    return Result::Other;
}
inline const char* result_text(Result r) {
    switch (r) {
        case Result::Created: return "created (instance added to the manager's +0x170 map)";
        case Result::MissingBoundActor:
            return "REJECTED: a bound actor (type 4/5) is not on this client (0xA9CE81, the instance is destroyed)";
        case Result::Duplicate: return "REJECTED: that sequencer id is already in this manager (0xAA6E90)";
        case Result::Other: return "REJECTED: other error";
    }
    return "?";
}

// ---------------------------------------------------------------- managers seen and M+0x231

struct ManagerInfo {
    uint64_t ptr = 0;
    uint32_t actor = 0;    // [[ptr+0x18]+0x60]
    int ready = -1;        // last u8 M+0x231 seen (-1 unknown)
    uint64_t first_us = 0, last_us = 0;
    uint32_t prepares = 0, branches = 0, deletes = 0, milestones = 0, npc_lookups = 0;
};

struct ReadyChange {
    bool changed = false;
    int before = -1, after = -1;
};

// The managers this client used (drain thread only). Bounded: the oldest is
// forgotten past `cap`.
class ManagerBook {
public:
    explicit ManagerBook(size_t cap = 64) : cap_(cap) {}

    ManagerInfo& touch(uint64_t ptr, uint32_t actor, uint64_t t_us) {
        for (ManagerInfo& m : v_) {
            if (m.ptr == ptr) {
                if (actor) m.actor = actor;
                m.last_us = t_us;
                return m;
            }
        }
        if (v_.size() >= cap_) v_.erase(v_.begin());
        ManagerInfo m;
        m.ptr = ptr;
        m.actor = actor;
        m.first_us = m.last_us = t_us;
        v_.push_back(m);
        return v_.back();
    }

    // A read of u8 ptr+0x231 (`value` < 0: unreadable). Records it and says
    // whether it differs from the last value of that manager.
    ReadyChange ready(uint64_t ptr, uint32_t actor, int value, uint64_t t_us) {
        ManagerInfo& m = touch(ptr, actor, t_us);
        ReadyChange c;
        c.before = m.ready;
        c.after = value;
        c.changed = value >= 0 && m.ready != value;
        if (value >= 0) m.ready = value;
        return c;
    }

    const ManagerInfo* find(uint64_t ptr) const {
        for (const ManagerInfo& m : v_) {
            if (m.ptr == ptr) return &m;
        }
        return nullptr;
    }
    // The manager the stage handlers routed to last (the field owner's in this client's view).
    const ManagerInfo* last_routed() const { return last_routed_ ? find(last_routed_) : nullptr; }
    void set_routed(uint64_t ptr) { last_routed_ = ptr; }
    const std::vector<ManagerInfo>& all() const { return v_; }

private:
    size_t cap_;
    std::vector<ManagerInfo> v_;
    uint64_t last_routed_ = 0;
};

// ---------------------------------------------------------------- the NPC side (0x9ED290)

struct NpcBind {
    uint32_t npc = 0;
    uint32_t seq_owner = 0;  // comp+0x2D8
    uint64_t seq_key = 0;    // comp+0x2E0 (low 29 bits non-zero = has a sequencer)
    uint32_t entries = 0, res8_pass = 0, res8_exit = 0, owner_found = 0, instance_found = 0;
    uint64_t last_res8 = 0;
    uint64_t owner_manager = 0;      // [[[A+0x68]+0xA8]+0x18] of comp+0x2D8's actor (0x9ED453)
    bool owner_manager_routed = false;  // == the manager the stage handlers route to
    uint64_t first_us = 0, last_us = 0;
};

enum class NpcStep : uint8_t { Entry, Res8, OwnerFound, Instance };

// Drain thread only; bounded (new NPCs past `cap` are counted, not kept).
class NpcTracker {
public:
    explicit NpcTracker(size_t cap = 4096) : cap_(cap) {}

    NpcBind* step(uint32_t npc, NpcStep s, uint64_t t_us) {
        if (!npc) return nullptr;
        auto it = m_.find(npc);
        if (it == m_.end()) {
            if (m_.size() >= cap_) {
                ++overflow_;
                return nullptr;
            }
            NpcBind b;
            b.npc = npc;
            b.first_us = t_us;
            it = m_.emplace(npc, b).first;
        }
        NpcBind& b = it->second;
        b.last_us = t_us;
        switch (s) {
            case NpcStep::Entry: ++b.entries; break;
            case NpcStep::Res8: break;  // counted by res8()
            case NpcStep::OwnerFound: ++b.owner_found; break;
            case NpcStep::Instance: ++b.instance_found; break;
        }
        return &b;
    }

    void res8(uint32_t npc, uint64_t value, uint64_t t_us) {
        NpcBind* b = step(npc, NpcStep::Res8, t_us);
        if (!b) return;
        b->last_res8 = value;
        if (res8_passes(value)) {
            ++b->res8_pass;
        } else {
            ++b->res8_exit;
        }
    }

    // 0x9ED401: `cmp dword [rbp+0x30],0 ; je exit ; cmp dword [rbp+0x34],-1 ; je exit`.
    static constexpr bool res8_passes(uint64_t v) {
        return static_cast<uint32_t>(v) != 0 && static_cast<uint32_t>(v >> 32) != 0xFFFFFFFFu;
    }

    struct Summary {
        size_t npcs = 0, with_owner = 0, passed_res8 = 0, exited_res8 = 0, owner_found = 0, instance = 0,
               owner_manager_routed = 0;
        std::map<uint32_t, size_t> owners;  // comp+0x2D8 -> NPCs
    };
    Summary summary() const {
        Summary s;
        for (const auto& [k, b] : m_) {
            ++s.npcs;
            if (b.seq_owner) {
                ++s.with_owner;
                ++s.owners[b.seq_owner];
            }
            if (b.res8_pass) ++s.passed_res8;
            if (b.res8_exit && !b.res8_pass) ++s.exited_res8;
            if (b.owner_found) ++s.owner_found;
            if (b.instance_found) ++s.instance;
            if (b.owner_manager_routed) ++s.owner_manager_routed;
        }
        return s;
    }
    const NpcBind* get(uint32_t npc) const {
        auto it = m_.find(npc);
        return it == m_.end() ? nullptr : &it->second;
    }
    NpcBind* get_mut(uint32_t npc) {
        auto it = m_.find(npc);
        return it == m_.end() ? nullptr : &it->second;
    }
    uint64_t overflow() const { return overflow_; }
    size_t size() const { return m_.size(); }

private:
    size_t cap_;
    std::map<uint32_t, NpcBind> m_;
    uint64_t overflow_ = 0;
};

}  // namespace cdmp::session2::gsd
