#pragma once
// P8 S2, GUEST: one line per load-related packet and one summary per loading
// cycle ("trip") of the guest's client, correlated by time with its loading
// state (docs/research/pendientes/p8-2322-observador.md §6). The pure logic,
// with no game access, so the tests can drive it.
//
// Inputs (all stamped with the P8 probes' QPC clock, p8_now_us):
//   in        a host packet queued for the client (network thread, guest_deliver):
//             2174 (value = the field owner as written for this client), 2144, 3066,
//             2322 (value = its actor key), 2762, 2042 (a bundle: contents not parsed)
//   delivered the same packet handed to ClientNet::OnRecvPacket (client NetworkThread)
//   out       a client Req sent to the host (guest_send): 2549, 2445
//   state     a change of the client's loading state [P+0x15C] (guest_actor_diag's
//             transitions; they reach the tracker up to ~5 s late: summaries wait)
// A trip begins with a 2174 and ends with the next one (or the next connection).
// Its summary is written once the trip settled (2762 seen or a newer trip, and
// kSettleUs passed), and as a report at +10/+60/+300 s after a 2322 while no
// 2762 came. Read only: nothing here decides anything for the game.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <string>
#include <vector>

#include "mod/session2/p8_probe_logic.h"

namespace cdmp::session2::ltr {

inline constexpr uint16_t kOpMoveFieldAck = 2174, kOpMoveFieldCompleteAck = 2144, kOpPostAck = 3066,
                          kOpMilestone = 2322, kOpLoadingCompleteAck = 2762, kOpBundle = 2042,
                          kOpMoveFieldCompleteReq = 2549, kOpLoadingCompleteReq = 2445;

constexpr bool followed_in(uint16_t op) {
    return op == kOpMoveFieldAck || op == kOpMoveFieldCompleteAck || op == kOpPostAck || op == kOpMilestone ||
           op == kOpLoadingCompleteAck || op == kOpBundle;
}
constexpr bool followed_out(uint16_t op) { return op == kOpMoveFieldCompleteReq || op == kOpLoadingCompleteReq; }

// p8p::Event.kind
enum Ev : uint16_t { kIn = 0, kDelivered, kOut, kState, kEvCount };
// v[0] opcode (kState: old state, -1 = first seen), v[1] value (kState: new state), v[2] connection epoch.

inline constexpr uint64_t kSettleUs = 6'000'000;
inline constexpr uint32_t kReportS[3] = {10, 60, 300};
inline constexpr size_t kMaxTrips = 8;

struct Trip {
    uint32_t n = 0;
    uint64_t conn = 0;
    uint64_t t2174 = 0, t2549 = 0, t2144 = 0, t3066 = 0, t2322 = 0, t2322_dlv = 0, t2445 = 0, t2762 = 0;
    uint64_t t_next = 0;  // the next trip's 2174 (0 = still the current one)
    uint32_t n2144 = 0, n3066 = 0, n2322 = 0, n2445 = 0, n2762 = 0, n2549 = 0, n_bundle = 0;
    uint32_t owner = 0, actor2322 = 0;
    // the loading state inside the trip's time span
    int first_state = -1, last_state = -1;
    uint64_t t_in11 = 0, t_out11 = 0;
    int out11_to = -1;
    uint32_t states = 0;
    bool summarized = false;
    uint8_t reported = 0;
};

inline std::string state_text(int s) { return s < 0 ? std::string("?") : std::format("0x{:02X}", s); }

inline std::string dt(uint64_t a, uint64_t b) {
    if (!a || !b) return "-";
    const double ms = b >= a ? static_cast<double>(b - a) / 1000.0 : -static_cast<double>(a - b) / 1000.0;
    return std::format("{:+.3f} ms", ms);
}

// One verdict per trip (the document's acceptance criteria 1, 4 and 6, seen from the guest).
inline std::string verdict(const Trip& t, bool states_known) {
    std::string v;
    if (!t.n2322) {
        v = "NO 2322 in this trip";
        if (t.t_in11 && !t.t_out11) v += ": the load waits in 0x11 (S2)";
    } else if (t.n2322 > 1) {
        v = std::format("DUPLICATE: {} x 2322 in one trip", t.n2322);
    } else if (!t.t2144 || t.t2322 < t.t2144) {
        v = "ORDER: the 2322 came BEFORE this trip's 2144";
    } else {
        v = "one 2322, after its 2144";
    }
    if (!states_known) return v + " | loading states not observed (guest_actor_diag=0?)";
    if (t.t_out11) {
        if (t.out11_to == 0x12) {
            v += std::format(" | 0x11 -> 0x12 ({} after the 2322)", t.t2322 ? dt(t.t2322, t.t_out11) : std::string("-"));
        } else {
            v += std::format(" | 0x11 -> {}: NOT the normal exit (0x12)", state_text(t.out11_to));
        }
    } else if (t.t_in11) {
        v += " | still in 0x11";
    }
    if (t.n2762) {
        v += t.out11_to == 0x12 ? " | 2445 and 2762: normal load completion" : " | 2762 seen";
    } else if (t.n2445) {
        v += " | 2445 sent, no 2762 yet";
    }
    return v;
}

class Tracker {
public:
    using When = std::function<std::string(uint64_t)>;
    explicit Tracker(When when) : when_(std::move(when)) {}

