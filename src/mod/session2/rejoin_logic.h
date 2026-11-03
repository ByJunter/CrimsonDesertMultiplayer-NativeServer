#pragma once
// P8 re-entry (docs/research/s25/reentrada.md §3): the guest goes back to the title and presses Continue in the same
// game run and plays on the HOST's server again, instead of on its own. Pure logic, no game access (tests:
// tests/optd_rejoin_tests.cpp).
//
// Until now the guest allowed ONE remote connection per game run (guest.cpp g_had_remote), and the host's C4 only
// started its next guest epoch on a newer TRANSPORT connection (a new ClientSession Hello), which a guest game that
// keeps running never makes. The re-entry is a new CONVERSATION inside the same tunnel connection instead: the
// reliable channel keeps the order, so everything the guest sends after its REJOIN and everything the host sends
// after its EPOCH belong to the new epoch.
//
// [session2] rejoin = 0 (default) | probe | 1, on BOTH PCs (the host also needs reconnect=1).
//   GUEST: announces caps 0x08; once its remote conversation is over (the host answered its goodbye 2002, or the
//          connection to the host stopped), asks the host for a new epoch (state 13, every kRetryMs, at most kMaxAsks
//          times) and, when the host says it began one for it (state 14) and a "ready" came after that, its client's
//          next connection goes to the host. probe: the requests say "probe", the host starts nothing, and the guest's
//          decision stays local (it logs what it would do).
//   HOST:  in C4's READY, a REJOIN starts the next epoch on the same connection (GuestLifecycle::start_epoch_inband)
//          and answers EPOCH; it sends EPOCH once a second (its C4 state) to a guest that announced caps 0x08.
//          probe: answers "would start" and starts nothing.
// No protocol kVersion change: protocol.cpp is untouched; these are tunnel control frames (opcode 1, slot -1) inside
// RelKind::TrocFrame, like states 3-12. An older host drops a 13 as no server Req (the guest never sees a 14 and
// stays local, as before); an older guest never announces 0x08 (the host sends no 14).
//
// Frames (LE): u16 opcode 1 | i8 -1 | u16 len | payload
//   13 REJOIN (guest -> host), 11 bytes: u8 13 | u8 version 1 | u8 kind (1 real, 2 probe) | u32 the guest's
//      conversation it asks for (2 = the second remote conversation of its run) | u32 the last host epoch it played
//   14 EPOCH (host -> guest), 12 bytes: u8 14 | u8 version 1 | u8 C4 state (HostState) | u8 reason (Reason) |
//      u32 the host's C4 epoch | u32 the guest conversation that epoch was started for in-band (0 = none)

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

#include "mod/session2/user2_funnels_logic.h"

