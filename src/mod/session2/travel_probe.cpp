#include "mod/session2/travel_probe.h"
#include "mod/game/build_profile.h"

#include <windows.h>

#include <atomic>
#include <format>
#include <mutex>
#include <string>

#include "mod/log.h"
#include "mod/session2/fast_travel_logic.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

std::atomic<bool> g_alias{true};  // the 2643 alias is on until configured otherwise (as the 2174's, always on)
std::atomic<bool> g_probe{false};
std::atomic<bool> g_started{false};

p8p::EventRing<256> g_ring;
constexpr uint16_t kAck = 10, kSummary = 11;

// The trip (one per 2283 of user 2 that entered the server) and the Ack window of its last 2283 / 2437.
std::atomic<uint32_t> g_trip{0};
std::atomic<uint64_t> g_win_open_ms{0};
std::atomic<uint16_t> g_win_opcode{0};
std::atomic<uint32_t> g_win_index{0};
std::atomic<bool> g_win_summarized{true};
std::atomic<uint32_t> g_win_counts[8] = {};  // 2174, 2643, 3066, 2144, 2762, 2679, 1013, 2322
// P8 S16: the host client's burst after user 2's 2873 (sniffer thread, network thread, the probe's thread).
std::mutex g_hb_mu;
tpl::HostBurst g_hb;  // under g_hb_mu
std::atomic<uint32_t> g_always{0};
std::atomic<uint64_t> g_begin_lines{0};
std::atomic<uint64_t> g_lines[16] = {};

constexpr uint64_t kBeginLines = 16, kLinesPerKind = 32;

int count_index(uint16_t op) {
    switch (op) {
        case tpl::kOpMoveFieldAck: return 0;
        case tpl::kOpLoadingFieldDataAck: return 1;
        case tpl::kOpMoveFieldCompletePostAck: return 2;
        case tpl::kOpMoveFieldCompleteAck: return 3;
        case tpl::kOpLoadingCompleteAck: return 4;
        case tpl::kOpLogoutCharacterAck: return 5;
        case tpl::kOpNak: return 6;
        case tpl::kOpPlayAllPreLoadSequencerAck: return 7;
        default: return -1;
    }
}

tpl::ReplyCounts counts_now() {
    tpl::ReplyCounts c;
    c.n2174 = g_win_counts[0].load();
    c.n2643 = g_win_counts[1].load();
    c.n3066 = g_win_counts[2].load();
    c.n2144 = g_win_counts[3].load();
    c.n2762 = g_win_counts[4].load();
    c.n2679 = g_win_counts[5].load();
    c.nak = g_win_counts[6].load();
    c.n2322 = g_win_counts[7].load();
    return c;
}

void push(uint16_t kind, std::span<const uint64_t> v) noexcept {
    p8p::Event e;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    e.kind = kind;
    for (size_t i = 0; i < v.size() && i < 12; ++i) e.v[i] = v[i];
    g_ring.push(e);  // full: counted by the ring
}

void push_summary() noexcept {
    if (g_win_summarized.exchange(true)) return;
    const tpl::ReplyCounts c = counts_now();
    uint64_t hb = 0;
    {
        std::lock_guard lock(g_hb_mu);
        hb = tpl::pack(g_hb);
    }
    const uint64_t v[] = {g_trip.load(), g_win_opcode.load(), c.n2174, c.n2643, c.n3066, c.n2144, c.n2762,
                          c.n2679, c.nak, g_win_index.load(), c.n2322, hb};
    push(kSummary, v);
}

std::string key(uint64_t k) { return k ? std::format("0x{:08X}", k) : std::string("none"); }

std::string ts(uint64_t packed) {
    const tpl::TsSnap s = tpl::unpack(packed);
    if (!s.ok) return "unreadable";
    return std::format("+0x550 0x{:X} +0x568 {} +0x569 {} +0x591 {}", s.pending, s.f568, s.f569, s.armed);
}

bool budget(uint16_t kind) { return g_lines[kind & 15].fetch_add(1) < kLinesPerKind; }

