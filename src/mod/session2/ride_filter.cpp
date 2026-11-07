#include "mod/session2/ride_filter.h"
#include "mod/game/build_profile.h"

#include <windows.h>

#include <atomic>
#include <format>
#include <string>

#include "mod/log.h"
#include "mod/net/troc_sniffer.h"
#include "mod/session2/mount_sync.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/ride_filter_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

using rfl::Kind;
using rfl::Mode;

constexpr uint16_t kOpMainPlayerDataStartAck = 2756;  // u32 the client's own character at +0 (server.cpp)
constexpr uint16_t kEvRewrite = 1, kEvWindow = 2;
constexpr uint64_t kMaxLines = 48;       // rewrite / window lines in a run
constexpr uint64_t kSummaryMs = 60000;

std::atomic<uint8_t> g_mode{0};
std::atomic<bool> g_guest{false}, g_host_attached{false};
std::atomic<uint32_t> g_local_key{0};   // the guest's own character (its MainPlayerDataStartAck)
std::atomic<uint32_t> g_remote_key{0};  // the guest: the host's character (the host's control state 4)
std::atomic<uint64_t> c_conn_resets{0};

std::atomic<uint64_t> c_seen{0}, c_remote{0}, c_kind[rfl::kKinds] = {}, c_rewritten{0}, c_write_failed{0},
    c_window{0}, c_no_key{0}, c_deferred{0}, c_passed{0}, c_gate{0};
// P8 S18 (ride_filter_logic.h, Dismount): what to do with the remote rider's dismount actions; the 0x40 hashes.
std::atomic<uint8_t> g_dismount{static_cast<uint8_t>(rfl::Dismount::Rewrite)};
rfl::GateHashes g_gate;
std::atomic<uint32_t> g_last_key{0}, g_last_a0{0}, g_last_a1{0};
rfl::ParentWatch g_watch;
rfl::PairTable g_pairs;
p8p::EventRing<128> g_ring;
std::atomic<uint64_t> g_lines{0}, g_lines_dropped{0};  // written by the tick's thread only
uint64_t g_last_summary_ms = 0;

uint64_t tick_ms() { return GetTickCount64(); }

uint64_t qpc_us() {
    LARGE_INTEGER f, q;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q);
    const auto ff = static_cast<uint64_t>(f.QuadPart), qq = static_cast<uint64_t>(q.QuadPart);
    return qq / ff * 1000000ull + qq % ff * 1000000ull / ff;
}

// The whole decision for one packet (the game's or the mod's network thread). `write_ok`: the caller's
// in-place rewrite worked (the host's goes through SEH).
void note(const rfl::Outcome& o, Mode mode) noexcept {
    if (!o.start_action) return;
    c_seen.fetch_add(1, std::memory_order_relaxed);
    if (!o.remote) return;
    c_remote.fetch_add(1, std::memory_order_relaxed);
    c_kind[static_cast<size_t>(o.kind)].fetch_add(1, std::memory_order_relaxed);
    const rfl::WindowNote w = g_watch.note(o.ack, tick_ms());
    if (o.kind != Kind::None || w.in_window) {
        c_window.fetch_add(1, std::memory_order_relaxed);
        g_pairs.add(o.ack.a0, o.ack.a1, rfl::pair_flags(o.ack, w.after_change, o.kind));
        p8p::Event e;
        e.qpc_us = qpc_us();
        e.tid = GetCurrentThreadId();
        e.kind = o.kind != Kind::None ? kEvRewrite : kEvWindow;
        e.flags = static_cast<uint16_t>(rfl::pair_flags(o.ack, w.after_change, o.kind));
        e.v[0] = o.ack.key;
        e.v[1] = o.ack.a0;
        e.v[2] = o.ack.a1;
        e.v[3] = o.ack.parent;
        e.v[4] = o.ack.target;
        e.v[5] = static_cast<uint64_t>(o.kind);
        e.v[6] = o.rewritten ? 1 : o.deferred ? 2 : o.passed ? 3 : 0;
        e.v[8] = o.gate;
        e.v[7] = static_cast<uint64_t>(mode);
        g_ring.push(e);
    }
    if (o.kind != Kind::None) {
        g_last_key.store(o.ack.key, std::memory_order_relaxed);
        g_last_a0.store(o.ack.a0, std::memory_order_relaxed);
        g_last_a1.store(o.ack.a1, std::memory_order_relaxed);
    }
    if (o.rewritten) c_rewritten.fetch_add(1, std::memory_order_relaxed);
    if (o.deferred) c_deferred.fetch_add(1, std::memory_order_relaxed);
    if (o.passed) c_passed.fetch_add(1, std::memory_order_relaxed);
    if (o.gate) c_gate.fetch_add(1, std::memory_order_relaxed);
}