    // One event (any order between the packet producers and the late state feed).
    void on_event(const p8p::Event& e, std::vector<std::string>& lines) {
        if (e.kind == kState) {
            on_state(e);
            return;
        }
        const auto op = static_cast<uint16_t>(e.v[0]);
        const auto value = static_cast<uint32_t>(e.v[1]);
        const uint64_t conn = e.v[2];
        if (e.kind == kIn && op == kOpMoveFieldAck) {
            if (!trips_.empty()) trips_.back().t_next = e.qpc_us;
            Trip t;
            t.n = ++count_;
            t.conn = conn;
            t.t2174 = e.qpc_us;
            t.owner = value;
            // the states already seen after this 2174 (they may arrive before it): re-attributed below
            trips_.push_back(t);
            while (trips_.size() > kMaxTrips) trips_.pop_front();
            reattribute();
            lines.push_back(std::format("GuestLoad [in 2174] {} T{} | trip {} begins (connection 0x{:X}) | field owner "
                                        "as written for this client 0x{:08X}",
                                        when_(e.qpc_us), e.tid, t.n, conn, value));
            return;
        }
        Trip* t = current();
        if (!t) return;
        if (e.kind == kOut) {
            if (op == kOpMoveFieldCompleteReq) {
                ++t->n2549;
                t->t2549 = e.qpc_us;
            } else if (op == kOpLoadingCompleteReq) {
                ++t->n2445;
                t->t2445 = e.qpc_us;
                lines.push_back(std::format("GuestLoad [out 2445] {} T{} | trip {} | LoadingCompleteReq to the host | {} "
                                            "after the 2322 | last state seen {}",
                                            when_(e.qpc_us), e.tid, t->n, dt(t->t2322, e.qpc_us), state_text(t->last_state)));
            }
            return;
        }
        if (e.kind == kDelivered) {
            if (op == kOpMilestone && !t->t2322_dlv) t->t2322_dlv = e.qpc_us;
            return;
        }
        switch (op) {
            case kOpMoveFieldCompleteAck:
                ++t->n2144;
                t->t2144 = e.qpc_us;
                lines.push_back(std::format("GuestLoad [in 2144] {} T{} | trip {} | MoveFieldCompleteAck #{} | {} after "
                                            "the 2549",
                                            when_(e.qpc_us), e.tid, t->n, t->n2144, dt(t->t2549, e.qpc_us)));
                break;
            case kOpPostAck:
                ++t->n3066;
                if (!t->t3066) t->t3066 = e.qpc_us;
                break;
            case kOpMilestone:
                ++t->n2322;
                if (!t->t2322) t->t2322 = e.qpc_us;
                t->actor2322 = value;
                lines.push_back(std::format(
                    "GuestLoad [in 2322] {} T{} | trip {} | PlayAllPreLoadSequencerAck #{} actor 0x{:08X} | {} after its "
                    "2144 | {} | last state seen {}{}",
                    when_(e.qpc_us), e.tid, t->n, t->n2322, value, dt(t->t2144, e.qpc_us),
                    std::format("corr=2322:{:08X}:0x0:0x0", value), state_text(t->last_state),
                    t->n2322 > 1 ? " | DUPLICATE in this trip" : (!t->t2144 ? " | BEFORE any 2144 of this trip" : "")));
                break;
            case kOpLoadingCompleteAck:
                ++t->n2762;
                t->t2762 = e.qpc_us;
                lines.push_back(std::format("GuestLoad [in 2762] {} T{} | trip {} | LoadingCompleteAck | {} after the 2445 "
                                            "| {} after the 2322",
                                            when_(e.qpc_us), e.tid, t->n, dt(t->t2445, e.qpc_us), dt(t->t2322, e.qpc_us)));
                break;
            case kOpBundle:
                ++t->n_bundle;
                break;
            default: break;
        }
    }

