#pragma once
// P8 session 27 (docs/research/s25/gancho-cuerda.md section 12, "Implementado (sondas)"): the pure logic of the
// read-only rope probes, with no game access, so the tests can run it. Build 1.0.0.2976 only. Nothing here decides
// anything for the game: the module (rope_probe.cpp) only counts, copies a few guarded reads and logs.
//
//   A. The tree bend (section 12.3): the thrower's client frame event pa::ClientFrameEventTreePulling (vtable
//      0x5584130, vf1 0x6D46D0) zeroes a timer of the character's movement controller ([[[actor+0x68]+0x40]+0xC8],
//      +0x63C); 0x8A30C0, called by the movement update 0x87A700 (0x87B2C6, under the actor's lock), runs it every
//      frame and pushes the far body of the pair at [mc+0x188]+0x50. Log-only hooks at both ENTRIES: who runs the
//      frame event, whether each character's controller (this client's and the remote ones) runs 0x8A30C0, and for
//      how long the timer is live and the pair there.
//   B. The grappling device (section 12.2): the docked "Game Mechanic" gimmick with the RemoteCatchPivot / HandTwist
//      sockets. From the sniffer's capture (no hook): the device gimmick events this client SENDS (3082 / 2758 with
//      the device hashes, 2609 of a known device) and every device 2005 it RECEIVES (key, owner +0x14, docked
//      character +0xEA).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace cdmp::session2::rpb {

// ---------------------------------------------------------------- build 1.0.0.2976: A, the tree bend

// pa::ClientFrameEventTreePulling::vf1 (this, &err, event, ?, &actor holder):
//   48 89 5C 24 08 | 48 89 74 24 10 | 57 | 48 83 EC 30 | 48 8B FA | 48 8B 44 24 60 (the 5th argument).
// Referenced by its vtable slot 0x5584138 only (and .pdata).
inline constexpr uintptr_t kTreePull = 0x6D46D0;
inline constexpr uint8_t kTreePullHead[23] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48,
                                              0x83, 0xEC, 0x30, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0x44, 0x24, 0x60};
inline constexpr uint32_t kTreePullBoundaries[] = {5, 10, 11, 15, 18, 23};
// The only patch size accepted: safetyhook's 5-byte jump (its 14-byte fallback is refused).
inline constexpr uint32_t kTreePullPatch[] = {5};
inline constexpr uintptr_t kTreePullVtbl = 0x5584130;
inline constexpr uintptr_t kTreePullSlot = 0x5584138;  // vf1
// The tree pull's per-frame apply 0x8A30C0 (mc, float dt in xmm1):
//   48 8B C4 mov rax,rsp | 57 push rdi | 48 81 EC 00 01 00 00 sub rsp,0x100 | C5 FA 10 91 3C 06 00 00 (the timer).
inline constexpr uintptr_t kPullApply = 0x8A30C0;
inline constexpr uint8_t kPullApplyHead[19] = {0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00,
                                               0x00, 0xC5, 0xFA, 0x10, 0x91, 0x3C, 0x06, 0x00, 0x00};
inline constexpr uint32_t kPullApplyBoundaries[] = {3, 4, 11, 19};
// The only patch size accepted: the 5-byte jump moves the first three whole instructions (11 bytes).
inline constexpr uint32_t kPullApplyPatch[] = {11};
// Its only caller, the movement update 0x87A700 (from cdmp_MovementController_UpdateByMode 0x88F4E0), under the
// actor's lock (cdmp_Actor_Lock at 0x87B2B8): vmovaps xmm1,xmm10 | mov rcx,rsi | call 0x8A30C0.
inline constexpr uintptr_t kPullApplyCall = 0x87B2BE;
inline constexpr uint8_t kPullApplyCallBytes[13] = {0xC4, 0xC1, 0x78, 0x28, 0xCA, 0x48, 0x8B,
                                                    0xCE, 0xE8, 0xF5, 0x7D, 0x02, 0x00};
inline constexpr uintptr_t kPullApplyReturn = 0x87B2CB;

