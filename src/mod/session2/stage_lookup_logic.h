#pragma once
// P8 session 12/13, the shopkeepers (docs/research/pendientes/p8-impl-tenderos-c1.md): pure logic shared by the
// guest's C1 redirect (guest_x_interaction.h) and the host's shop probe (shop_probe.h). No game call, no allocation.
//
// 1. A read-only replica of the sequencer lookup 0x1F6C190(mgr, &out, u64 id) (build 1.0.0.2976, common to the
//    client's and the server's stage managers; read with capstone, p8-impl-tenderos-c1.md §2):
//      [mgr+0x10C] u32 element count (0 -> not found)
//      [mgr+0x108] u32 bucket count  (0 -> not found)
//      h = 0x1364780(&id, 8)          lookup3 hashlittle over the 8 bytes of the u64 id, initval 0xC5EDE
//                                     (lea ebx,[rdx-0x2145E233]: a = b = c = 0xDEBA1DCD + 8; jump-table case 8:
//                                     b += k[1], a += k[0], then the final mix; returns c)
//      bucket = [mgr+0x118] + (h % buckets) * 0x100: u32 n at +0, then n pairs {u32 hash, u32 index} from +8
//      element = [[mgr+0x120] + index * 8]: its u64 [+8] must be the id; the instance is [element+0x10]
//      an instance whose byte [+0x15] is set counts as not found (0x1F6C240: the "dead" flag)
//    The game takes [mgr+8]'s lock around it; the replica reads without it (SEH reads in the hooks): a table being
//    rebuilt at that moment gives "not found" or a stale answer, never a write.
// 2. IdBook: "the first K events of each id" for the log lines, lock-free (any game thread).

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cdmp::session2::isl {

// ---------------------------------------------------------------- the id hash (0x1364780 for 8 bytes)

inline constexpr uint32_t kIdHashInit = 0xC5EDE;  // 0xDEBA1DCD - 0xDEADBEEF (lookup3's base)
inline constexpr uint32_t rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

// lookup3 hashlittle of the 8 little-endian bytes of id with initval 0xC5EDE: no CRT, no loop (hook-safe).
inline constexpr uint32_t hash_id(uint64_t id) {
    uint32_t a = 0xDEADBEEFu + 8u + kIdHashInit, b = a, c = a;
    b += static_cast<uint32_t>(id >> 32);
    a += static_cast<uint32_t>(id);
    c ^= b; c -= rotl(b, 14);
    a ^= c; a -= rotl(c, 11);
    b ^= a; b -= rotl(a, 25);
    c ^= b; c -= rotl(b, 16);
    a ^= c; a -= rotl(c, 4);
    b ^= a; b -= rotl(a, 14);
    c ^= b; c -= rotl(b, 24);
    return c;
}

// ---------------------------------------------------------------- the table walk

inline constexpr uint32_t kMgr_Buckets = 0x108, kMgr_Count = 0x10C, kMgr_BucketArray = 0x118, kMgr_Elements = 0x120;
inline constexpr uint32_t kBucketStride = 0x100, kBucketPairs = (kBucketStride - 8) / 8;  // 31 pairs fit a bucket
inline constexpr uint32_t kElem_Id = 0x08, kElem_Inst = 0x10, kInst_Dead = 0x15;
inline constexpr uint32_t kMaxIndex = 1u << 20;  // a sane element index (the game has hundreds)

struct Lookup {
    bool read_ok = false;  // every read the walk needed succeeded
    bool found = false;    // an element with the id and a live instance (what 0x1F6C190 returns non-null)
    bool dead = false;     // the element exists but its instance is flagged dead (+0x15): 0x1F6C190 returns null
    uintptr_t inst = 0;    // the instance (found or dead)
    uint32_t hash = 0;
    uint32_t count = 0;    // [mgr+0x10C]
};

// Rd: an object with `template <class T> bool rd(uintptr_t addr, T& out) const` (SEH reads in the mod, a fake
// memory in the tests). Mirrors 0x1F6C190 step by step.
template <class Rd>
Lookup find_stage(const Rd& m, uintptr_t mgr, uint64_t id) {
    Lookup r;
    r.hash = hash_id(id);
    if (!mgr) return r;
    uint32_t count = 0, buckets = 0;
    if (!m.rd(mgr + kMgr_Count, count)) return r;
    r.count = count;
    if (count == 0) {
        r.read_ok = true;
        return r;
    }
    if (!m.rd(mgr + kMgr_Buckets, buckets)) return r;
    if (buckets == 0) {
        r.read_ok = true;
        return r;
    }
    uintptr_t barr = 0, elems = 0;
    if (!m.rd(mgr + kMgr_BucketArray, barr) || !m.rd(mgr + kMgr_Elements, elems) || !barr || !elems) return r;
    const uintptr_t bucket = barr + static_cast<uintptr_t>(r.hash % buckets) * kBucketStride;
    uint32_t n = 0;
    if (!m.rd(bucket, n)) return r;
    if (n > kBucketPairs) n = kBucketPairs;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t h = 0, idx = 0;
        if (!m.rd(bucket + 8 + 8 * static_cast<uintptr_t>(i), h)) return r;
        if (h != r.hash) continue;
        if (!m.rd(bucket + 0xC + 8 * static_cast<uintptr_t>(i), idx) || idx >= kMaxIndex) return r;
        uintptr_t elem = 0;
        uint64_t eid = 0;
        if (!m.rd(elems + 8 * static_cast<uintptr_t>(idx), elem) || !elem || !m.rd(elem + kElem_Id, eid)) return r;
        if (eid != id) continue;
        uintptr_t inst = 0;
        if (!m.rd(elem + kElem_Inst, inst)) return r;
        r.read_ok = true;
        r.inst = inst;
        if (!inst) return r;
        uint8_t dead = 0;
        if (!m.rd(inst + kInst_Dead, dead)) {
            r.read_ok = false;
            return r;
        }
        r.dead = dead != 0;
        r.found = !r.dead;
        return r;
    }
    r.read_ok = true;
    return r;
}