namespace cdmp::session2::rjl {

using u2f::Mode;
using u2f::mode_name;
using u2f::parse_mode;

inline constexpr uint16_t kControlOpcode = 1;    // troc_frame.h kControlOpcode
inline constexpr uint8_t kControlRejoin = 13;    // troc_frame.h kControlRejoin
inline constexpr uint8_t kControlEpoch = 14;     // troc_frame.h kControlEpoch
inline constexpr uint8_t kCapsRejoin = 0x08;     // troc_frame.h kCapsRejoin
inline constexpr uint8_t kWireVersion = 1;
inline constexpr size_t kRejoinPayload = 11, kEpochPayload = 12;
inline constexpr uint64_t kRetryMs = 1000;
inline constexpr uint32_t kMaxAsks = 30;

enum class Kind : uint8_t { Real = 1, Probe = 2 };
enum class HostState : uint8_t { Active = 0, Retiring = 1, Clean = 2, Ready = 3, Quarantined = 4 };
enum class Reason : uint8_t { Heartbeat = 0, StartRejoin = 1, StartConnection = 2, ProbeWould = 3, Ignored = 4 };

inline const char* host_state_name(HostState s) {
    switch (s) {
        case HostState::Active: return "ACTIVE";
        case HostState::Retiring: return "RETIRING";
        case HostState::Clean: return "CLEAN";
        case HostState::Ready: return "READY";
        case HostState::Quarantined: return "QUARANTINED";
    }
    return "?";
}
inline const char* reason_name(Reason r) {
    switch (r) {
        case Reason::Heartbeat: return "heartbeat";
        case Reason::StartRejoin: return "started in-band for the re-entry";
        case Reason::StartConnection: return "started on a new connection";
        case Reason::ProbeWould: return "probe: would start";
        case Reason::Ignored: return "re-entry not possible now";
    }
    return "?";
}

struct Rejoin {
    Kind kind = Kind::Real;
    uint32_t conversation = 0;
    uint32_t last_epoch = 0;
};
struct Epoch {
    HostState state = HostState::Active;
    Reason reason = Reason::Heartbeat;
    uint32_t epoch = 0;
    uint32_t conversation = 0;
};

namespace detail {
inline void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t get32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}
template <size_t N>
inline std::array<uint8_t, 5 + N> frame_of(const std::array<uint8_t, N>& payload) {
    std::array<uint8_t, 5 + N> f{};
    f[0] = kControlOpcode & 0xFF;
    f[1] = kControlOpcode >> 8;
    f[2] = 0xFF;  // slot -1
    f[3] = static_cast<uint8_t>(N);
    f[4] = 0;
    for (size_t i = 0; i < N; ++i) f[5 + i] = payload[i];
    return f;
}
inline bool is_frame(std::span<const uint8_t> f, uint8_t state, size_t payload) {
    return f.size() == 5 + payload && f[0] == (kControlOpcode & 0xFF) && f[1] == (kControlOpcode >> 8) &&
           f[2] == 0xFF && f[3] == payload && f[4] == 0 && f[5] == state && f[6] == kWireVersion;
}
}  // namespace detail

inline std::array<uint8_t, kRejoinPayload> encode_rejoin(const Rejoin& r) {
    std::array<uint8_t, kRejoinPayload> p{};
    p[0] = kControlRejoin;
    p[1] = kWireVersion;
    p[2] = static_cast<uint8_t>(r.kind);
    detail::put32(p.data() + 3, r.conversation);
    detail::put32(p.data() + 7, r.last_epoch);
    return p;
}
inline std::optional<Rejoin> decode_rejoin(std::span<const uint8_t> p) {
    if (p.size() != kRejoinPayload || p[0] != kControlRejoin || p[1] != kWireVersion) return std::nullopt;
    if (p[2] != static_cast<uint8_t>(Kind::Real) && p[2] != static_cast<uint8_t>(Kind::Probe)) return std::nullopt;
    Rejoin r;
    r.kind = static_cast<Kind>(p[2]);
    r.conversation = detail::get32(p.data() + 3);
    r.last_epoch = detail::get32(p.data() + 7);
    if (r.conversation < 2) return std::nullopt;  // the first conversation is never asked for
    return r;
}
inline std::array<uint8_t, kEpochPayload> encode_epoch(const Epoch& e) {
    std::array<uint8_t, kEpochPayload> p{};
    p[0] = kControlEpoch;
    p[1] = kWireVersion;
    p[2] = static_cast<uint8_t>(e.state);
    p[3] = static_cast<uint8_t>(e.reason);
    detail::put32(p.data() + 4, e.epoch);
    detail::put32(p.data() + 8, e.conversation);
    return p;
}
inline std::optional<Epoch> decode_epoch(std::span<const uint8_t> p) {
    if (p.size() != kEpochPayload || p[0] != kControlEpoch || p[1] != kWireVersion) return std::nullopt;
    if (p[2] > static_cast<uint8_t>(HostState::Quarantined) || p[3] > static_cast<uint8_t>(Reason::Ignored)) {
        return std::nullopt;
    }
    Epoch e;
    e.state = static_cast<HostState>(p[2]);
    e.reason = static_cast<Reason>(p[3]);
    e.epoch = detail::get32(p.data() + 4);
    e.conversation = detail::get32(p.data() + 8);
    return e;
}
inline std::array<uint8_t, 5 + kRejoinPayload> rejoin_frame(const Rejoin& r) { return detail::frame_of(encode_rejoin(r)); }
inline std::array<uint8_t, 5 + kEpochPayload> epoch_frame(const Epoch& e) { return detail::frame_of(encode_epoch(e)); }
// Only these exact forms are taken out of a stream (any other opcode-1 frame goes on to the usual checks).
inline bool is_rejoin_frame(std::span<const uint8_t> f) { return detail::is_frame(f, kControlRejoin, kRejoinPayload); }
inline bool is_epoch_frame(std::span<const uint8_t> f) { return detail::is_frame(f, kControlEpoch, kEpochPayload); }
inline std::optional<Rejoin> parse_rejoin_frame(std::span<const uint8_t> f) {
    return is_rejoin_frame(f) ? decode_rejoin(f.subspan(5)) : std::nullopt;
}
inline std::optional<Epoch> parse_epoch_frame(std::span<const uint8_t> f) {
    return is_epoch_frame(f) ? decode_epoch(f.subspan(5)) : std::nullopt;
}