void format(const p8p::Event& e) {
    const std::string when = p8_when(e.qpc_us);
    switch (e.kind) {
        case kTravelContext: {
            if (!budget(e.kind)) return;
            const bool ok = tpl::context_ok(static_cast<uint32_t>(e.v[6]), static_cast<uint32_t>(e.v[2]), e.v[5], e.v[4]);
            const std::string line = std::format(
                "TravelContext trip {} {}: vf48 returning to exe+0x{:X} | user 2's character {} | field owner {} | the "
                "owner's manager 0x{:X} (its +0x18 {}; read, not called) | returned 0x{:X} (its +0x18 {}) | {} -> {}",
                e.v[0], when, e.v[1], key(e.v[2]), key(e.v[3]), e.v[4], key(e.v[8]), e.v[5], key(e.v[6]),
                e.v[1] == ftl::kApplyVf48Ret ? "second (0x2BC4950)" : "first (the 2283 handler)",
                ok ? "user 2's own (ok)" : "NOT user 2's own");
            if (ok) {
                log::info("{}", line);
            } else {
                log::warn("{}", line);
            }
            return;
        }
        case kTravelPrepared: {
            if (!budget(e.kind)) return;
            tpl::PreparedIn in;
            in.u2_before = tpl::unpack(e.v[2]);
            in.u2_after = tpl::unpack(e.v[3]);
            in.host_before = tpl::unpack(e.v[4]);
            in.host_after = tpl::unpack(e.v[5]);
            in.host_mgr_ok = (e.v[6] & 1) != 0;
            in.host_6a0_before = static_cast<uint8_t>(e.v[6] >> 8);
            in.host_6a0_after = static_cast<uint8_t>(e.v[6] >> 16);
            const tpl::PreparedOut o = tpl::decide_prepared(in);
            const std::string line = std::format(
                "TravelPrepared trip {} {}: user 2's 2283 ({}) | TS_user2 before [{}] after [{}] | TS_host before [{}] "
                "after [{}] | host manager +0x6A0 {} | setter 0x2BC4950 seen {} | F3 {} | ExecuteTroc result {} -> {}",
                e.v[0], when, key(e.v[1]), ts(e.v[2]), ts(e.v[3]), ts(e.v[4]), ts(e.v[5]),
                in.host_mgr_ok ? std::format("{} -> {}", in.host_6a0_before, in.host_6a0_after) : std::string("unreadable"),
                e.v[7] ? "yes" : "no", e.v[9] ? std::format("real manager 0x{:X}", e.v[9]) : e.v[10] ? "block" : "none",
                e.v[11] ? std::format("0x{:X}", e.v[8]) : std::string("unreadable"), tpl::prepared_name(o));
            if (o == tpl::PreparedOut::Prepared) {
                log::info("{}", line);
            } else if (o == tpl::PreparedOut::HostTouched) {
                log::error("{}", line);
            } else {
                log::warn("{}", line);
            }
            return;
        }
        case kTravelRevive: {
            // P8 S27 (read only): user 2's ReviveLoadingReq 2362 around its ExecuteTroc.
            if (!budget(e.kind)) return;
            tpl::PreparedIn in;
            in.u2_before = tpl::unpack(e.v[2]);
            in.u2_after = tpl::unpack(e.v[3]);
            in.host_before = tpl::unpack(e.v[4]);
            in.host_after = tpl::unpack(e.v[5]);
            in.host_mgr_ok = (e.v[6] & 1) != 0;
            in.host_6a0_before = static_cast<uint8_t>(e.v[6] >> 8);
            in.host_6a0_after = static_cast<uint8_t>(e.v[6] >> 16);
            const tpl::PreparedOut o = tpl::decide_prepared(in);
            const std::string line = std::format(
                "TravelRevive trip {} {}: user 2's ReviveLoadingReq 2362 ({}) | TS_user2 before [{}] after [{}] | TS_host "
                "before [{}] after [{}] | host manager +0x6A0 {} | ExecuteTroc result {} -> {}{} | F4: {}",
                e.v[0], when, key(e.v[1]), ts(e.v[2]), ts(e.v[3]), ts(e.v[4]), ts(e.v[5]),
                in.host_mgr_ok ? std::format("{} -> {}", in.host_6a0_before, in.host_6a0_after) : std::string("unreadable"),
                e.v[8] ? std::format("0x{:X}", e.v[7]) : std::string("unreadable"), tpl::revive_name(o),
                o == tpl::PreparedOut::Prepared && in.u2_after.f569 ? " (+0x569: its 2437 reloads the same field)" : "",
                e.v[9] ? ftl::f4_out_name(static_cast<ftl::F4Out>(e.v[9] - 1))
                       : "the setter's vf48 was not reached under this 2362 (or F4 not installed)");
            if (o == tpl::PreparedOut::Prepared) {
                log::info("{}", line);
            } else if (o == tpl::PreparedOut::HostTouched) {
                log::error("{}", line);
            } else {
                log::warn("{}", line);
            }
            return;
        }
        case kTravelConsume: {
            if (!budget(e.kind)) return;
            const tpl::TsSnap before = tpl::unpack(e.v[2]);
            const tpl::ConsumeOut o = tpl::decide_consume(before);
            const std::string line = std::format(
                "TravelConsume trip {} {}: user 2's 2437 ({}) | TS_user2 before [{}] after [{}] | 0x2BC9200: {} ({}) | "
                "ExecuteTroc result {}",
                e.v[0], when, key(e.v[1]), ts(e.v[2]), ts(e.v[3]), tpl::consume_name(o), tpl::consume_branch(before),
                e.v[5] ? std::format("0x{:X}", e.v[4]) : std::string("unreadable"));
            if (o == tpl::ConsumeOut::Pending) {
                log::info("{}", line);
            } else {
                log::warn("{}", line);
            }
            return;
        }
        case kAck: {
            const auto op = static_cast<uint16_t>(e.v[1]);
            const auto alias = static_cast<tpl::AliasOut>(e.v[5]);
            std::string extra;
            if (op == tpl::kOpLoadingFieldDataAck && e.v[10] != ~0ull) {
                extra = std::format(" | actor {} FieldInfoKey 0x{:X} records {} append {} @0 {}", key(e.v[9]), e.v[8],
                                    e.v[10] & 0xFFFFFFFF, (e.v[10] >> 32) & 0xFF, (e.v[10] >> 40) & 0xFF);
            } else if (op == tpl::kOpLogoutCharacterAck) {
                extra = std::format(" | actor {} reason 0x{:02X} error 0x{:08X}{}", key(e.v[8]), e.v[9], e.v[10],
                                    e.v[10] == tpl::kErrInvalidFieldInfoKey ? " (eErrNoInvalidFieldInfoKey)" : "");
            }
            const std::string owner = alias == tpl::AliasOut::NotOwnerAck
                                          ? std::string()
                                          : std::format(" | owner as written {} / user 2's copy {} ({})", key(e.v[3]),
                                                        key(e.v[4]), tpl::alias_name(alias));
            const char* name = tpl::ack_name(op);
            const std::string line =
                std::format("TravelAck trip {} {}: #{} +{} ms to user 2: {}{}{} ({} bytes){}{}{}", e.v[0], when, e.v[7],
                            e.v[11], op, *name ? " " : "", name, e.v[2], owner, extra, e.v[6] ? "" : " NOT queued");
            if (tpl::always_noted(op) || !e.v[6]) {
                log::warn("{}", line);
            } else {
                log::info("{}", line);
            }
            return;
        }
        case kSummary: {
            tpl::ReplyCounts c;
            c.n2174 = static_cast<uint32_t>(e.v[2]);
            c.n2643 = static_cast<uint32_t>(e.v[3]);
            c.n3066 = static_cast<uint32_t>(e.v[4]);
            c.n2144 = static_cast<uint32_t>(e.v[5]);
            c.n2762 = static_cast<uint32_t>(e.v[6]);
            c.n2679 = static_cast<uint32_t>(e.v[7]);
            c.nak = static_cast<uint32_t>(e.v[8]);
            c.n2322 = static_cast<uint32_t>(e.v[10]);
            const tpl::Reply r = tpl::classify_reply(c);
            const tpl::HostBurstView hb = tpl::unpack_host(e.v[11]);
            const std::string host =
                hb.armed ? std::format("user 2's 2873 {}: the host's client got 2661 x{} 2981 x{} 2322 x{} within 3 s -> {}",
                                       hb.handed ? "went to the server" : "was DROPPED (observer_milestone_reload)",
                                       hb.n2661, hb.n2981, hb.n2322, tpl::host_effect_name(hb.effect))
                         : std::string(tpl::host_effect_name(hb.effect));
            const std::string line = std::format(
                "TravelAck trip {} after user 2's {}: {} | 2643 {} 3066 {} 2174 {} 2144 {} 2762 {} 2679 {} Nak {} | "
                "2322 to user 2 {} | host side effect: {} | Acks seen {}",
                e.v[0], e.v[1], tpl::reply_name(r), c.n2643, c.n3066, c.n2174, c.n2144, c.n2762, c.n2679, c.nak,
                c.n2322, host, e.v[9]);
            if (r == tpl::Reply::Refused2679 || r == tpl::Reply::Nak || hb.effect == tpl::HostEffect::SideEffect) {
                log::warn("{}", line);
            } else {
                log::info("{}", line);
            }
            return;
        }
        default: return;
    }
}