// The movement controller (0x8A30C0's argument): [mc+0x68] the actor; +0x63C the tree-pull timer (-1 off, >= 0
// running, back to -1 after DAT_146cb8788); +0x638 the value applied this frame (-1 when it ends); +0x188 -> +0x50 the
// body pair: [0] near, [1] far (each +8 = the body object, +0x15 gone, +0x30 type, the object at -0x28), [4] the
// link (+0x3C / +0x3D both set).
inline constexpr uint32_t kMc_Actor = 0x68;
inline constexpr uint32_t kMc_Timer = 0x63C;
inline constexpr uint32_t kMc_Applied = 0x638;
inline constexpr uint32_t kMc_Pair = 0x188;
inline constexpr uint32_t kPairHolder_Pair = 0x50;
inline constexpr uint32_t kPair_Near = 0x00;
inline constexpr uint32_t kPair_Far = 0x08;
inline constexpr uint32_t kPair_Link = 0x20;
inline constexpr uint32_t kSide_Obj = 0x08;
inline constexpr uint32_t kObj_Gone = 0x15;
inline constexpr uint32_t kObj_Type = 0x30;
inline constexpr uint32_t kObj_Base = 0x28;  // the owning object is at obj - 0x28
inline constexpr uint32_t kLink_On0 = 0x3C;
inline constexpr uint32_t kLink_On1 = 0x3D;
// The actor -> its movement controller, as 0x6D46D0 reaches the timer: [[[actor+0x68]+0x40]+0xC8].
inline constexpr uint32_t kActor_Comps = 0x68;
inline constexpr uint32_t kComps_Control = 0x40;
inline constexpr uint32_t kControl_Mc = 0xC8;
inline constexpr uint32_t kActor_Key = 0x60;
inline constexpr uint32_t kActor_Control = 0xC8;  // u8: this client controls it

// The pair's state, packed in one u16 for the events (bit set = true).
enum PairBit : uint16_t {
    kPairHolder = 1,   // [mc+0x188] non-null
    kPairPair = 2,     // [holder+0x50] non-null
    kPairFar = 4,      // far side and its body object there
    kPairFarOk = 8,    // far body not gone and type > 4 (0x8A30C0's test)
    kPairNear = 16,    // near side and its body object there, not gone
    kPairLink = 32,    // the link there with +0x3C and +0x3D set
    kPairRead = 0x100  // the whole chain could be read (else a read failed on the way)
};
// 0x8A30C0 applies the pull only with all of these.
inline constexpr uint16_t kPairComplete = kPairHolder | kPairPair | kPairFar | kPairFarOk | kPairNear | kPairLink;
inline bool pair_complete(uint16_t bits) { return (bits & kPairComplete) == kPairComplete; }

// The timer: 0x8A30C0 runs it while 0 <= t <= DAT_146cb8788 (then it writes -1); off is -1. A value above the end
// is never left there by the game; the probe only tests the lower bound.
inline bool timer_running(float t) { return t >= 0.0f; }

// Whether 0x8A30C0 pushed this frame: +0x638 is written only when it pushes (and -1 at a run's end, possibly written
// again >= 0 in the same call), so a value carried over from an earlier frame is not a push. A push of exactly the
// same value as the frame before is not counted (dt varies; see section 13.4).
inline bool pull_applied(bool before_ok, float before, bool after_ok, float after) {
    if (!after_ok || after < 0.0f) return false;
    return !before_ok || before < 0.0f || after != before;
}

// ---------------------------------------------------------------- per character counts (lock-free, fixed size)