// ---------------------------------------------------------------- the host: what a REJOIN does

enum class HostAction : uint8_t {
    StartInBand,  // READY: begin the next epoch on this connection, answer EPOCH(ACTIVE, StartRejoin)
    Repeat,       // ACTIVE, started in-band for this very conversation and the guest has not spoken yet: answer again
    ProbeWould,   // READY, but probe (this host or the guest's request): answer EPOCH(READY, ProbeWould), start nothing
    Ignore,       // anything else: answer EPOCH(state, Ignored)
    Off,          // rejoin=0 here: consumed and counted, no answer (the guest sees no 14: an older host for it)
};
inline const char* host_action_name(HostAction a) {
    switch (a) {
        case HostAction::StartInBand: return "start the next epoch in-band";
        case HostAction::Repeat: return "already started for it: answer again";
        case HostAction::ProbeWould: return "probe: would start the next epoch";
        case HostAction::Ignore: return "not now: answer the state";
        case HostAction::Off: return "rejoin=0 here: ignored";
    }
    return "?";
}
struct HostIn {
    Mode mode = Mode::Off;
    HostState state = HostState::Active;
    bool conversation = false;     // ACTIVE and a packet of the guest was admitted in this epoch
    uint32_t started_for = 0;      // the guest conversation the current epoch was started for in-band (0 = none)
    Rejoin request;
};
inline HostAction host_decide(const HostIn& in) {
    if (in.mode == Mode::Off) return HostAction::Off;
    if (in.state == HostState::Ready) {
        return in.mode == Mode::Probe || in.request.kind == Kind::Probe ? HostAction::ProbeWould : HostAction::StartInBand;
    }
    // ACTIVE and nobody has spoken in this epoch yet: started in-band for this very conversation (an answer lost or
    // overtaken), or started by a new transport connection (review of c4367f8, 8: that epoch is free for whoever
    // speaks first on the current connection, so it may as well be this guest's). A probing request never takes it.
    if (in.state == HostState::Active && !in.conversation && in.request.kind == Kind::Real &&
        (in.started_for == 0 || in.started_for == in.request.conversation)) {
        return HostAction::Repeat;
    }
    return HostAction::Ignore;
}

// The conversation the host's heartbeat names (review of c4367f8, 2): the in-band start's conversation only while
// nobody has spoken in the epoch; once a conversation is bound, 0 (a heartbeat must never look like a grant of an
// epoch that is already being played).
inline uint32_t heartbeat_conversation(bool conversation_bound, uint32_t started_for) {
    return conversation_bound ? 0 : started_for;
}

// Review of c4367f8, 1: an epoch the host began for a re-entry and nobody used (the guest's client went to its own
// server, or the guest closed the game) is given up after kGrantUnusedMs: its logout finds nothing (no packet reached
// the server) and C4 is READY again.
inline constexpr uint64_t kGrantUnusedMs = 60000;
inline bool grant_unused(HostState state, bool conversation_bound, uint32_t started_for, uint64_t started_ms,
                         uint64_t now_ms) {
    return state == HostState::Active && !conversation_bound && started_for != 0 && now_ms >= started_ms &&
           now_ms - started_ms >= kGrantUnusedMs;
}