DWORD WINAPI probe_thread(LPVOID) {
    for (;;) {
        Sleep(200);
        p8p::Event e;
        for (int n = 0; n < 64 && g_ring.pop(e); ++n) format(e);
        const uint64_t opened = g_win_open_ms.load();
        if (opened && !g_win_summarized.load() && GetTickCount64() - opened > tpl::kWindowMs) push_summary();
    }
}

}  // namespace

void travel_configure(bool loading_owner_alias, bool travel_probe) {
    g_alias.store(loading_owner_alias);
    g_probe.store(travel_probe);
    log::info("Travel (host): user2_loading_owner_alias={}: user 2's copy of LoadingFieldDataAck 2643 {} | "
              "user2_travel_probe={}{}",
              loading_owner_alias ? 1 : 0,
              loading_owner_alias ? "names user 2 as the field owner (u32 +9, layout 20 + 48n), as its MoveFieldAck 2174"
                                  : "keeps the owner the server wrote (the host)",
              travel_probe ? 1 : 0,
              travel_probe ? ": TravelBegin / TravelContext / TravelPrepared / TravelConsume / TravelAck lines (read "
                             "only; the server lines need user2_fast_travel=1)"
                           : "");
    if (travel_probe && !g_started.exchange(true)) CloseHandle(CreateThread(nullptr, 0, probe_thread, nullptr, 0, nullptr));
}