// Calls of 0x8A30C0 per PLAYER character key (top nibble 0xA; the NPCs and animals only go to one aggregate), from
// the game's threads (relaxed atomics, insert by CAS on an empty key). The module's thread reads it for the summary.
// Keys are server keys; run_mc is a local identity only (the movement controller of the run in progress).
struct KeyCount {
    std::atomic<uint32_t> key{0};
    std::atomic<uint8_t> ctl{0xFF};  // [actor+0xC8] when first seen (0xFF unread)
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> running_frames{0};  // calls with the timer running
    std::atomic<uint64_t> applied_frames{0};  // ... and a push this frame (pull_applied)
    std::atomic<uint32_t> runs{0};            // timer runs started
    std::atomic<uint8_t> running{0};          // a run in progress (start / end events)
    std::atomic<uintptr_t> run_mc{0};         // its movement controller (this process only)
    std::atomic<float> last_timer{-1.0f};     // the timer at the run's last frame (a reset starts a new run)
};

template <size_t N>
class KeyTable {
public:
    // The entry of `key` (inserted if new); nullptr when the table is full or key is 0.
    KeyCount* get(uint32_t key) {
        if (key == 0) return nullptr;
        size_t i = hash(key) & (N - 1);
        for (size_t n = 0; n < N; ++n, i = (i + 1) & (N - 1)) {
            uint32_t k = e_[i].key.load(std::memory_order_acquire);
            if (k == key) return &e_[i];
            if (k == 0) {
                uint32_t expect = 0;
                if (e_[i].key.compare_exchange_strong(expect, key, std::memory_order_acq_rel)) return &e_[i];
                if (expect == key) return &e_[i];
            }
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    const KeyCount* find(uint32_t key) const {
        if (key == 0) return nullptr;
        size_t i = hash(key) & (N - 1);
        for (size_t n = 0; n < N; ++n, i = (i + 1) & (N - 1)) {
            const uint32_t k = e_[i].key.load(std::memory_order_acquire);
            if (k == key) return &e_[i];
            if (k == 0) return nullptr;
        }
        return nullptr;
    }
    size_t used() const {
        size_t n = 0;
        for (const KeyCount& e : e_) n += e.key.load(std::memory_order_relaxed) != 0;
        return n;
    }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }
    const KeyCount& at(size_t i) const { return e_[i]; }
    size_t index(const KeyCount* p) const { return static_cast<size_t>(p - e_); }
    static constexpr size_t size() { return N; }

private:
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static size_t hash(uint32_t k) { return static_cast<size_t>((k * 2654435761u) >> 7); }
    KeyCount e_[N];
    std::atomic<uint64_t> full_{0};
};

// ---------------------------------------------------------------- B, the grappling device (sniffer)

inline constexpr size_t kHeader = 5;  // u16 opcode | u8 slot | u16 length
inline constexpr uint16_t kOpActorLoad = 2005;
inline constexpr uint16_t kOpRemoveActor = 2516;
inline constexpr uint16_t kOpGimmickFrameEvent = 3082;
inline constexpr uint16_t kOpUpdateGimmick = 2758;
inline constexpr uint16_t kOpGimmickBranch = 2609;
inline constexpr uint16_t kOpStartActionAck = 2991;
inline constexpr uint16_t kOpRemoteCatchAck = 2763;

// The device's gimmick events (S25 / S27 captures, section 12.2): the hash at +4 of 3082 / 2758.
enum class DevEvent : uint8_t { Deploy, Catch, E9278, EA55A, Release, Stow, Count };
inline const char* dev_event_name(DevEvent e) {
    switch (e) {
        case DevEvent::Deploy: return "deploy 003ECC59";
        case DevEvent::Catch: return "catch DA667CC9";
        case DevEvent::E9278: return "92786A19";
        case DevEvent::EA55A: return "A55A59FF";
        case DevEvent::Release: return "release 550D1ED6";
        case DevEvent::Stow: return "stow EDFD0EBB";
        case DevEvent::Count: break;
    }
    return "?";
}
inline std::optional<DevEvent> dev_event(uint32_t hash) {
    switch (hash) {
        case 0x003ECC59: return DevEvent::Deploy;
        case 0xDA667CC9: return DevEvent::Catch;
        case 0x92786A19: return DevEvent::E9278;
        case 0xA55A59FF: return DevEvent::EA55A;
        case 0x550D1ED6: return DevEvent::Release;
        case 0xEDFD0EBB: return DevEvent::Stow;
        default: return std::nullopt;
    }
}

inline uint32_t le32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

// A gimmick request this client sent (3082 / 2758): +0 the gimmick, +4 the event hash, +0x14 the character.
struct GimmickReq {
    uint32_t gimmick = 0;
    uint32_t hash = 0;
    uint32_t character = 0;
};
inline std::optional<GimmickReq> parse_gimmick_req(std::span<const uint8_t> pl) {
    if (pl.size() < 0x18) return std::nullopt;
    return GimmickReq{le32(&pl[0]), le32(&pl[4]), le32(&pl[0x14])};
}

// A 2005 ActorLoadDataAck: +3 the actor key, +0x14 the owner (the field owner's user in option D), +0xEA and +0x104
// the docked parent (the same key twice in the S27 devices). A device: both socket names in it.
inline constexpr size_t k2005_Key = 3;
inline constexpr size_t k2005_Owner = 0x14;
inline constexpr size_t k2005_Parent = 0xEA;
inline constexpr size_t k2005_Parent2 = 0x104;
struct Load2005 {
    uint32_t key = 0;
    uint32_t owner = 0;
    uint32_t parent = 0;  // 0: unknown (the two copies differ or the payload is short)
    bool device = false;
};
inline bool contains(std::span<const uint8_t> pl, const char* s) {
    const size_t n = std::strlen(s);
    if (n == 0 || pl.size() < n) return false;
    for (size_t i = 0; i + n <= pl.size(); ++i) {
        if (pl[i] == static_cast<uint8_t>(s[0]) && std::memcmp(&pl[i], s, n) == 0) return true;
    }
    return false;
}
inline std::optional<Load2005> parse_2005(std::span<const uint8_t> pl) {
    if (pl.size() < k2005_Owner + 4) return std::nullopt;
    Load2005 r;
    r.key = le32(&pl[k2005_Key]);
    r.owner = le32(&pl[k2005_Owner]);
    if (pl.size() >= k2005_Parent2 + 4 && le32(&pl[k2005_Parent]) == le32(&pl[k2005_Parent2])) {
        r.parent = le32(&pl[k2005_Parent]);
    }
    r.device = contains(pl, "RemoteCatchPivot") && contains(pl, "HandTwist");
    return r;
}

// A player character key (class 0xA in the top nibble).
inline bool player_key(uint32_t k) { return (k >> 28) == 0xA; }

// The devices this client knows (2005 received or an event sent), fixed size, the module's thread only.
struct Device {
    uint32_t key = 0;
    uint32_t owner = 0;
    uint32_t parent = 0;      // from the 2005
    uint32_t character = 0;   // from the events this client sent (+0x14)
    bool got_2005 = false;
    bool removed = false;
    uint64_t first_2005_us = 0;
    uint64_t first_event_us = 0, last_event_us = 0;
    // One UpdateGimmickReq 2758 per occurrence of each event (S27: deploy / 92786A19 / A55A59FF / catch / release /
    // stow 13 / 13 / 8 / 8 / 8 / 8 for 8 ropes); the 3082 copies (several per event) only counted.
    uint32_t sent[static_cast<size_t>(DevEvent::Count)] = {};
    uint32_t sent_3082 = 0;
    uint32_t sent_branch = 0;  // 2609 of this device
};
template <size_t N>
class DeviceTable {
public:
    Device* find(uint32_t key) {
        for (size_t i = 0; i < n_; ++i) {
            if (d_[i].key == key) return &d_[i];
        }
        return nullptr;
    }
    Device* get(uint32_t key) {
        if (Device* d = find(key)) return d;
        if (n_ == N) {
            ++full_;
            return nullptr;
        }
        d_[n_] = Device{};
        d_[n_].key = key;
        return &d_[n_++];
    }
    size_t size() const { return n_; }
    const Device& at(size_t i) const { return d_[i]; }
    uint64_t full() const { return full_; }

private:
    Device d_[N];
    size_t n_ = 0;
    uint64_t full_ = 0;
};

}  // namespace cdmp::session2::rpb
