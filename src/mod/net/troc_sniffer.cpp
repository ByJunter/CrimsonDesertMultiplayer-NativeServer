#include "mod/net/troc_sniffer.h"

#include <safetyhook.hpp>

#include <atomic>
#include <deque>

#include <share.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#include "common/byte_ring.h"
#include "common/capture_file.h"
#include "mod/log_ring.h"
#include "mod/role.h"
#include "mod/game/offsets.h"
#include "mod/game/rtti.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/game/game_errors.h"
#include "mod/game/game_fault.h"
#include "mod/net/local_capture.h"
#include "mod/net/remote_puppet.h"

namespace cdmp::net {
namespace {
namespace off = game::build_2976;

enum class Dir : uint8_t { ClientToServer = 1, ServerToClient = 2 };

constexpr size_t kRingBytes = 64ull * 1024 * 1024;
constexpr uint16_t kMaxCopy = 32768;  // the game's packet buffers are 0x8000 bytes
constexpr size_t kPreviewBytes = 48;

// One record in the byte ring, followed by `copied` packet bytes. Records are
// 8-byte aligned; a record_len of 0 marks "wrap to the start of the ring".
#pragma pack(push, 1)
struct RecordHeader {
    uint32_t record_len;  // header + data, aligned
    uint32_t session;
    uint64_t t_us;
    uint32_t thread;
    uint16_t size;    // size the game reported
    uint16_t copied;  // bytes that follow (== size unless > kMaxCopy)
    Dir dir;
    uint8_t flags;  // kRecModExtra: a packet the mod itself delivered (recv-extra), not the server's
    uint8_t pad[6];
};
#pragma pack(pop)
static_assert(sizeof(RecordHeader) == 32);
constexpr uint8_t kRecModExtra = 1;  // also the .bin record's pad8 (bit 0); readers that ignore it see the old format

// Shared by game threads (producers: any thread that posts a packet, plus the
// client NetworkThread) and our writer (consumer). Producers hold the SRW lock
// only for one memcpy; see ByteRing for the consumer protocol. If the writer
// falls behind, packets are dropped and counted, never blocking the game.
SRWLOCK g_lock = SRWLOCK_INIT;
ByteRing* g_ring = nullptr;
uint64_t g_dropped = 0;
uint64_t g_truncated = 0;
LARGE_INTEGER g_qpc_freq;
std::atomic<uint64_t> g_first_packet_us{0};  // the text capture's t = 0 (sniffer_first_packet_us)
std::atomic<PacketTap> g_tap{nullptr};        // sniffer_set_tap: called on the writer thread
std::atomic<RecvRewrite> g_rewrite{nullptr};  // sniffer_set_recv_rewrite: called in the OnRecvPacket hook
std::atomic<RecvExtra> g_extra{nullptr};      // sniffer_set_recv_extra: called in the OnRecvPacket hook
std::atomic<uint64_t> g_extra_delivered{0}, g_extra_faulted{0}, g_extra_refused{0};
thread_local bool t_extra = false;  // inside the recv-extra delivery: capture() flags the record kRecModExtra
constexpr size_t kExtraCap = 512;             // the largest extra packet (a 2697 is 240 bytes)
std::atomic<bool> g_hooks_in{false};

SafetyHookMid g_post_hook;
SafetyHookMid g_recv_hook;

// No C++ objects here: SEH and destructors don't mix.
bool guarded_copy(void* dst, const void* src, size_t n) {
    __try {
        std::memcpy(dst, src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint64_t now_us() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    const uint64_t f = static_cast<uint64_t>(g_qpc_freq.QuadPart);
    const uint64_t t = static_cast<uint64_t>(q.QuadPart);
    return t / f * 1000000ull + t % f * 1000000ull / f;  // no overflow for long uptimes
}

void capture(Dir dir, uint32_t session, const uint8_t* pkt, uint64_t rsp) {
    uint16_t size = 0;
    if (!guarded_copy(&size, reinterpret_cast<const void*>(rsp + off::kHookArgSizeStackOffset),
                      sizeof size) ||
        !pkt || size < 3) {
        return;
    }
    if (size < 5) {
        // Only the internal 3-byte goodbye LogoutSessionToServerReq D1 07 FF
        // (0x8CAD10; docs/research/pendientes/salida.md §1.1) is that short.
        uint8_t b[3] = {};
        if (size != 3 || !guarded_copy(b, pkt, 3) || b[0] != 0xD1 || b[1] != 0x07 || b[2] != 0xFF) return;
    }
    RecordHeader h{};
    h.session = session;
    h.flags = t_extra ? kRecModExtra : 0;
    h.t_us = now_us();
    h.thread = GetCurrentThreadId();
    h.size = size;
    h.copied = std::min(size, kMaxCopy);
    h.dir = dir;
    h.record_len = static_cast<uint32_t>((sizeof h + h.copied + 7) & ~size_t{7});

    AcquireSRWLockExclusive(&g_lock);
    if (auto at = g_ring->reserve(h.record_len)) {
        // If the copy faults, the record is simply not committed (a wrap that
        // already happened stays valid: the consumer skips the marker).
        if (guarded_copy(g_ring->at(*at) + sizeof h, pkt, h.copied)) {
            std::memcpy(g_ring->at(*at), &h, sizeof h);
            g_ring->commit(h.record_len);
            if (h.copied < h.size) ++g_truncated;
        }
    } else {
        ++g_dropped;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

// ClientNet::PostPacket(ClientNet*, int* result, u32 session, const u8* packet,
// u16 size, u8 channel, u16 flags) (combate-coop.md §4.2). The channel and
// flags of real calls are recorded per opcode (read-only) so that a packet of
// ours can be posted the same way.
using PostPacketFn = void(__fastcall*)(void*, int*, uint32_t, const uint8_t*, uint16_t, uint8_t, uint16_t);
PostPacketFn g_post_fn = nullptr;
std::atomic<uint32_t> g_post_args[4096];  // bit31 seen | flags << 8 | channel
std::atomic<uint32_t> g_post_args_any{0};  // last seen for any opcode
SRWLOCK g_c2s_lock = SRWLOCK_INIT;
struct QueuedC2S {
    uint64_t ms = 0;
    std::vector<uint8_t> packet;
};
std::deque<QueuedC2S> g_c2s_queue;
// Our packets wait for the game's next real one; one that waited this long
// (a menu, a loading screen) names actors of a world that may be gone
// (pendiente-robustez.md §2.4): dropped.
constexpr uint64_t kMaxC2SWaitMs = 3000;
thread_local bool t_posting = false;

bool guarded_post(void* net, uint32_t session, const uint8_t* pkt, uint16_t size, uint8_t channel,
                  uint16_t flags) {
    __try {
        int result = 0;
        g_post_fn(net, &result, session, pkt, size, channel, flags);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_post_args(uint64_t rsp, uint8_t& channel, uint16_t& flags) {
    __try {
        channel = *reinterpret_cast<const uint8_t*>(rsp + 0x30);
        flags = *reinterpret_cast<const uint16_t*>(rsp + 0x38);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void on_post_packet(safetyhook::Context& ctx) {
    capture(Dir::ClientToServer, static_cast<uint32_t>(ctx.r8),
            reinterpret_cast<const uint8_t*>(ctx.r9), ctx.rsp);
    if (t_posting) return;
    uint8_t channel = 0;
    uint16_t flags = 0;
    uint16_t op = 0;
    if (!read_post_args(ctx.rsp, channel, flags) || !guarded_copy(&op, reinterpret_cast<const void*>(ctx.r9), 2)) {
        return;
    }
    const uint32_t packed = 0x80000000u | (static_cast<uint32_t>(flags) << 8) | channel;
    if (op < 4096) g_post_args[op].store(packed, std::memory_order_relaxed);
    g_post_args_any.store(packed, std::memory_order_relaxed);

    // Our own packets go in before this real one, on the same thread.
    if (!g_post_fn || !TryAcquireSRWLockExclusive(&g_c2s_lock)) return;
    std::deque<QueuedC2S> mine;
    mine.swap(g_c2s_queue);
    ReleaseSRWLockExclusive(&g_c2s_lock);
    const uint64_t now = GetTickCount64();
    for (auto& [queued_ms, pkt] : mine) {
        uint16_t pop = 0;
        std::memcpy(&pop, pkt.data(), 2);
        if (now - queued_ms > kMaxC2SWaitMs) {
            log::info("Inject C>S: opcode {} dropped, it waited {} ms", pop, now - queued_ms);
            continue;
        }
        const uint32_t a = pop < 4096 && g_post_args[pop].load() ? g_post_args[pop].load() : packed;
        t_posting = true;
        const bool ok = guarded_post(reinterpret_cast<void*>(ctx.rcx), static_cast<uint32_t>(ctx.r8),
                                     pkt.data(), static_cast<uint16_t>(pkt.size()),
                                     static_cast<uint8_t>(a & 0xFF), static_cast<uint16_t>((a >> 8) & 0xFFFF));
        t_posting = false;
        log::info("Inject C>S: opcode {} ({} bytes, channel {}, flags 0x{:X}) {}", pop, pkt.size(),
                  a & 0xFF, (a >> 8) & 0xFFFF, ok ? "posted" : "EXCEPTION");
        if (!ok) g_post_fn = nullptr;
    }
}

// ClientNet::OnRecvPacket(ClientNet*, NwSession*, ?, const u8* packet, u16 size).
using OnRecvPacketFn = void(__fastcall*)(void*, void*, uint64_t, const uint8_t*, uint16_t);
OnRecvPacketFn g_on_recv_fn = nullptr;
bool g_injection_enabled = false;
thread_local bool t_injecting = false;

// SEH wrapper (no C++ objects): a malformed injected packet must not take the
// game down from inside its own packet code.
bool guarded_inject(void* net, void* session, uint64_t arg3, const uint8_t* pkt, uint16_t size) {
    __try {
        g_on_recv_fn(net, session, arg3, pkt, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// P8 mount ride (review of 3aaf038, I1): an exception inside OnRecvPacket while it takes the mod's extra packet is the
// game's (it may hold its queue's lock): the path switches off, one line goes straight to the log and the exception
// is passed on, never swallowed.
int extra_fault(EXCEPTION_POINTERS* ep) noexcept {
    t_injecting = false;
    t_extra = false;
    g_extra.store(nullptr, std::memory_order_release);
    g_extra_faulted.fetch_add(1, std::memory_order_relaxed);
    return game::game_fault_continue(ep, "Sniffer recv-extra",
                                     "ClientNet::OnRecvPacket (exe+0x35CA00) took the mod's derived packet");
}
void deliver_extra(void* net, void* session, uint64_t arg3, const uint8_t* pkt, uint16_t size) noexcept {
    __try {
        g_on_recv_fn(net, session, arg3, pkt, size);
    } __except (extra_fault(GetExceptionInformation())) {
    }
}

void on_recv_packet(safetyhook::Context& ctx) {
    capture(Dir::ServerToClient, 0, reinterpret_cast<const uint8_t*>(ctx.r9), ctx.rsp);
    // P8 D1 (session2/ride_filter.h): after the capture, before the game reads the packet; the callback
    // guards its own accesses. The size is the 5th argument, as capture() reads it. Never on a packet the mod
    // itself injects (experiment 4c, t_injecting): those are the dev tools' own, not the server's.
    if (const RecvRewrite rw = g_rewrite.load(std::memory_order_acquire); rw && ctx.r9 && !t_injecting) {
        uint16_t size = 0;
        if (guarded_copy(&size, reinterpret_cast<const void*>(ctx.rsp + off::kHookArgSizeStackOffset), sizeof size)) {
            rw(reinterpret_cast<uint8_t*>(ctx.r9), size);
        }
    }
    // P8 mount ride (session2/horse_follow.h): at most one extra packet derived from this one, delivered before it
    // through the same function and thread (4c's path); the mod's stack buffer, nothing allocated.
    if (const RecvExtra ex = g_extra.load(std::memory_order_acquire); ex && ctx.r9 && !t_injecting && g_on_recv_fn) {
        uint16_t size = 0;
        if (guarded_copy(&size, reinterpret_cast<const void*>(ctx.rsp + off::kHookArgSizeStackOffset), sizeof size)) {
            alignas(8) uint8_t extra[kExtraCap];
            // The callback always runs (both PCs read the riders' Acks); only the delivery needs injection on
            // (the opcode table checked at install, and not stopped by an earlier fault of experiment 4c).
            const size_t n = ex(reinterpret_cast<const uint8_t*>(ctx.r9), size, extra, sizeof extra);
            if (n >= 5 && n <= sizeof extra && !g_injection_enabled) {
                g_extra_refused.fetch_add(1, std::memory_order_relaxed);
            } else if (n >= 5 && n <= sizeof extra) {
                t_injecting = true;
                t_extra = true;
                deliver_extra(reinterpret_cast<void*>(ctx.rcx), reinterpret_cast<void*>(ctx.rdx), ctx.r8, extra,
                              static_cast<uint16_t>(n));
                t_extra = false;
                t_injecting = false;
                g_extra_delivered.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    if (!g_injection_enabled || t_injecting) return;

    // Experiment 4c: feed queued packets through the same function, on the
    // same thread, right before this real one. Re-entering hits this hook
    // again (and gets captured) but t_injecting stops the recursion.
    std::vector<uint8_t> pkt;
    while (puppet_take_injection(pkt)) {
        t_injecting = true;
        const bool ok = guarded_inject(reinterpret_cast<void*>(ctx.rcx),
                                       reinterpret_cast<void*>(ctx.rdx), ctx.r8, pkt.data(),
                                       static_cast<uint16_t>(pkt.size()));
        t_injecting = false;
        const uint16_t op = static_cast<uint16_t>(pkt[0] | (pkt[1] << 8));
        if (ok) {
            // Movement goes in at ~40 Hz; log only loads and removals.
            if (op == off::kOpActorLoadDataAck || op == off::kOpRemoveActorAck) {
                log::info("Inject: delivered opcode {} ({} bytes) to ClientNet::OnRecvPacket", op,
                          pkt.size());
            } else if (op == off::kOpStartActionAck && pkt.size() >= 9) {
                // The horse copy's actions (its first one matters: horse_diag).
                uint32_t key = 0;
                std::memcpy(&key, pkt.data() + 5, 4);
                static int horse_actions = 0;
                if (key == 0xB010F002 && horse_actions++ < 20) {
                    log::info("Inject: delivered StartActionAck of the horse copy ({} bytes)", pkt.size());
                }
            }
        } else {
            g_injection_enabled = false;
            log::error("Inject: EXCEPTION while delivering opcode {}; injection disabled", op);
        }
    }
}

// ---------------------------------------------------------------- names

struct TrocInfo {
    std::string name;
    uint32_t kind = 0;
};
// Key: (opcode << 32) | kind. Opcodes are unique per build in practice, but
// keep the kind in the key so a Req and an Ack can never be confused.
std::map<uint64_t, TrocInfo> g_trocs;
std::map<uint16_t, std::string> g_name_by_opcode;

// Walks the static handler instances and reads (opcode, kind, RTTI name).
// Resolved at runtime because opcodes shift between game builds.
void load_troc_names(uintptr_t base) {
    for (uintptr_t a = base + off::kTrocInstancesBegin; a < base + off::kTrocInstancesEnd; a += 8) {
        auto vt = game::read_ptr(a);
        if (!vt || *vt < base || *vt >= base + 0x10000000) continue;
        const std::string decorated = game::rtti_name_of_vtable(*vt);
        if (decorated.rfind(".?AVTrocTr", 0) != 0) continue;
        auto kind = game::read<uint32_t>(a + off::kTroc_Kind);
        auto op = game::read<uint16_t>(a + off::kTroc_Opcode);
        if (!kind || !op) continue;
        TrocInfo info{game::short_class_name(decorated, "TrocTr"), *kind};
        g_name_by_opcode.emplace(*op, info.name);
        g_trocs[(static_cast<uint64_t>(*op) << 32) | *kind] = std::move(info);
        a += 0x80;  // skip the rest of this 0x90-byte instance
    }
}

const std::string& name_for(uint16_t op) {
    static const std::string unknown = "?";
    auto it = g_name_by_opcode.find(op);
    return it == g_name_by_opcode.end() ? unknown : it->second;
}

// ---------------------------------------------------------------- writer

// cdmp_packets_*.bin: "CDPK" u32 version u32 pe_timestamp, then records:
//   u64 t_us | u8 dir | u8 pad | u16 size | u16 copied | u16 pad | u32 session | u32 thread | data[copied]
// version 1: at most 1024 bytes per packet; version 2: whole packets (<= 32 KB).
struct BinHeader {
    char magic[4];
    uint32_t version;
    uint32_t pe_timestamp;
};

#pragma pack(push, 1)
struct BinRecord {
    uint64_t t_us;
    uint8_t dir;
    uint8_t pad8;
    uint16_t size;
    uint16_t copied;
    uint16_t pad16;
    uint32_t session;
    uint32_t thread;
};
#pragma pack(pop)

// ---------------------------------------------------------------- the disk queue
//
// p8-impl-log-async.md §1.3: the game's threads only ever copy a packet into g_ring (above). This writer
// thread decodes the records, feeds the co-op capture and the tap (observer_seq_copy's ledger,
// host_scene_diag) and runs capture_tick / puppet_tick / game_errors_drain. Until 10-10 it also did the
// fwrite of cdmp_packets_*.log/.bin itself, so a slow disk (Shadow: ~0.3 MB/s, 310 MB in 16 min) stopped all
// of that and, once g_ring was full, cost packets to the tap too. Now the recording goes into two bounded
// rings of the mod's memory (VirtualAlloc, only with record=1) written by a disk thread of its own; when
// they are full a line / record is dropped and counted ("# DISK QUEUE" in the text capture), never waited for.
struct DiskLock {
    SRWLOCK l = SRWLOCK_INIT;
    void lock() noexcept { AcquireSRWLockExclusive(&l); }
    void unlock() noexcept { ReleaseSRWLockExclusive(&l); }
};
// Small on purpose (review of 10-10): at process exit the threads are killed and what is queued is lost, so
// the queue holds ~1-2 s of a slow disk, not minutes. With a healthy disk it is empty every 20 ms (each pass
// ends with fflush; the CRT buffers are small too). Lines/records that do not fit are dropped and counted.
constexpr size_t kDiskTextBytes = 1u << 20, kDiskBinBytes = 4u << 20;
// Kept free by ordinary lines/records for the loss notes and gap records (pushed with no reserve).
constexpr size_t kDiskReserve = 4096;
constexpr size_t kDiskPassBytes = 1u << 20;  // one pass of the disk thread writes at most this per file
using DiskTextRing = log::LineRing<kDiskTextBytes, DiskLock>;
using DiskBinRing = log::LineRing<kDiskBinBytes, DiskLock>;

template <class R>
R* make_disk_ring() {
    void* p = VirtualAlloc(nullptr, sizeof(R), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return p ? new (p) R() : nullptr;
}

struct DiskQueue {
    DiskTextRing* text = nullptr;  // allocated once per process, reused
    DiskBinRing* bin = nullptr;
    FILE* ftext = nullptr;
    FILE* fbin = nullptr;
    HANDLE thread = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> slowest_ms{0}, slow_writes{0};
    // The sniffer's writer thread only (the single producer of both rings).
    uint64_t text_lost = 0;          // ordinary text lines not recorded
    uint64_t bin_lost = 0;           // .bin records not recorded
    uint32_t bin_lost_pending = 0;   // since the last gap record that made it
};
DiskQueue g_disk;

template <class R>
size_t disk_drain(R& ring, FILE* f) {
    const auto r = ring.readable(kDiskPassBytes);
    if (!r.size()) return 0;
    const uint64_t t0 = GetTickCount64();
    std::fwrite(r.a.p, 1, r.a.n, f);
    if (r.b.n) std::fwrite(r.b.p, 1, r.b.n, f);
    std::fflush(f);
    ring.consume(r.end);
    const uint64_t dt = GetTickCount64() - t0;
    if (dt > g_disk.slowest_ms.load(std::memory_order_relaxed)) g_disk.slowest_ms.store(dt, std::memory_order_relaxed);
    if (dt >= 250) g_disk.slow_writes.fetch_add(1, std::memory_order_relaxed);
    return r.size();
}

DWORD WINAPI disk_thread(LPVOID) {
    for (;;) {
        const bool stop = g_disk.stop.load(std::memory_order_acquire);
        // Bounded passes (1 MB per file each), so the stop flag is looked at even under a constant flood.
        const ULONGLONG t0 = GetTickCount64();
        while (disk_drain(*g_disk.text, g_disk.ftext) + disk_drain(*g_disk.bin, g_disk.fbin)) {
            if (GetTickCount64() - t0 >= (stop ? 3000u : 100u)) break;
        }
        if (stop) break;
        Sleep(20);
    }
    return 0;
}

// An ordinary text line (keeps the reserve free for the notes); a loss is counted for the next note.
void disk_text(std::string_view s) {
    if (!g_disk.text->push_bytes(s, {}, kDiskReserve)) ++g_disk.text_lost;
}
void disk_text(const char* s, size_t n) { disk_text(std::string_view(s, n)); }
// A loss note: may use the reserve; false if even that did not fit (the caller keeps it pending).
bool disk_note(std::string_view s) { return g_disk.text->push_bytes(s); }

// One .bin record, whole or not at all. Records lost before it are marked by a gap record in their place
// (capture::gap_record: an empty S>C record every reader skips); a gap that does not fit stays pending.
void disk_bin(std::string_view head, std::string_view data, uint64_t t_us) {
    if (g_disk.bin_lost_pending) {
        const auto gap = capture::encode_record(capture::gap_record(t_us, g_disk.bin_lost_pending));
        if (g_disk.bin->push_bytes(std::string_view(reinterpret_cast<const char*>(gap.data()), gap.size()))) {
            g_disk.bin_lost_pending = 0;
        }
    }
    if (g_disk.bin_lost_pending || !g_disk.bin->push_bytes(head, data, kDiskReserve)) {
        // Behind an unwritten gap nothing is kept (the gap must come first): this record is lost too.
        ++g_disk.bin_lost;
        ++g_disk.bin_lost_pending;
    }
}

struct Writer {
    FILE* text = nullptr;
    FILE* bin = nullptr;
    uint64_t t0_us = 0;
    std::map<uint16_t, uint64_t> window_c2s, window_s2c, total_c2s, total_s2c;
    uint64_t reported_dropped = 0;
    uint64_t reported_disk_text = 0, reported_disk_bin = 0;
    uint64_t packets = 0;

    void write(const RecordHeader& h, const uint8_t* data) {
        if (!t0_us) {
            t0_us = h.t_us;
            if (text) g_first_packet_us.store(t0_us, std::memory_order_relaxed);
        }
        ++packets;
        const uint16_t op = static_cast<uint16_t>(data[0] | (data[1] << 8));
        const int slot = static_cast<int8_t>(data[2]);
        const uint16_t payload_len = static_cast<uint16_t>(data[3] | (data[4] << 8));
        const bool c2s = h.dir == Dir::ClientToServer;
        // P8 mount ride: a packet the mod itself delivered (recv-extra) is not the server's traffic: no tap (the
        // host's scene / seq-copy / owner_fanout ledgers) and no co-op observer; marked in the capture.
        const bool mod_extra = (h.flags & kRecModExtra) != 0;
        (c2s ? window_c2s : window_s2c)[op]++;
        (c2s ? total_c2s : total_s2c)[op]++;
        if (const PacketTap tap = g_tap.load(std::memory_order_acquire); tap && h.copied == h.size && !mod_extra) {
            tap(data, h.copied, h.t_us, !c2s);
        }
        if (!text) {  // not recording: only feed co-op
            if (h.copied == h.size && !mod_extra) {
                if (c2s) capture_observe_c2s(data, h.copied);
                else capture_observe_s2c(data, h.copied);
            }
            return;
        }

        // Hex preview through a lookup table: formatting used to be the
        // bottleneck that made v1 drop packets during loading bursts.
        static const char* kHex = "0123456789ABCDEF";
        char hex[kPreviewBytes * 3 + 1];
        const size_t n = std::min<size_t>(h.copied > 5 ? h.copied - 5 : 0, kPreviewBytes);
        for (size_t i = 0; i < n; ++i) {
            hex[i * 3] = kHex[data[5 + i] >> 4];
            hex[i * 3 + 1] = kHex[data[5 + i] & 15];
            hex[i * 3 + 2] = ' ';
        }
        hex[n * 3] = '\0';
        char line[512];
        const int len = std::snprintf(
            line, sizeof line, "%10.3f %s %-38s %5u slot %3d len %5u%s%s | %s\n",
            static_cast<double>(h.t_us - t0_us) / 1e6, c2s ? "C>S" : "S>C", name_for(op).c_str(),
            op, slot, payload_len, payload_len + 5u == h.size ? "" : " (size mismatch)",
            mod_extra ? " [mod: derived, not the server's]" : "", hex);
        if (len > 0) disk_text(line, std::min<size_t>(len, sizeof line - 1));

        if (h.copied == h.size && !mod_extra) {
            if (c2s) {
                capture_observe_c2s(data, h.copied);
            } else {
                capture_observe_s2c(data, h.copied);
            }
        }

        const BinRecord r{h.t_us, static_cast<uint8_t>(h.dir), static_cast<uint8_t>(mod_extra ? kRecModExtra : 0),
                          h.size, h.copied, 0, h.session, h.thread};
        // One record whole or not at all (a gap record marks the lost ones: disk_bin).
        disk_bin(std::string_view(reinterpret_cast<const char*>(&r), sizeof r),
                 std::string_view(reinterpret_cast<const char*>(data), h.copied), h.t_us);
    }

    static std::string top(const std::map<uint16_t, uint64_t>& counts, size_t k) {
        std::vector<std::pair<uint64_t, uint16_t>> v;
        uint64_t sum = 0;
        for (auto& [op, c] : counts) {
            v.emplace_back(c, op);
            sum += c;
        }
        std::sort(v.rbegin(), v.rend());
        std::string s = std::format("{} total", sum);
        for (size_t i = 0; i < v.size() && i < k; ++i)
            s += std::format(", {} x{}", name_for(v[i].second), v[i].first);
        return s;
    }

    void stats(uint64_t dropped, uint64_t truncated) {
        if (!text) {
            window_c2s.clear();
            window_s2c.clear();
            return;
        }
        disk_text(std::format("# stats 10s | C>S {}\n", top(window_c2s, 8)));
        disk_text(std::format("# stats 10s | S>C {}\n", top(window_s2c, 8)));
        if (dropped != reported_dropped) {
            disk_text(std::format("# DROPPED {} packets so far (writer too slow)\n", dropped));
            reported_dropped = dropped;
        }
        if (truncated) {
            disk_text(std::format("# truncated {} packets larger than {} bytes so far\n", truncated, kMaxCopy));
        }
        const uint64_t lost_text = g_disk.text_lost, lost_bin = g_disk.bin_lost;
        if (lost_text != reported_disk_text || lost_bin != reported_disk_bin) {
            // From the reserve; if even the note does not fit, it is not counted as reported (next time).
            if (disk_note(std::format("# DISK QUEUE: {} text line(s) and {} .bin record(s) not recorded so far "
                                      "(the disk was too slow; nothing waited for it; gap records in the .bin), "
                                      "slowest write {} ms\n",
                                      lost_text, lost_bin, g_disk.slowest_ms.load()))) {
                reported_disk_text = lost_text;
                reported_disk_bin = lost_bin;
            }
        }
        window_c2s.clear();
        window_s2c.clear();
    }
};

struct ThreadArgs {
    std::wstring dir;
    HANDLE stop;
    uint32_t pe_timestamp;
    bool record;
};

DWORD WINAPI writer_thread(LPVOID p) {
    std::unique_ptr<ThreadArgs> args(static_cast<ThreadArgs*>(p));
    Writer w;
    // One pair of files per game session so a restart never overwrites a capture.
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond);
    // CDMP_ROLE (role.h): cdmp_packets.<role>_<stamp>, apart from the other copy's.
    const std::wstring stem = args->dir + role::tagged(L"cdmp_packets") + L"_" + stamp;
    if (args->record) {
        // Shared access so the capture can be read (e.g. by Claude) while the game runs.
        w.text = _wfsopen((stem + L".log").c_str(), L"w", _SH_DENYNO);
        w.bin = _wfsopen((stem + L".bin").c_str(), L"wb", _SH_DENYNO);
        if (!w.text || !w.bin) {
            log::error("Sniffer: cannot open output files");
            return 0;
        }
        // Small CRT buffers: the disk queue is the buffer (and is flushed after every pass).
        std::setvbuf(w.text, nullptr, _IOFBF, 64 << 10);
        std::setvbuf(w.bin, nullptr, _IOFBF, 256 << 10);
        const BinHeader hdr{{'C', 'D', 'P', 'K'}, 2, args->pe_timestamp};
        std::fwrite(&hdr, sizeof hdr, 1, w.bin);
        std::fprintf(w.text, "# cdmp packet sniffer v2 | %zu message types known | t = seconds since first packet\n",
                     g_trocs.size());
        std::fprintf(w.text, "#       t dir name                                   opcode slot      len | payload preview\n");
        // From here on only the disk thread touches the two files. sniffer_install runs once per process
        // (mod.cpp's late_init); a second start reuses the rings and starts from a clean state anyway.
        if (!g_disk.text) g_disk.text = make_disk_ring<DiskTextRing>();
        if (!g_disk.bin) g_disk.bin = make_disk_ring<DiskBinRing>();
        g_disk.ftext = w.text;
        g_disk.fbin = w.bin;
        g_disk.stop.store(false, std::memory_order_release);
        g_disk.text_lost = g_disk.bin_lost = 0;
        g_disk.bin_lost_pending = 0;
        g_disk.thread = nullptr;
        if (g_disk.text && g_disk.bin) {
            // Whatever an earlier run left queued belongs to its files, not to these.
            g_disk.text->consume(g_disk.text->pushed_bytes());
            g_disk.bin->consume(g_disk.bin->pushed_bytes());
            g_disk.thread = CreateThread(nullptr, 0, disk_thread, nullptr, 0, nullptr);
        }
        if (!g_disk.thread) {
            log::error("Sniffer: cannot start the disk queue ({} + {} MB); not recording", kDiskTextBytes >> 20,
                       kDiskBinBytes >> 20);
            std::fclose(w.text);
            std::fclose(w.bin);
            w.text = w.bin = nullptr;
        } else {
            log::info("Sniffer: recording through a disk queue of {} + {} MB (its own thread; a slow disk drops "
                      "recorded lines/records, counted in '# DISK QUEUE' lines, and never stops the packet decoding, "
                      "the tap or the game)",
                      kDiskTextBytes >> 20, kDiskBinBytes >> 20);
        }
    } else {
        log::info("Sniffer: not recording packets to disk ([sniffer] record=0)");
    }

    ULONGLONG last_stats = GetTickCount64();
    for (;;) {
        AcquireSRWLockExclusive(&g_lock);
        const uint64_t head = g_ring->head();
        const uint64_t tail = g_ring->tail();  // only this thread moves the tail
        const uint64_t dropped = g_dropped;
        const uint64_t truncated = g_truncated;
        ReleaseSRWLockExclusive(&g_lock);

        // Outside the lock: producers never write into [tail, head).
        const uint64_t new_tail = g_ring->drain(tail, head, [&](const uint8_t* rec, uint32_t) {
            RecordHeader h;
            std::memcpy(&h, rec, sizeof h);
            w.write(h, rec + sizeof h);
        });
        AcquireSRWLockExclusive(&g_lock);
        g_ring->set_tail(new_tail);
        ReleaseSRWLockExclusive(&g_lock);

        capture_tick();
        puppet_tick();
        game::game_errors_drain();

        if (GetTickCount64() - last_stats >= 10000) {
            last_stats = GetTickCount64();
            w.stats(dropped, truncated);
        }
        if (WaitForSingleObject(args->stop, 10) == WAIT_OBJECT_0) break;
    }
    if (w.text) {
        disk_text(std::format("# session totals | {} packets\n", w.packets));
        disk_text(std::format("# session totals | C>S {}\n", Writer::top(w.total_c2s, 30)));
        disk_text(std::format("# session totals | S>C {}\n", Writer::top(w.total_s2c, 30)));
        if (g_disk.bin_lost_pending) {  // the last records were lost: their gap closes the .bin (best effort)
            const auto gap = capture::encode_record(capture::gap_record(w.t0_us, g_disk.bin_lost_pending));
            if (g_disk.bin->push_bytes(std::string_view(reinterpret_cast<const char*>(gap.data()), gap.size()))) {
                g_disk.bin_lost_pending = 0;
            }
        }
        if (g_disk.text_lost || g_disk.bin_lost) {
            disk_note(std::format("# DISK QUEUE (session): {} text line(s) and {} .bin record(s) not recorded\n",
                                  g_disk.text_lost, g_disk.bin_lost));
        }
        g_disk.stop.store(true, std::memory_order_release);
        // Bounded: a disk thread stuck in a write keeps its files (never closed under it).
        if (WaitForSingleObject(g_disk.thread, 5000) == WAIT_OBJECT_0) {
            std::fclose(w.text);
            std::fclose(w.bin);
            CloseHandle(g_disk.thread);
            g_disk.thread = nullptr;
        }
    }
    return 0;
}

bool head_matches(uintptr_t addr, const uint8_t* expected, size_t n) {
    std::vector<uint8_t> actual(n);
    return game::safe_read(addr, actual.data(), n) && std::memcmp(actual.data(), expected, n) == 0;
}

}  // namespace

bool sniffer_inject_c2s(std::vector<uint8_t> packet) {
    if (!g_post_fn || packet.size() < 5 || packet.size() > 0x7F00) return false;
    AcquireSRWLockExclusive(&g_c2s_lock);
    const bool ok = g_c2s_queue.size() < 16;
    if (ok) g_c2s_queue.push_back({GetTickCount64(), std::move(packet)});
    ReleaseSRWLockExclusive(&g_c2s_lock);
    return ok;
}

uint64_t sniffer_first_packet_us() noexcept { return g_first_packet_us.load(std::memory_order_relaxed); }

void sniffer_set_tap(PacketTap tap) noexcept { g_tap.store(tap, std::memory_order_release); }

bool sniffer_set_recv_rewrite(RecvRewrite fn) noexcept {
    if (!g_hooks_in.load(std::memory_order_acquire)) return false;
    g_rewrite.store(fn, std::memory_order_release);
    return true;
}

bool sniffer_set_recv_extra(RecvExtra fn) noexcept {
    if (!g_hooks_in.load(std::memory_order_acquire) || !g_on_recv_fn) return false;
    g_extra.store(fn, std::memory_order_release);
    return true;
}

RecvExtraStats sniffer_recv_extra_stats() noexcept {
    return {g_extra_delivered.load(std::memory_order_relaxed), g_extra_faulted.load(std::memory_order_relaxed),
            g_extra_refused.load(std::memory_order_relaxed)};
}

bool sniffer_install(const std::wstring& dir, HANDLE stop, bool record) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (base != off::kImageBase || nt->FileHeader.TimeDateStamp != off::kPeTimestamp) {
        log::warn("Sniffer: unsupported game build, not installed");
        return false;
    }
    const uintptr_t post = base + off::kClientPostPacket;
    const uintptr_t recv = base + off::kClientOnRecvPacket;
    if (!head_matches(post, off::kClientPostPacketHead, sizeof off::kClientPostPacketHead) ||
        !head_matches(recv, off::kClientOnRecvPacketHead, sizeof off::kClientOnRecvPacketHead)) {
        log::error("Sniffer: function prologues don't match the expected bytes, not installed");
        return false;
    }

    load_troc_names(base);
    log::info("Sniffer: {} message types resolved from the game ({} opcodes)", g_trocs.size(),
              g_name_by_opcode.size());

    // Injection relies on two opcodes; check them against the live table.
    g_on_recv_fn = reinterpret_cast<OnRecvPacketFn>(recv);
    g_post_fn = reinterpret_cast<PostPacketFn>(post);
    const std::pair<uint16_t, const char*> expected[] = {
        {off::kOpActorLoadDataAck, "ActorLoadDataAck"}, {off::kOpRemoveActorAck, "RemoveActorAck"},
        {off::kOpMoveActorReq, "MoveActorReq"},         {off::kOpMoveActorAck, "MoveActorAck"},
        {off::kOpStartActionReq, "StartActionReq"},     {off::kOpStartActionAck, "StartActionAck"},
        {off::kOpRotateActorReq, "RotateActorReq"},     {off::kOpRotateActorAck, "RotateActorAck"},
    };
    g_injection_enabled = true;
    for (const auto& [op, name] : expected) {
        if (name_for(op) != name) {
            log::warn("Sniffer: opcode {} is '{}' in this game, expected '{}'", op, name_for(op), name);
            g_injection_enabled = false;
        }
    }
    if (!g_injection_enabled) log::warn("Sniffer: opcode table mismatch, injection disabled");

    QueryPerformanceFrequency(&g_qpc_freq);
    auto* storage = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, kRingBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (storage) g_ring = new ByteRing(storage, kRingBytes);
    if (!g_ring) {
        log::error("Sniffer: cannot allocate the {} MB capture ring", kRingBytes >> 20);
        return false;
    }

    g_post_hook = safetyhook::create_mid(reinterpret_cast<void*>(post), on_post_packet);
    g_recv_hook = safetyhook::create_mid(reinterpret_cast<void*>(recv), on_recv_packet);
    if (!g_post_hook || !g_recv_hook) {
        log::error("Sniffer: mid-hook creation failed (post {}, recv {})",
                   static_cast<bool>(g_post_hook), static_cast<bool>(g_recv_hook));
        return false;
    }
    g_hooks_in.store(true, std::memory_order_release);

    auto* args = new ThreadArgs{dir, stop, nt->FileHeader.TimeDateStamp, record};
    if (HANDLE t = CreateThread(nullptr, 0, writer_thread, args, 0, nullptr)) CloseHandle(t);
    log::info("Sniffer v2: hooked ClientNet::PostPacket (exe+0x{:X}) and ClientNet::OnRecvPacket "
              "(exe+0x{:X}); full packets up to {} bytes; writing cdmp_packets_<date-time>.log/.bin",
              off::kClientPostPacket, off::kClientOnRecvPacket, kMaxCopy);
    return true;
}

}  // namespace cdmp::net