bool travel_loading_owner_alias() noexcept { return g_alias.load(std::memory_order_relaxed); }
bool travel_probe_on() noexcept { return g_probe.load(std::memory_order_relaxed); }

void travel_probe_begin(const TravelBeginInfo& b) {
    if (!travel_probe_on() || g_begin_lines.fetch_add(1) >= kBeginLines) return;
    std::string dest;
    if (b.reload) {
        const ftl::Reload& r = *b.reload;
        dest = std::format("WakeSpot u32 {} | FieldInfoKey 0x{:X} | actor selector {} | position ({:.3f}, {:.3f}, {:.3f}) "
                           "rotation ({:.4f}, {:.4f}, {:.4f}, {:.4f}) scale ({:.2f}, {:.2f}, {:.2f}) | flag {}",
                           r.a, r.field_info_key, key(r.target_actor), r.pos[0], r.pos[1], r.pos[2], r.rot[0], r.rot[1],
                           r.rot[2], r.rot[3], r.scale[0], r.scale[1], r.scale[2], r.flag);
    } else {
        dest = std::format("{} bytes, NOT the 53-byte layout of build 2976", b.size);
    }
    log::info("TravelBegin {}: user 2's ReloadStageStartReq 2283 (user {}, character {}) | {} | in {} (owner {}, host {}) "
              "| F2/F3: {} ({}) | next server trip {}",
              p8_when(p8_now_us()), key(b.user2_user), key(b.char_key), dest, b.where, key(b.owner), key(b.host_user),
              b.gate, b.outcome, g_trip.load() + 1);
}

