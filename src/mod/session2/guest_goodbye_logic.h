#pragma once
// P8 guest: what to do with the client's goodbye (LogoutSessionToServerReq
// 2001) when the host may not answer it (pendientes/p8-salida-invitado.md).
// Pure logic, no game memory: guest.cpp feeds it atomics and the clock.
//
// The native answer (host's server, live): 2003 + 2002 ("D3/D2 07 FF" + the
// u32 g_ServerFrameworkMode). With a field of the host loaded the server
// first unloads it (RemoveActorAck, >5285 UnloadSyncSceneObjectAck in P8
// session 6, stage A) and only then sends 2003/2002; no crash. Our local
// 2003/2002 cannot do that unload (we do not know the objects): the client
// deletes its own user, which the rewritten 2174 made the field's owner, and
// its StageChartProcessor reads the missing owner 22 ms later (exe+0x2045A6A,
// sessions 5 and 6). So:
//   - no field of the host (the title: the client never sent its 3093):
//     answer locally, as before (that fixed the title's hang, session 1);
//   - a field of the host: never answer locally; end the process once the
//     host cannot answer (the link is broken, or nothing came from the host
//     for kSilentMs after the goodbye, or no 2002 kFieldCapMs after it). The
//     game was going away anyway (Alt+F4, or back to the title, which cannot
//     reconnect to the host in this run).

#include <cstdint>

namespace cdmp::session2::goodbye {

enum class Action : uint8_t {
    Wait,           // nothing yet (no goodbye, already answered, or the host may still answer)
    AnswerLocally,  // inject the native 2003 + 2002 (only without a field of the host)
    EndProcess,     // a field of the host is loaded and the host cannot answer: TerminateProcess
};

constexpr uint64_t kAnswerMs = 5000;    // the host answered in 0.16-0.41 s live
constexpr uint64_t kSilentMs = 5000;    // with a field: nothing at all from the host for this long
constexpr uint64_t kFieldCapMs = 30000; // with a field: no 2002 this long after the goodbye, whatever came

struct State {
    uint64_t now_ms = 0;
    uint64_t goodbye_ms = 0;  // when the client's 2001 went out (0 = none pending)
    uint64_t last_in_ms = 0;  // the last packet of the host's server through the tunnel (0 = none)
    bool answered = false;    // a 2002 (the host's, or ours) reached the client
    bool broken = false;      // the connection to the host stopped: nothing more is delivered
    bool field = false;       // the client asked the host for a field (its 3093 went to the host)
};

constexpr uint64_t elapsed(uint64_t now, uint64_t since) { return now > since ? now - since : 0; }

constexpr Action decide(const State& s) {
    if (!s.goodbye_ms || s.answered) return Action::Wait;
    const uint64_t since_goodbye = elapsed(s.now_ms, s.goodbye_ms);
    if (!s.field) return s.broken || since_goodbye >= kAnswerMs ? Action::AnswerLocally : Action::Wait;
    if (s.broken) return Action::EndProcess;
    // The host's unload stream is still arriving: let it finish (it ends with the real 2003/2002).
    const uint64_t last = s.last_in_ms > s.goodbye_ms ? s.last_in_ms : s.goodbye_ms;
    if (since_goodbye >= kAnswerMs && elapsed(s.now_ms, last) >= kSilentMs) return Action::EndProcess;
    if (since_goodbye >= kFieldCapMs) return Action::EndProcess;
    return Action::Wait;
}

// Why the process ends (for the log line); only meaningful with EndProcess.
constexpr const char* end_reason(const State& s) {
    if (s.broken) return "the connection to the host is broken";
    if (elapsed(s.now_ms, s.last_in_ms > s.goodbye_ms ? s.last_in_ms : s.goodbye_ms) >= kSilentMs) {
        return "nothing came from the host for 5 s after the goodbye";
    }
    return "no 2002 from the host 30 s after the goodbye";
}

}  // namespace cdmp::session2::goodbye
