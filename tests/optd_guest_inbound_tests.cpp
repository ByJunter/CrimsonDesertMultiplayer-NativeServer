// P8 guest: the host's packets on their way into the client and the thread
// that does the client's network work (src/mod/session2/guest_inbound_logic.h;
// docs/research/pendientes/p8-auditoria-hilos.md).
//
// SCOPE: offline logic only - the queue between the mod's network thread and
// the client NetworkThread (order, both bounds, the per-turn cap, clear), the
// delivery measures and the thread-owner check (first claim, foreign calls,
// two threads at once). A simulated zone load and a simulated stall of the
// NetworkThread check that nothing is lost or reordered. They do NOT test the
// hooks at 0x35BAD4 / 0x14380B0, ClientNet::OnRecvPacket or the game's threads:
// that is the static analysis of the document and the test in the game.

#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "mod/session2/guest_inbound_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

std::vector<uint8_t> pkt(uint32_t seq, size_t size = 8) {
    std::vector<uint8_t> p(size < 4 ? 4 : size, 0xAB);
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(seq >> (8 * i));
    return p;
}

uint32_t seq_of(const gin::Item& it) {
    uint32_t s = 0;
    for (int i = 0; i < 4; ++i) s |= static_cast<uint32_t>(it.packet[i]) << (8 * i);
    return s;
}

void test_order_and_cap() {
    gin::InboundQueue q({100, 1u << 20});
    for (uint32_t i = 0; i < 10; ++i) CHECK(q.push(pkt(i), i) == gin::Push::Queued);
    CHECK(q.size() == 10 && q.bytes() == 80 && q.peak() == 10);
    std::vector<gin::Item> out;
    CHECK(q.take(4, out) == 4);
    CHECK(out.size() == 4 && q.size() == 6 && q.bytes() == 48);
    for (uint32_t i = 0; i < 4; ++i) CHECK(seq_of(out[i]) == i && out[i].queued_us == i);
    // take appends, still in order.
    CHECK(q.take(100, out) == 6);
    CHECK(out.size() == 10 && q.size() == 0 && q.bytes() == 0);
    for (uint32_t i = 0; i < 10; ++i) CHECK(seq_of(out[i]) == i);
    CHECK(q.take(5, out) == 0);
    CHECK(q.peak() == 10);
}

void test_bounds() {
    // Count bound.
    gin::InboundQueue by_count({3, 1u << 20});
    for (uint32_t i = 0; i < 3; ++i) CHECK(by_count.push(pkt(i), 0) == gin::Push::Queued);
    CHECK(by_count.push(pkt(3), 0) == gin::Push::Full);
    CHECK(by_count.full() == 1 && by_count.size() == 3);
    // Byte bound: exactly at the bound is fine, one more byte is not.
    gin::InboundQueue by_bytes({100, 32});
    CHECK(by_bytes.push(pkt(0, 16), 0) == gin::Push::Queued);
    CHECK(by_bytes.push(pkt(1, 16), 0) == gin::Push::Queued);
    CHECK(by_bytes.bytes() == 32);
    CHECK(by_bytes.push(pkt(2, 4), 0) == gin::Push::Full);
    std::vector<gin::Item> out;
    by_bytes.take(1, out);
    CHECK(by_bytes.push(pkt(2, 16), 0) == gin::Push::Queued);  // room again
    // A packet bigger than the whole budget never goes in.
    gin::InboundQueue tiny({100, 8});
    CHECK(tiny.push(pkt(0, 9), 0) == gin::Push::Full);
    // Clear: counted, empty, and usable again.
    CHECK(by_count.clear() == 3);
    CHECK(by_count.size() == 0 && by_count.bytes() == 0 && by_count.cleared() == 3);
    CHECK(by_count.push(pkt(9), 0) == gin::Push::Queued);
    // The defaults are the guest's: 64 MB as before, 1 M packets, 4096 per turn.
    static_assert(gin::kLimits.max_bytes == (size_t{64} << 20));
    static_assert(gin::kLimits.max_packets == (size_t{1} << 20));
    static_assert(gin::kMaxPerTurn == 4096);
}

void test_stats() {
    gin::DeliveryStats total, turn;
    turn.on_batch(0, 0);
    CHECK(turn.batches == 0);
    turn.on_batch(3, 0);
    turn.on_delivered(1000, 3000);   // 2 ms
    turn.on_delivered(1000, 5000);   // 4 ms
    turn.on_delivered(9000, 5000);   // clock went "back": 0, never negative
    CHECK(turn.batches == 1 && turn.batch_max == 3 && turn.capped == 0 && turn.delivered == 3);
    CHECK(turn.wait_max_us == 4000 && turn.wait_sum_us == 6000);
    total.merge(turn);
    gin::DeliveryStats t2;
    t2.on_batch(5, 7);
    t2.on_delivered(0, 10000);
    total.merge(t2);
    CHECK(total.batches == 2 && total.batch_max == 5 && total.capped == 1 && total.delivered == 4);
    CHECK(total.wait_max_us == 10000);
    CHECK(total.wait_mean_ms() > 3.99 && total.wait_mean_ms() < 4.01);  // 16000 us / 4
    CHECK(gin::DeliveryStats{}.wait_mean_ms() == 0.0);
}