uint32_t travel_probe_open_window(uint16_t opcode) noexcept {
    if (!travel_probe_on()) return 0;
    push_summary();  // the previous window's verdict, if it was not given yet
    // A 2283 or (P8 S27) a revive's 2362 begins a trip; its 2437 opens the next window of the same trip.
    const uint32_t trip = opcode == tpl::kOpReloadStageStartReq || opcode == tpl::kOpReviveLoadingReq
                              ? g_trip.fetch_add(1) + 1
                              : g_trip.load();
    for (auto& c : g_win_counts) c.store(0);
    {
        std::lock_guard lock(g_hb_mu);
        g_hb.armed = false;  // the host's own last 2873 is kept
        g_hb.n2661 = g_hb.n2981 = g_hb.n2322 = 0;
    }
    g_win_index.store(0);
    g_win_opcode.store(opcode);
    g_win_open_ms.store(GetTickCount64());
    g_win_summarized.store(false);
    return trip;
}

uint32_t travel_probe_trip() noexcept { return g_trip.load(std::memory_order_relaxed); }

void travel_probe_on_user2_2873(bool handed) noexcept {
    if (!travel_probe_on()) return;
    try {
        std::lock_guard lock(g_hb_mu);
        if (!g_hb.armed) g_hb.open(p8_now_us(), handed);  // the first 2873 of the window
    } catch (...) {
    }
}

void travel_probe_host_sniffed(const uint8_t* packet, size_t size, uint64_t t_us, bool server_to_client) noexcept {
    if (!travel_probe_on() || !packet || size < 2) return;
    const auto op = static_cast<uint16_t>(packet[0] | packet[1] << 8);
    const bool wanted = server_to_client ? (op == tpl::kOpPrepareSequencerAck || op == tpl::kOpStageBranchStateAck ||
                                            op == tpl::kOpPlayAllPreLoadSequencerAck)
                                         : op == tpl::kOpPreLoadSequencerReq;
    if (!wanted) return;
    try {
        std::lock_guard lock(g_hb_mu);
        g_hb.on_host(op, server_to_client, t_us);
    } catch (...) {
    }
}

void travel_probe_push(uint16_t kind, std::span<const uint64_t> v) noexcept {
    if (travel_probe_on()) push(kind, v);
}

void travel_probe_on_out(uint16_t opcode, size_t size, std::span<const uint8_t> head, uint32_t owner_orig,
                         uint32_t owner_copy, tpl::AliasOut alias, bool pushed) noexcept {
    if (!travel_probe_on()) return;
    const uint64_t now = GetTickCount64();
    const uint64_t opened = g_win_open_ms.load(std::memory_order_relaxed);
    const bool open = opened && now >= opened && now - opened <= tpl::kWindowMs;
    uint32_t index = 0;
    if (open) {
        index = g_win_index.fetch_add(1, std::memory_order_relaxed);
        if (const int i = count_index(opcode); i >= 0) g_win_counts[i].fetch_add(1, std::memory_order_relaxed);
    }
    const bool in_window = open && tpl::ack_in_window(now, opened, index);
    const bool always = tpl::always_noted(opcode) && g_always.fetch_add(1, std::memory_order_relaxed) < tpl::kAlwaysLines;
    if (!in_window && !always) return;
    uint64_t v[12] = {g_trip.load(), opcode, size, owner_orig, owner_copy, static_cast<uint64_t>(alias),
                      pushed ? 1u : 0u, index, 0, 0, ~0ull, open ? now - opened : 0};
    if (opcode == tpl::kOpLoadingFieldDataAck) {
        if (const auto d = tpl::parse_loading_field_data(head, size, game::current_build())) {
            v[8] = d->field_key;
            v[9] = d->actor;
            v[10] = static_cast<uint64_t>(d->records & 0xFFFFFFFF) | (static_cast<uint64_t>(d->append) << 32) |
                    (static_cast<uint64_t>(d->kind) << 40);
        }
    } else if (opcode == tpl::kOpLogoutCharacterAck) {
        if (const auto l = tpl::parse_logout(head)) {
            v[8] = l->actor;
            v[9] = l->reason;
            v[10] = l->error;
        }
    }
    push(kAck, v);
}

}  // namespace cdmp::session2
