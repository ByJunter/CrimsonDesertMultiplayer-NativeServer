#include "mod/session2/load_trip.h"

#include <windows.h>

#include <atomic>
#include <format>
#include <vector>

#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/load_trip_logic.h"
#include "mod/session2/p8_probes.h"

namespace cdmp::session2 {
namespace {

std::atomic<bool> g_on{false};
p8p::EventRing<2048> g_ring;
std::atomic<uint64_t> g_lost{0}, g_events{0};
uint64_t g_lines = 0;  // diagnostics thread only
constexpr uint64_t kMaxLines = 3000;

ltr::Tracker& tracker() {
    static ltr::Tracker* t = new ltr::Tracker([](uint64_t qpc) { return p8_when(qpc); });  // never destroyed
    return *t;
}

uint16_t op_of(std::span<const uint8_t> p) { return p.size() >= 2 ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : 0; }

uint32_t u32_at(std::span<const uint8_t> b, size_t at) {
    if (b.size() < at + 4) return 0;
    return static_cast<uint32_t>(b[at]) | static_cast<uint32_t>(b[at + 1]) << 8 |
           static_cast<uint32_t>(b[at + 2]) << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
}

void push(uint16_t kind, uint64_t v0, uint64_t v1) noexcept {
    p8p::Event e;
    e.kind = kind;
    e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    e.v[0] = v0;
    e.v[1] = v1;
    e.v[2] = guest_connection_epoch();
    g_events.fetch_add(1, std::memory_order_relaxed);
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

// 2174: the owner at payload +9 (the copy the host rewrote for this client); 2322: its actor key.
uint32_t value_of(uint16_t op, std::span<const uint8_t> payload) {
    if (op == ltr::kOpMoveFieldAck) return u32_at(payload, 9);
    if (op == ltr::kOpMilestone) return u32_at(payload, 0);
    return 0;
}

}  // namespace

void load_trip_start(bool on) {
    if (!on || g_on.exchange(true)) return;
    log::info("GuestLoad ON (load_diag): the client's loading cycles (2174 -> 2549 -> 2144 -> 3066 -> 2322 -> 0x11 exit "
              "-> 2445 -> 2762) with the P8 probes' clock, one summary per trip with its verdict; read only, formatted "
              "on the diagnostics thread (loading states from guest_actor_diag)");
}

bool load_trip_wanted() { return g_on.load(std::memory_order_relaxed); }

void load_trip_on_in(uint16_t opcode, std::span<const uint8_t> payload) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !ltr::followed_in(opcode)) return;
    // P8 S1 option A (guest_seq_route.h): the host's 2322 copied for its own character's manager is not this
    // client's milestone (the trip's verdict counts this client's own only).
    if (opcode == ltr::kOpMilestone && guest_seq_route_other_character(opcode, payload)) return;
    push(ltr::kIn, opcode, value_of(opcode, payload));
}

void load_trip_on_delivered(std::span<const uint8_t> packet) noexcept {
    const uint16_t op = op_of(packet);
    if (!g_on.load(std::memory_order_relaxed) || (op != ltr::kOpMilestone && op != ltr::kOpLoadingCompleteAck)) return;
    if (op == ltr::kOpMilestone && packet.size() > 5 && guest_seq_route_other_character(op, packet.subspan(5))) return;
    push(ltr::kDelivered, op, 0);
}

void load_trip_on_out(std::span<const uint8_t> packet) noexcept {
    const uint16_t op = op_of(packet);
    if (!g_on.load(std::memory_order_relaxed) || !ltr::followed_out(op)) return;
    push(ltr::kOut, op, 0);
}

void load_trip_on_state(int old_state, int new_state, uint64_t qpc_us) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    p8p::Event e;
    e.kind = ltr::kState;
    e.qpc_us = qpc_us;
    e.v[0] = static_cast<uint64_t>(static_cast<int64_t>(old_state));
    e.v[1] = static_cast<uint64_t>(new_state & 0xFF);
    e.v[2] = guest_connection_epoch();
    if (!g_ring.push(e)) g_lost.fetch_add(1, std::memory_order_relaxed);
}

void load_trip_tick() noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    try {
        std::vector<std::string> lines;
        p8p::Event e;
        while (g_ring.pop(e)) tracker().on_event(e, lines);
        tracker().tick(p8_now_us(), lines);
        for (const std::string& s : lines) {
            if (g_lines++ < kMaxLines) log::info("{}", s);
        }
        static uint64_t lost_logged = 0;
        if (const uint64_t lost = g_lost.load(); lost != lost_logged) {
            log::warn("GuestLoad: {} event(s) lost (ring full)", lost - lost_logged);
            lost_logged = lost;
        }
    } catch (...) {
    }
}

std::string load_trip_describe() {
    if (!g_on.load()) return {};
    return std::format(" | GuestLoad events {} lost {}", g_events.load(), g_lost.load());
}

}  // namespace cdmp::session2