// P8 mounts (mount_sync.h, mode 1): with ride_filter=1 the decision is made on a copy first (probe semantics);
// a mount of a rider whose validated mount transaction mount_sync holds is left to its dispatch (which rewrites it
// there if the link is still missing); a dismount follows ride_dismount (P8 S18, rfl::route): rewrite = always
// rewritten (S17), partial = only the 0x40 actions rewritten and the rest let through (or deferred), native = left
// to mount_sync's dispatch and its gate detour while its transaction is valid; anything else is rewritten here.
Mode decide_mode(Mode mode) { return mode == Mode::On ? Mode::Probe : mode; }
rfl::Route route_of(Mode mode, rfl::Outcome& o) noexcept {
    if (mode != Mode::On || !o.remote || o.kind == Kind::None) return rfl::Route::Pass;
    auto d = static_cast<rfl::Dismount>(g_dismount.load(std::memory_order_relaxed));
    // native without its gate detour (not hooked, refused, off after a fault): the copy would stay in RideOff, so the
    // dismount is rewritten as in S17.
    if (d == rfl::Dismount::Native && o.kind == Kind::Dismount && !mount_sync_gate_ready()) d = rfl::Dismount::Rewrite;
    o.gate = o.kind == Kind::Dismount && rfl::gate_dismount(o.ack, g_gate);
    const bool ms = rfl::wants_mount_sync(o.kind, d, o.gate) && mount_sync_ride_filter_defers(o.ack.key);
    return rfl::route(o.kind, d, o.gate, ms);
}