// Review of c4367f8, 6: rejoin=0 must behave exactly as before. What only a re-entry needs is done only for a
// re-entry conversation (the second remote conversation of a run, or later) or, on the host, only with rejoin on.
inline bool reentry_conversation(uint32_t conversation) { return conversation >= 2; }
inline bool host_forgets_user2_keys(Mode m) { return m != Mode::Off; }

// ---------------------------------------------------------------- the guest's state machine (not thread-safe)

enum class Phase : uint8_t {
    NoRemote,  // no remote conversation in this run yet (the first one is decided as always)
    Playing,   // a remote conversation is going on
    Over,      // it ended and nothing is asked (rejoin=0, or the host never said it can)
    Asking,    // REJOIN sent every kRetryMs, waiting for the host's EPOCH
    Granted,   // the host began an epoch for the next conversation
    Refused,   // no re-entry in this run (why: refusal())
};
enum class Refusal : uint8_t { None, LocalGame, HostDead, HostQuarantined, NoAnswer, LocalConnection };

inline const char* phase_name(Phase p) {
    switch (p) {
        case Phase::NoRemote: return "no remote conversation yet";
        case Phase::Playing: return "playing on the host";
        case Phase::Over: return "over (nothing asked)";
        case Phase::Asking: return "asking the host";
        case Phase::Granted: return "granted";
        case Phase::Refused: return "refused";
    }
    return "?";
}
inline const char* refusal_text(Refusal r) {
    switch (r) {
        case Refusal::None: return "";
        case Refusal::LocalGame: return "a game was loaded on this PC's own server after the remote conversation";
        case Refusal::HostDead: return "the host stopped its end of the tunnel";
        case Refusal::HostQuarantined: return "the host's C4 is quarantined (no more logins in its run)";
        case Refusal::NoAnswer:
            return "the host never answered the re-entry requests (no EPOCH frame after the maximum number of requests)";
        case Refusal::LocalConnection:
            return "the client's first connection after the remote conversation went to this PC's own server (its "
                   "title and Continue now load a local game)";
    }
    return "?";
}

struct Verdict {
    bool remote = false;  // the connection goes to the host (rejoin=1 only)
    bool would = false;   // probe: it would have gone
    std::string why;
};