void test_owner() {
    gin::ThreadOwner o;
    CHECK(o.owner() == 0);
    CHECK(o.check(100) == gin::ThreadOwner::Seen::First);
    CHECK(o.check(100) == gin::ThreadOwner::Seen::Owner);
    CHECK(o.owner() == 100 && o.foreign() == 0);
    CHECK(o.check(200) == gin::ThreadOwner::Seen::FirstForeign);
    CHECK(o.check(300) == gin::ThreadOwner::Seen::Foreign);
    CHECK(o.check(100) == gin::ThreadOwner::Seen::Owner);  // the owner never changes
    CHECK(o.owner() == 100 && o.foreign() == 2 && o.last_foreign() == 300);
    // Two real threads, one after the other: the first owns, the second is counted once.
    for (int round = 0; round < 50; ++round) {
        gin::ThreadOwner r;
        int first = 0, foreign_first = 0;
        std::thread a([&] { (r.check(11) == gin::ThreadOwner::Seen::First ? first : foreign_first)++; });
        a.join();
        std::thread b([&] {
            const auto s = r.check(22);
            if (s == gin::ThreadOwner::Seen::First) ++first;
            if (s == gin::ThreadOwner::Seen::FirstForeign) ++foreign_first;
        });
        b.join();
        CHECK(first == 1 && foreign_first == 1 && r.owner() == 11);
    }
}

// A zone load (live: ~42,000 Acks in ~12 s, ~3,500/s) against a NetworkThread
// that turns every 16 ms: never capped, nothing lost, in order, short waits.
void test_zone_load() {
    gin::InboundQueue q;
    gin::DeliveryStats st;
    uint32_t next_in = 0, next_out = 0;
    bool ordered = true;
    std::vector<gin::Item> batch;
    for (uint64_t t_us = 0; t_us < 12'000'000; t_us += 1000) {  // 1 ms steps
        for (int k = 0; k < 4 && next_in < 42'000; ++k) {        // ~3.5/ms
            CHECK(q.push(pkt(next_in++, 40), t_us) == gin::Push::Queued);
        }
        if (t_us % 16'000 == 0) {  // a turn
            batch.clear();
            q.take(gin::kMaxPerTurn, batch);
            st.on_batch(batch.size(), q.size());
            for (auto& it : batch) {
                ordered = ordered && seq_of(it) == next_out++;
                st.on_delivered(it.queued_us, t_us);
            }
        }
    }
    batch.clear();
    q.take(gin::kMaxPerTurn, batch);
    for (auto& it : batch) ordered = ordered && seq_of(it) == next_out++;
    CHECK(ordered);
    CHECK(next_out == 42'000 && q.size() == 0);
    CHECK(st.capped == 0);
    CHECK(st.batch_max <= 64);
    CHECK(st.wait_max_us <= 16'000);
}

// The NetworkThread stops for 5 s (a hitch) while the host keeps sending at
// 3,000/s: 15,000 wait, then go in 4096 per turn, in order, nothing lost.
void test_stall() {
    gin::InboundQueue q;
    gin::DeliveryStats st;
    uint32_t next_in = 0, next_out = 0;
    bool ordered = true;
    std::vector<gin::Item> batch;
    for (uint64_t t_us = 0; t_us < 5'000'000; t_us += 1000) {
        for (int k = 0; k < 3; ++k) CHECK(q.push(pkt(next_in++, 40), t_us) == gin::Push::Queued);
    }
    const size_t waiting = q.size();
    CHECK(waiting == 15'000 && q.peak() == 15'000);
    int turns = 0;
    while (q.size()) {
        batch.clear();
        q.take(gin::kMaxPerTurn, batch);
        st.on_batch(batch.size(), q.size());
        for (auto& it : batch) {
            ordered = ordered && seq_of(it) == next_out++;
            st.on_delivered(it.queued_us, 5'000'000 + 16'000ull * static_cast<uint64_t>(turns));
        }
        ++turns;
    }
    CHECK(ordered && next_out == next_in);
    CHECK(turns == 4);           // 4096 + 4096 + 4096 + 2712
    CHECK(st.capped == 3 && st.batch_max == gin::kMaxPerTurn);
    CHECK(st.wait_max_us >= 5'000'000);
}

}  // namespace

int main() {
    test_order_and_cap();
    test_bounds();
    test_stats();
    test_owner();
    test_zone_load();
    test_stall();
    if (g_failures) {
        std::printf("optd_guest_inbound: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd_guest_inbound: all OK\n");
    return 0;
}