// HOST: the sniffer's OnRecvPacket mid hook (the host client's NetworkThread). The packet is the game's
// receive buffer: read and written only under SEH (no C++ objects in this frame).
bool guarded_filter(Mode mode, uint8_t* packet, uint16_t size, uint32_t remote_key, rfl::Outcome* out) {
    __try {
        *out = rfl::filter_packet(mode, packet, size, remote_key, 0, game::current_build());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool guarded_rewrite(uint8_t* packet, rfl::Kind kind, bool* out) {
    __try {
        *out = rfl::rewrite(packet + rfl::kHeader, movement::payload_size(game::current_build(), 2991), kind, game::current_build());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void host_recv(uint8_t* packet, uint16_t size) noexcept {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == Mode::Off || size != rfl::kHeader + movement::payload_size(game::current_build(), 2991)) return;
    const uint32_t remote = server_user2_player_key();  // user 2's character (0 = not logged in yet)
    if (!remote) {
        c_no_key.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    rfl::Outcome o;
    if (!guarded_filter(decide_mode(mode), packet, size, remote, &o)) {
        c_write_failed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    switch (route_of(mode, o)) {
        case rfl::Route::Defer: o.deferred = true; break;
        case rfl::Route::Rewrite:
            if (!guarded_rewrite(packet, o.kind, &o.rewritten)) c_write_failed.fetch_add(1, std::memory_order_relaxed);
            break;
        case rfl::Route::Pass: o.passed = mode == Mode::On && o.remote && o.kind != Kind::None; break;
    }
    note(o, mode);
}

std::string learned_text() {
    std::string s;
    for (const auto& h : g_gate.h) {
        if (const uint32_t v = h.load(std::memory_order_relaxed)) s += std::format(" 0x{:08X}", v);
    }
    return s.empty() ? std::string() : std::format(":{}", s);
}

std::string hex_pairs() {
    std::string s;
    size_t shown = 0;
    for (const auto& slot : g_pairs.slot) {
        if (!slot.used.load(std::memory_order_acquire)) continue;
        if (shown++ >= 40) break;
        const uint8_t f = slot.flags.load(std::memory_order_relaxed);
        s += std::format("{}{:08X}/{:08X}{}{}{}{} x{}", s.empty() ? "" : ", ", slot.a0.load(), slot.a1.load(),
                         (f & 1) ? " P" : "", (f & 2) ? " T" : "", (f & 4) ? " after" : "",
                         (f >> 4) ? std::format(" [{}]", rfl::kind_name(static_cast<Kind>(f >> 4))) : std::string(),
                         slot.n.load());
    }
    return s.empty() ? std::string("none") : s;
}

}  // namespace

bool ride_filter_configure(const std::string& raw, bool guest) {
    const auto p = rfl::parse_mode(raw);
    if (!p.known) {
        log::warn("RideFilter: ride_filter='{}' is not 0, probe or 1; it stays off", raw);
        return false;
    }
    g_guest.store(guest);
    g_mode.store(static_cast<uint8_t>(p.mode));
    if (p.mode == Mode::Off) return false;
    log::info("RideFilter ON ({}, {}): the other player's StartActionAck 2991 with a mount action (A0 0x{:08X} / "
              "0x{:08X}) or the dismount layer (A1 0x{:08X} with an 0xB parent) {} | the hashes of its Acks that "
              "name an 0xB key or come within {} ms of a parent change are recorded | dismounts: ride_dismount={}",
              rfl::mode_name(p.mode), guest ? "guest: in guest_deliver" : "host: in ClientNet::OnRecvPacket",
              rfl::kMountA0[0], rfl::kMountA0[1], rfl::kDismountA1,
              p.mode == Mode::On ? "becomes the native on-foot (A0 0x26107D3C) / riding (A0 0xF79C7424) action with "
                                   "an empty A1, so RideOn/RideOff are never entered"
                                 : "is counted only (probe: nothing changed)",
              rfl::kWindowMs, rfl::dismount_name(ride_dismount_mode()));
    return true;
}

bool ride_dismount_configure(const std::string& raw) {
    const auto p = rfl::parse_dismount(raw);
    if (!p.known) log::warn("RideFilter: ride_dismount='{}' is not rewrite, partial or native; it stays rewrite", raw);
    g_dismount.store(static_cast<uint8_t>(p.mode));
    return p.known;
}
rfl::Dismount ride_dismount_mode() { return static_cast<rfl::Dismount>(g_dismount.load(std::memory_order_relaxed)); }
bool ride_filter_learn_gate_hash(uint32_t hash) noexcept { return g_gate.learn(hash); }
bool ride_filter_gate_hash(uint32_t hash) noexcept { return g_gate.contains(hash); }

bool ride_filter_active() { return g_mode.load(std::memory_order_relaxed) != 0; }
bool ride_filter_rewrites() { return static_cast<Mode>(g_mode.load(std::memory_order_relaxed)) == Mode::On; }

namespace {
// The guest's client connection the keys belong to: a new one forgets both (review 2026-10-10). The mod's network
// thread only (guest_deliver), so learning and forgetting never race.
uint64_t g_conn_epoch = 0;
void sync_connection(uint64_t epoch) {
    if (epoch == g_conn_epoch) return;
    g_conn_epoch = epoch;
    g_local_key.store(0, std::memory_order_relaxed);
    g_remote_key.store(0, std::memory_order_relaxed);
    c_conn_resets.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

void ride_filter_guest_control(const uint8_t* payload, size_t size, uint64_t connection_epoch) noexcept {
    if (!g_guest.load(std::memory_order_relaxed) || !ride_filter_active()) return;
    sync_connection(connection_epoch);
    if (const uint32_t k = rfl::host_character_from_control(payload, size)) {
        g_remote_key.store(k, std::memory_order_relaxed);
    }
}

void ride_filter_on_guest_packet(std::vector<uint8_t>& packet, uint64_t connection_epoch) noexcept {
    const auto mode = static_cast<Mode>(g_mode.load(std::memory_order_relaxed));
    if (mode == Mode::Off || !g_guest.load(std::memory_order_relaxed) || packet.size() < rfl::kHeader + 4) return;
    sync_connection(connection_epoch);
    const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
    if (op == kOpMainPlayerDataStartAck) {
        g_local_key.store(rfl::rd32(packet.data() + rfl::kHeader), std::memory_order_relaxed);
        return;
    }
    if (op != rfl::kOpStartActionAck) return;
    const uint32_t remote = g_remote_key.load(std::memory_order_relaxed);
    if (!remote) {
        c_no_key.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    rfl::Outcome o = rfl::filter_packet(decide_mode(mode), packet.data(), packet.size(), remote,
                                        g_local_key.load(std::memory_order_relaxed), game::current_build());
    switch (route_of(mode, o)) {
        case rfl::Route::Defer: o.deferred = true; break;
        case rfl::Route::Rewrite: o.rewritten = rfl::rewrite(packet.data() + rfl::kHeader, packet.size() - rfl::kHeader, o.kind, game::current_build()); break;
        case rfl::Route::Pass: o.passed = mode == Mode::On && o.remote && o.kind != Kind::None; break;
    }
    note(o, mode);
}

bool ride_filter_host_attach() {
    if (!ride_filter_active() || g_guest.load()) return false;
    if (!net::sniffer_set_recv_rewrite(&host_recv)) {
        log::warn("RideFilter: the host side needs the sniffer's ClientNet::OnRecvPacket hook ([sniffer] enabled=1); "
                  "ride_filter stays off on this PC");
        g_mode.store(0);
        return false;
    }
    g_host_attached.store(true);
    log::info("RideFilter: host side attached to the sniffer's OnRecvPacket hook (user 2's character only)");
    return true;
}

void ride_filter_tick() noexcept {
    if (!ride_filter_active()) return;
    try {
        p8p::Event e;
        while (g_ring.pop(e)) {
            if (g_lines >= kMaxLines) {
                ++g_lines_dropped;
                continue;
            }
            ++g_lines;
            const uint64_t parent = e.v[3];
            log::info("RideFilter [{}]: qpc {:.3f} s T{} | 0x{:08X} {} A0 0x{:08X} A1 0x{:08X} | parent {} | target "
                      "0x{:08X}{}{}",
                      rfl::mode_name(static_cast<Mode>(e.v[7])), static_cast<double>(e.qpc_us) / 1e6, e.tid, e.v[0],
                      e.kind == kEvRewrite ? rfl::kind_name(static_cast<Kind>(e.v[5])) : "window", e.v[1], e.v[2],
                      parent == ~0ull ? std::string("-1") : std::format("0x{:08X}", parent), e.v[4],
                      (e.flags & 4) ? " | within 3 s of a parent change" : "",
                      e.kind == kEvRewrite
                          ? (e.v[6] == 1   ? (e.v[8] ? " -> REWRITTEN (a 0x40 dismount action)" : " -> REWRITTEN")
                             : e.v[6] == 2 ? " -> DEFERRED to mount_sync (validated mount transaction)"
                             : e.v[6] == 3 ? " -> PASSED (ride_dismount=partial: not a 0x40 dismount action)"
                                           : " -> would be rewritten (probe)")
                          : "");
        }
        const uint64_t now = tick_ms();
        if (!g_last_summary_ms) g_last_summary_ms = now;
        if (now - g_last_summary_ms >= kSummaryMs) {
            g_last_summary_ms = now;
            log::info("{}", ride_filter_describe());
        }
    } catch (...) {
    }
}

std::string ride_filter_describe() {
    const auto mode = static_cast<Mode>(g_mode.load());
    if (mode == Mode::Off) return {};
    return std::format(
        "RideFilter summary [{}] ({}): StartActionAcks {} (remote character {}, its key unknown {}; guest keys: host's 0x{:08X}, own 0x{:08X}, connection resets {}) | mount {} "
        "dismount {} | ride_dismount {} (0x40 dismount actions {}, learned 0x40 hashes {}{}) | rewritten {} (write "
        "failed {}, deferred to mount_sync {}, passed {}) | last ride action 0x{:08X} A0 0x{:08X} A1 0x{:08X} | "
        "recorded around riding {} (lines {}, not logged {}, ring dropped {}) | pairs (P parent 0xB, T target "
        "0xB, after = within 3 s of a parent change) [{}] (table full {}, busy {})",
        rfl::mode_name(mode), g_guest.load() ? "guest" : (g_host_attached.load() ? "host" : "host, NOT attached"),
        c_seen.load(), c_remote.load(), c_no_key.load(), g_remote_key.load(), g_local_key.load(), c_conn_resets.load(),
        c_kind[static_cast<size_t>(Kind::Mount)].load(),
        c_kind[static_cast<size_t>(Kind::Dismount)].load(), rfl::dismount_name(ride_dismount_mode()), c_gate.load(),
        g_gate.learned(), learned_text(), c_rewritten.load(), c_write_failed.load(), c_deferred.load(), c_passed.load(),
        g_last_key.load(), g_last_a0.load(), g_last_a1.load(), c_window.load(), g_lines.load(), g_lines_dropped.load(),
        g_ring.dropped(), hex_pairs(), g_pairs.overflow.load(), g_pairs.busy.load());
}

}  // namespace cdmp::session2