class GuestRejoin {
public:
    explicit GuestRejoin(Mode m = Mode::Off) : mode_(m) {}
    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }

    // The host's EPOCH frame (any phase).
    void on_epoch(const Epoch& e, uint64_t now_ms) {
        host_capable_ = true;
        last_state_ = e.state;
        last_epoch_ = e.epoch;
        ++epochs_seen_;
        silent_ = 0;  // the host answers: not silence (review of c4367f8, 5)
        // Review of c4367f8, 2: the epoch being played is the newest ACTIVE one the host named while playing (an EPOCH
        // may come only after the client's first packet): never granted again for the next conversation.
        if (phase_ == Phase::Playing && e.state == HostState::Active && e.epoch > played_epoch_) played_epoch_ = e.epoch;
        if (e.state == HostState::Quarantined && phase_ != Phase::Playing && phase_ != Phase::NoRemote) {
            refuse(Refusal::HostQuarantined);
            return;
        }
        if (phase_ == Phase::Asking && e.conversation == asking_ && e.conversation != 0) {
            if (e.reason == Reason::ProbeWould) {
                probe_would_ = true;
                probe_epoch_ = e.epoch + 1;
            } else if (e.state == HostState::Active && e.epoch > played_epoch_ &&
                       (e.reason == Reason::StartRejoin || e.reason == Reason::Heartbeat)) {
                phase_ = Phase::Granted;
                granted_epoch_ = e.epoch;
                granted_ms_ = now_ms;
                granted_ready_ = ready_count_;
            }
        }
    }
    // A "ready" (state 1) from the host: its count goes up (the decision needs one AFTER the grant).
    void on_ready() { ++ready_count_; }
    uint64_t ready_count() const { return ready_count_; }

    // A client connection was decided REMOTE: conversation n begins (1 the first, 2 the second ...).
    uint32_t on_remote_begin(uint64_t now_ms) {
        played_epoch_ = phase_ == Phase::Granted ? granted_epoch_ : last_epoch_;
        ++conversation_;
        phase_ = Phase::Playing;
        began_ms_ = now_ms;
        asks_ = 0;
        probe_would_ = false;
        return conversation_;
    }
    // The remote conversation is over (the host answered the goodbye, or the connection to the host stopped).
    // True when it changed something (the first notice of this conversation's end).
    bool on_remote_over(uint64_t now_ms) {
        if (phase_ != Phase::Playing) return false;
        over_ready_ = ready_count_;
        if (mode_ == Mode::Off || !host_capable_) {
            phase_ = Phase::Over;
            return true;
        }
        phase_ = Phase::Asking;
        asking_ = conversation_ + 1;
        next_ask_ms_ = now_ms;
        asks_ = 0;
        silent_ = 0;
        return true;
    }
    // A save loaded on this PC's own server (its 3093 on a local connection) after a remote conversation.
    void on_local_game() {
        if (phase_ == Phase::Over || phase_ == Phase::Asking || phase_ == Phase::Granted) refuse(Refusal::LocalGame);
    }
    void on_host_dead() {
        if (phase_ == Phase::Over || phase_ == Phase::Asking || phase_ == Phase::Granted) refuse(Refusal::HostDead);
    }
    // Review of c4367f8, 1: the client's first connection after the remote conversation was decided LOCAL (not
    // granted yet, the tunnel down...). Its title and Continue load a local game now: no re-entry in this run (no more
    // requests, and a late grant is never shown as "the host waits for you").
    void on_local_connection() {
        if (mode_ == Mode::Off) return;  // rejoin=0: as before
        if (phase_ == Phase::Over || phase_ == Phase::Asking || phase_ == Phase::Granted) {
            refuse(Refusal::LocalConnection);
        }
    }

    // The REJOIN to send now, if any. link_up: the tunnel's transport is connected (a request queued while it is not
    // would go nowhere: not sent, not counted). Only SILENCE refuses (review of c4367f8, 5): kMaxAsks requests in a row
    // with no EPOCH frame at all from the host; a host that answers "not now" (RETIRING / CLEAN) is asked on.
    std::optional<Rejoin> due(uint64_t now_ms, bool link_up = true) {
        if (phase_ != Phase::Asking || mode_ == Mode::Off) return std::nullopt;
        if (mode_ == Mode::Probe && probe_would_) return std::nullopt;  // probe: answered, nothing more to ask
        if (now_ms < next_ask_ms_ || !link_up) return std::nullopt;
        if (silent_ >= kMaxAsks) {
            if (mode_ == Mode::Probe) return std::nullopt;  // probe never refuses anything
            refuse(Refusal::NoAnswer);
            return std::nullopt;
        }
        ++asks_;
        ++silent_;
        next_ask_ms_ = now_ms + kRetryMs;
        return Rejoin{mode_ == Mode::Probe ? Kind::Probe : Kind::Real, asking_, played_epoch_};
    }

    // A new client connection after a remote one (decide()'s "had a remote connection" case).
    Verdict judge(uint64_t now_ms) const {
        Verdict v;
        switch (phase_) {
            case Phase::NoRemote:
            case Phase::Playing:
                v.why = "no remote conversation is over yet";
                return v;
            case Phase::Over:
                v.why = mode_ == Mode::Off
                            ? "this game run already had a remote connection (rejoin=0: restart the game to play on "
                              "the host again)"
                            : "this game run already had a remote connection and the host never said it can take the "
                              "guest back (no EPOCH frame: an older host, or its rejoin=0): restart the game";
                return v;
            case Phase::Refused:
                v.why = std::format("no re-entry in this run: {} (restart the game to play on the host again)",
                                    refusal_text(refusal_));
                return v;
            case Phase::Asking:
                if (probe_would_) {
                    v.would = true;
                    v.why = std::format("rejoin=probe: the host would begin epoch {} for conversation {} (probe: "
                                        "nothing started, this connection stays local)",
                                        probe_epoch_, asking_);
                    return v;
                }
                v.why = std::format("re-entry asked {} time(s) for conversation {}; the host has not begun a new epoch "
                                    "(its C4 state {}, epoch {})",
                                    asks_, asking_, host_state_name(last_state_), last_epoch_);
                return v;
            case Phase::Granted:
                if (ready_count_ <= granted_ready_) {
                    v.why = std::format("re-entry granted (host epoch {}) but no 'ready' from the host after it yet",
                                        granted_epoch_);
                    return v;
                }
                v.would = true;
                v.remote = mode_ == Mode::On;
                v.why = std::format("re-entry granted (host epoch {} for conversation {}, {} ms ago){}", granted_epoch_,
                                    asking_, now_ms > granted_ms_ ? now_ms - granted_ms_ : 0,
                                    mode_ == Mode::On ? "" : " (rejoin=probe: would be REMOTE; stays local)");
                return v;
        }
        return v;
    }

    // What the HUD says while the client does not play on the host (Spanish, the user's language); "" = nothing.
    // probe never changes the HUD (review of c4367f8, 7).
    std::string hud_text() const {
        if (mode_ != Mode::On) return {};
        switch (phase_) {
            case Phase::Asking: return "Volviendo al mundo del anfitrión: esperando a que termine tu salida...";
            case Phase::Granted: return "El anfitrión te espera: pulsa Continuar para volver a su mundo.";
            case Phase::Over:
                return "El anfitrión no admite volver a entrar: cierra el juego y ábrelo.";
            case Phase::Refused:
                switch (refusal_) {
                    case Refusal::LocalGame: return "Cargaste tu partida local: para volver, cierra el juego y ábrelo.";
                    case Refusal::HostDead: return "El anfitrión cerró la conexión: cierra el juego y ábrelo.";
                    case Refusal::HostQuarantined: return "El anfitrión no admite más entradas en esta partida.";
                    case Refusal::NoAnswer: return "El anfitrión no respondió: cierra el juego y ábrelo.";
                    case Refusal::LocalConnection:
                        return "No se pudo volver al mundo del anfitrión (tu juego ya está en tu propio servidor): "
                               "cierra el juego y ábrelo.";
                    case Refusal::None: return {};
                }
                return {};
            case Phase::NoRemote:
            case Phase::Playing: return {};
        }
        return {};
    }

    Phase phase() const { return phase_; }
    Refusal refusal() const { return refusal_; }
    uint32_t conversation() const { return conversation_; }
    uint32_t asking() const { return asking_; }
    uint32_t asks() const { return asks_; }
    uint32_t granted_epoch() const { return granted_epoch_; }
    uint32_t played_epoch() const { return played_epoch_; }
    uint32_t last_epoch() const { return last_epoch_; }
    HostState last_state() const { return last_state_; }
    bool host_capable() const { return host_capable_; }
    bool probe_would() const { return probe_would_; }
    uint64_t epochs_seen() const { return epochs_seen_; }
    // B2 (s25/reentrada.md §1.1): the ready count when the last remote conversation ended; a "ready" counts again
    // only once the count is above it.
    uint64_t over_ready() const { return over_ready_; }

private:
    void refuse(Refusal r) {
        if (mode_ == Mode::Probe && r == Refusal::NoAnswer) return;
        phase_ = Phase::Refused;
        refusal_ = r;
    }
    Mode mode_ = Mode::Off;
    Phase phase_ = Phase::NoRemote;
    Refusal refusal_ = Refusal::None;
    bool host_capable_ = false, probe_would_ = false;
    HostState last_state_ = HostState::Active;
    uint32_t last_epoch_ = 0, played_epoch_ = 0, granted_epoch_ = 0, probe_epoch_ = 0;
    uint32_t conversation_ = 0, asking_ = 0, asks_ = 0, silent_ = 0;
    uint64_t next_ask_ms_ = 0, granted_ms_ = 0, began_ms_ = 0;
    uint64_t ready_count_ = 0, granted_ready_ = 0, over_ready_ = 0, epochs_seen_ = 0;
};

}  // namespace cdmp::session2::rjl