    // The diag worker's tick: summaries of the settled trips and the reports of the stuck one.
    void tick(uint64_t now_us, std::vector<std::string>& lines) {
        for (Trip& t : trips_) {
            const uint64_t end = t.t2762 ? t.t2762 : t.t_next;
            if (!t.summarized && end && now_us >= end + kSettleUs) {
                t.summarized = true;
                lines.push_back(summary(t, "summary"));
                continue;
            }
            if (t.summarized || !t.t2322 || t.n2762) continue;
            for (int i = 0; i < 3; ++i) {
                if (t.reported & (1u << i)) continue;
                if (now_us < t.t2322 + static_cast<uint64_t>(kReportS[i]) * 1'000'000) break;
                t.reported |= static_cast<uint8_t>(1u << i);
                lines.push_back(summary(t, std::format("+{} s after the 2322, no 2762", kReportS[i])));
            }
        }
    }

    const std::deque<Trip>& trips() const { return trips_; }
    bool states_known() const { return states_ > 0; }

private:
    Trip* current() { return trips_.empty() ? nullptr : &trips_.back(); }

    struct StateEv {
        uint64_t t;
        int from, to;
    };

    void on_state(const p8p::Event& e) {
        ++states_;
        const StateEv s{e.qpc_us, static_cast<int>(static_cast<int64_t>(e.v[0])), static_cast<int>(e.v[1] & 0xFF)};
        state_log_.push_back(s);
        while (state_log_.size() > 256) state_log_.pop_front();
        apply(s);
    }

    Trip* trip_at(uint64_t t) {
        for (auto it = trips_.rbegin(); it != trips_.rend(); ++it) {
            if (t >= it->t2174 && (!it->t_next || t < it->t_next)) return &*it;
        }
        return nullptr;
    }

    void apply(const StateEv& s) {
        Trip* t = trip_at(s.t);
        if (!t) return;
        ++t->states;
        if (t->first_state < 0) t->first_state = s.to;
        t->last_state = s.to;
        if (s.to == 0x11 && !t->t_in11) t->t_in11 = s.t;
        if (s.from == 0x11 && !t->t_out11) {
            t->t_out11 = s.t;
            t->out11_to = s.to;
        }
    }

    // A new trip began: the states of its span (already fed) belong to it, not to the previous one.
    void reattribute() {
        for (Trip& t : trips_) {
            if (t.summarized) continue;
            t.first_state = t.last_state = -1;
            t.t_in11 = t.t_out11 = 0;
            t.out11_to = -1;
            t.states = 0;
        }
        for (const StateEv& s : state_log_) apply(s);
    }

    std::string summary(const Trip& t, const std::string& what) const {
        return std::format(
            "GuestLoad trip {} {} (connection 0x{:X}) | owner 0x{:08X} | 2174 {} | 2549 {} | 2144 {} (x{}) | 3066 {} | "
            "2322 {} (x{}, actor 0x{:08X}, to OnRecvPacket {}) | 0x11 in {} out {} -> {} | 2445 {} | 2762 {} | "
            "relative to the 2174; bundles 2042 x{} | VERDICT: {}",
            t.n, what, t.conn, t.owner, when_(t.t2174), dt(t.t2174, t.t2549), dt(t.t2174, t.t2144), t.n2144,
            dt(t.t2174, t.t3066), dt(t.t2174, t.t2322), t.n2322, t.actor2322, dt(t.t2322, t.t2322_dlv),
            dt(t.t2174, t.t_in11), dt(t.t2174, t.t_out11), state_text(t.out11_to), dt(t.t2174, t.t2445),
            dt(t.t2174, t.t2762), t.n_bundle, verdict(t, states_ > 0));
    }

    When when_;
    std::deque<Trip> trips_;
    std::deque<StateEv> state_log_;
    uint32_t count_ = 0;
    uint64_t states_ = 0;
};

}  // namespace cdmp::session2::ltr