// ---------------------------------------------------------------- the first K events of each id

// A fixed table of ids (0 = a free slot), claimed with a CAS; `take` says whether this event is among the first
// `k` of its id. A full table answers false (the caller's window limiter and counters still see the event).
template <size_t N>
class IdBook {
public:
    bool take(uint64_t id, uint32_t k) {
        if (id == 0) return false;
        for (size_t i = 0; i < N; ++i) {
            uint64_t cur = ids_[i].load(std::memory_order_acquire);
            if (cur == 0) {
                uint64_t expected = 0;
                if (ids_[i].compare_exchange_strong(expected, id, std::memory_order_acq_rel)) {
                    cur = id;
                } else {
                    cur = expected;
                }
            }
            if (cur == id) return counts_[i].fetch_add(1, std::memory_order_relaxed) < k;
        }
        full_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint64_t count_of(uint64_t id) const {
        for (size_t i = 0; i < N; ++i) {
            if (ids_[i].load(std::memory_order_acquire) == id) return counts_[i].load(std::memory_order_relaxed);
        }
        return 0;
    }
    size_t used() const {
        size_t n = 0;
        for (size_t i = 0; i < N; ++i) n += ids_[i].load(std::memory_order_relaxed) != 0;
        return n;
    }
    uint64_t id_at(size_t i) const { return i < N ? ids_[i].load(std::memory_order_acquire) : 0; }
    uint64_t count_at(size_t i) const { return i < N ? counts_[i].load(std::memory_order_relaxed) : 0; }
    uint64_t full() const { return full_.load(std::memory_order_relaxed); }
    static constexpr size_t capacity() { return N; }

private:
    std::atomic<uint64_t> ids_[N] = {};
    std::atomic<uint64_t> counts_[N] = {};
    std::atomic<uint64_t> full_{0};
};

// ---------------------------------------------------------------- the points in 0x2126920 (both sides)

// 0x2126920(entry, J) = the condition of a dynamic interaction entry (p8-s12-tenderos.md §4.2): RCX = the entry
// (kept in R13), RDX = J (R15). Read with capstone on the read-only PE (build 2976, .rsrc, RUNTIME_FUNCTION
// 0x2126920-0x2126D3C; no branch of the function and no rel32 call/jmp of the PE lands strictly inside a patch).
//   A 0x2126A1F  mov rdi,rax ; mov [rbp+0x8E8],rax       RAX = field.vf48 = the stage manager the lookup will use;
//                (10 bytes)                              the field at [rsp+0x48]; then vf40, the lock of [rdi+8]
//                                                        (0x393DF0 at 0x2126A56) and 0x1F6C190(rdi, &out, [r13+0x20])
//                                                        at 0x2126A6A. Every later use of the manager reads rdi /
//                                                        [rbp+0x8E8] (0x1F6FC40 at 0x2126AD0), so RAX := X here
//                                                        makes the lookup, its lock and the dead check use X alike.
//   R 0x2126AAB  lea rcx,[rbp+0x8D0]                     after the lookup (both paths join here): RSI = the stage
//                (7 bytes; a je target at its start)     instance found or 0, [rbp+0x8E8] = the manager used.
//   F 0x2126D11  mov [rbp-0x80],rdi ; lea rcx,[rbp-0x80] every path but the two early returns (no stage id: al = 2;
//                (8 bytes; a jmp target at its start)    no user: al = 0) reaches it with BL = the result (0 refused,
//                                                        1 / 2 accepted: 0x2126C6F..0x2126C77, 0x2126C88 xor bl).
inline constexpr uintptr_t kPointA = 0x2126A1F, kPointR = 0x2126AAB, kPointF = 0x2126D11;
inline constexpr uint32_t kPatchA = 10, kPatchR = 7, kPatchF = 8;
inline constexpr uint8_t kBytesA[] = {0x48, 0x8B, 0xF8, 0x48, 0x89, 0x85, 0xE8, 0x08, 0x00, 0x00};
inline constexpr uint8_t kBytesR[] = {0x48, 0x8D, 0x8D, 0xD0, 0x08, 0x00, 0x00};
inline constexpr uint8_t kBytesF[] = {0x48, 0x89, 0x7D, 0x80, 0x48, 0x8D, 0x4D, 0x80};
// Context checked before A (the vf48 call and its field slot) and before R (the lookup's call and its result use):
//   0x2126A14 mov rcx,[rsp+0x48] ; mov rax,[rcx] ; call [rax+0x48]
//   0x2126A5C mov r8,[r13+0x20] ; lea rdx,[rbp+0x8D0] ; mov rcx,rdi ; call 0x1F6C190 (rel32 E8 21 57 E4 FF)
inline constexpr uintptr_t kCtxA = 0x2126A14, kCtxLookup = 0x2126A5C;
inline constexpr uint8_t kCtxABytes[] = {0x48, 0x8B, 0x4C, 0x24, 0x48, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48};
inline constexpr uint8_t kCtxLookupBytes[] = {0x4D, 0x8B, 0x45, 0x20, 0x48, 0x8D, 0x95, 0xD0, 0x08, 0x00, 0x00,
                                              0x48, 0x8B, 0xCF, 0xE8, 0x21, 0x57, 0xE4, 0xFF};
inline constexpr uint32_t kRspField = 0x48;     // [rsp+0x48] at A: the field whose vf48 gave RAX
inline constexpr uint32_t kRbpMgrUsed = 0x8E8;  // [rbp+0x8E8] after A: the manager the lookup used
inline constexpr uint32_t kEntry_StageId = 0x20;  // u64 [entry+0x20] (0x2126A5C), tested & 0x1FFFFFFF at 0x212694D
inline constexpr uint32_t kJ_User = 0xA0, kUser_Key = 0x60;

// RTTI vtables (build 2976).
inline constexpr uintptr_t kVtClientField = 0x55BCEA0;    // pa::ClientField (vf48 0x9A5BE0)
inline constexpr uintptr_t kVtServerField = 0x5B29A60;    // pa::ServerField (vf48 0x2B00CE0)
inline constexpr uintptr_t kVtClientStageMgr = 0x55C9720;  // pa::ClientSequencerStageManager
inline constexpr uintptr_t kVtServerStageMgr = 0x5B17A58;  // pa::ServerSequencerStageManager

}  // namespace cdmp::session2::isl
