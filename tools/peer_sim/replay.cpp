#include "replay.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

#include "common/capture_file.h"
#include "common/client_session.h"
#include "common/clock.h"
#include "common/troc_link.h"
#include "common/troc_script.h"
#include "common/udp.h"
#include "mod/session2/rejoin_logic.h"

using namespace cdmp;
namespace rjl = cdmp::session2::rjl;

namespace {

volatile bool g_replay_running = true;

// Game build 1.0.0.2976: opcodes only mean something for it.
constexpr uint32_t kGameBuildTimestamp = 0x6AB28F00;
constexpr uint16_t kOpLoginUserToFieldServerReq = 2490;
constexpr uint16_t kNakOpcode = 1013;
// Never replayed from a capture: saving and deleting saves (P6 refuses the
// writes anyway; this keeps the server from even trying).
constexpr uint16_t kNeverSend[] = {
    3037,  // SaveAutoReq
    2910,  // SaveToFileCurrentPlayerReq
    2337,  // DeleteSaveDataReq
    2676,  // SaveCharacterCustomizationReq
    2353,  // SaveTargetFieldGimmickSaveDataForDevReq
};

BOOL WINAPI on_ctrl(DWORD) {
    g_replay_running = false;
    return TRUE;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s.push_back(c < 128 ? static_cast<char>(c) : '?');
    return s;
}

uint64_t now_us() {
    LARGE_INTEGER f, q;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&q);
    const auto ff = static_cast<uint64_t>(f.QuadPart), t = static_cast<uint64_t>(q.QuadPart);
    return t / ff * 1000000ull + t % ff * 1000000ull / ff;
}

// Both directions of the conversation with session 2, as a sniffer capture
// (session field = 2), so the usual analysis scripts read it.
class Recorder {
public:
    bool open(const std::wstring& path) {
        if (_wfopen_s(&f_, path.c_str(), L"wb") != 0 || !f_) {
            f_ = nullptr;
            return false;
        }
        const auto h = capture::encode_header(2, 0);
        std::fwrite(h.data(), 1, h.size(), f_);
        return true;
    }
    ~Recorder() {
        if (f_) std::fclose(f_);
    }
    void write(capture::Dir dir, const std::vector<uint8_t>& packet) {
        if (!f_) return;
        capture::Record r;
        r.t_us = now_us();
        r.dir = dir;
        r.size = static_cast<uint16_t>(packet.size());
        r.session = 2;
        r.data = packet;
        const auto b = capture::encode_record(r);
        std::fwrite(b.data(), 1, b.size(), f_);
    }

private:
    FILE* f_ = nullptr;
};

// The Req to play: a script, or the client->server packets of a capture.
class Source {
public:
    virtual ~Source() = default;
    virtual void on_server_packet(const std::vector<uint8_t>& p, uint32_t now) = 0;
    virtual std::vector<std::vector<uint8_t>> tick(uint32_t now) = 0;
    virtual bool finished() const = 0;
    virtual bool failed() const = 0;
    virtual std::string describe() const = 0;
};

class ScriptSource final : public Source {
public:
    explicit ScriptSource(troc::Script s) : p_(std::move(s)) {}
    troc::ScriptPlayer& player() { return p_; }
    void on_server_packet(const std::vector<uint8_t>& p, uint32_t now) override {
        p_.on_server_packet(p, now);
        // learn_child (D-53r): which child and hash were learned, or a bad 2005.
        for (const auto& n : p_.take_notes()) std::printf("Script: %s\n", n.c_str());
    }
    std::vector<std::vector<uint8_t>> tick(uint32_t now) override { return p_.tick(now); }
    bool finished() const override { return p_.state() != troc::ScriptPlayer::State::Running; }
    bool failed() const override { return p_.state() == troc::ScriptPlayer::State::Failed; }
    std::string describe() const override { return p_.describe(); }

private:
    troc::ScriptPlayer p_;
};

class CaptureSource final : public Source {
public:
    struct Item {
        uint64_t at_us;  // since the first packet played
        std::vector<uint8_t> packet;
    };
    explicit CaptureSource(std::deque<Item> items) : items_(std::move(items)), total_(items_.size()) {}
    void on_server_packet(const std::vector<uint8_t>&, uint32_t) override {}
    std::vector<std::vector<uint8_t>> tick(uint32_t now) override {
        std::vector<std::vector<uint8_t>> out;
        if (!started_) {
            started_ = true;
            start_ms_ = now;
        }
        const uint64_t elapsed_us = static_cast<uint64_t>(now - start_ms_) * 1000;
        while (!items_.empty() && items_.front().at_us <= elapsed_us) {
            out.push_back(std::move(items_.front().packet));
            items_.pop_front();
            ++sent_;
        }
        return out;
    }
    bool finished() const override { return items_.empty(); }
    bool failed() const override { return false; }
    std::string describe() const override {
        char b[96];
        std::snprintf(b, sizeof b, "capture replay: sent %zu of %zu packets", sent_, total_);
        return b;
    }

private:
    std::deque<Item> items_;
    size_t total_ = 0, sent_ = 0;
    bool started_ = false;
    uint32_t start_ms_ = 0;
};

std::unique_ptr<Source> load_source(const ReplayOptions& opt) {
    if (!opt.script.empty()) {
        std::ifstream f(opt.script);
        if (!f) {
            std::printf("Cannot read the script %s\n", narrow(opt.script).c_str());
            return nullptr;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        std::string err;
        auto s = troc::parse_script(ss.str(), &err);
        if (!s) {
            std::printf("Script %s: %s\n", narrow(opt.script).c_str(), err.c_str());
            return nullptr;
        }
        auto src = std::make_unique<ScriptSource>(std::move(*s));
        for (const auto& [name, value] : opt.vars) src->player().set_var(name, value);
        std::printf("Script %s: %zu steps\n", narrow(opt.script).c_str(), src->player().steps());
        return src;
    }
    capture::Reader r;
    if (!r.open(opt.capture)) {
        std::printf("Capture %s: %s\n", narrow(opt.capture).c_str(), r.error().c_str());
        return nullptr;
    }
    if (r.pe_timestamp() != kGameBuildTimestamp) {
        std::printf("Capture %s: game build 0x%08X, not 0x%08X (1.0.0.2976): opcodes differ, not played\n",
                    narrow(opt.capture).c_str(), r.pe_timestamp(), kGameBuildTimestamp);
        return nullptr;
    }
    uint64_t account = 1;
    for (const auto& [name, value] : opt.vars) {
        if (name == "ACCOUNT") account = value;
    }
    // 0 is kept out from caution (it is not the host's: b2-login-pasos.md);
    // all ones is the one value that would take the host's user.
    if (account == 0 || account == ~0ull) {
        std::printf("ACCOUNT=0 or all ones is not allowed (all ones would take the host's user); not played\n");
        return nullptr;
    }
    size_t rewritten = 0;
    std::deque<CaptureSource::Item> items;
    std::optional<uint64_t> t0, first_played;
    size_t truncated = 0, skipped = 0;
    while (auto rec = r.next()) {
        if (!t0) t0 = rec->t_us;
        const double t = static_cast<double>(rec->t_us - *t0) / 1e6;
        if (rec->dir != capture::Dir::ClientToServer || t < opt.from_s || t > opt.to_s) continue;
        const uint16_t op = rec->opcode();
        // 1001 is the client's connect frame, not a message the server knows.
        const bool never = std::find(std::begin(kNeverSend), std::end(kNeverSend), op) != std::end(kNeverSend);
        const bool wanted = op != 1001 && !never &&
                            (opt.only.empty() || std::find(opt.only.begin(), opt.only.end(), op) != opt.only.end()) &&
                            std::find(opt.skip.begin(), opt.skip.end(), op) == opt.skip.end();
        if (!wanted) {
            ++skipped;
            continue;
        }
        if (!rec->whole() || !troc::parse(rec->data)) {
            ++truncated;  // a version 1 capture cut it at 1024 bytes
            continue;
        }
        if (op == kOpLoginUserToFieldServerReq && rec->data.size() >= troc::kHeaderSize + 8) {
            // Account 0 is the host's: the server would take it for a relogin of
            // the host and drop the host's own session (segunda-sesion.md §3).
            for (int i = 0; i < 8; ++i) rec->data[troc::kHeaderSize + i] = static_cast<uint8_t>(account >> (8 * i));
            ++rewritten;
        }
        if (!first_played) first_played = rec->t_us;
        items.push_back({rec->t_us - *first_played, std::move(rec->data)});
    }
    if (!r.error().empty()) std::printf("Capture: stopped early: %s\n", r.error().c_str());
    std::printf("Capture %s (v%u): %zu Req to play, %zu filtered out, %zu truncated (left out); "
                "account %llu in %zu LoginUserToFieldServerReq\n",
                narrow(opt.capture).c_str(), r.version(), items.size(), skipped, truncated,
                static_cast<unsigned long long>(account), rewritten);
    if (items.empty()) return nullptr;
    return std::make_unique<CaptureSource>(std::move(items));
}

}  // namespace

int run_replay(const ReplayOptions& opt) {
    auto source = load_source(opt);
    if (!source) return 2;
    // P8 S25 phase B (save_sim.h): the guest's own save, as the guest's mod does it.
    SaveSim save(opt.save);
    if (save.enabled()) {
        uint32_t default_slot = 1;
        for (const auto& [name, value] : opt.vars) {
            if (name == "SAVE_SLOT") default_slot = static_cast<uint32_t>(value);
        }
        std::string why;
        if (!save.load(default_slot, why)) {
            std::printf("Guest save: %s; not played\n", why.c_str());
            return 2;
        }
    }
    Recorder rec;
    if (!opt.record.empty()) {
        if (!rec.open(opt.record)) {
            std::printf("Cannot write %s\n", narrow(opt.record).c_str());
            return 2;
        }
        std::printf("Recording both directions to %s\n", narrow(opt.record).c_str());
    }

    net::UdpSocket sock;
    if (!sock.open(opt.port, !opt.public_bind)) {
        std::printf("Cannot bind UDP port %u: %s\n", opt.port, net::UdpSocket::last_error().c_str());
        return 1;
    }
    SetConsoleCtrlHandler(on_ctrl, TRUE);
    std::printf("cdmp_peer replay listening on %s:%u: start the game with the mod as guest of this "
                "address and [session2] enabled=1 create_session=1 accept_tunnel=1\n",
                opt.public_bind ? "0.0.0.0" : "127.0.0.1", opt.port);

    std::optional<net::Endpoint> client;
    ClientSession session(
        "cdmp_peer (replay)", 0,
        [&](const std::vector<uint8_t>& b) {
            if (client) sock.send_to(*client, b);
        },
        [](const std::string& e) { std::printf("Session: %s\n", e.c_str()); }, ClientSession::Role::Responder);
    std::map<uint16_t, uint64_t> acks;
    std::vector<std::vector<uint8_t>> save_replies;  // pushed from the loop, not from inside the receive handler
    bool host_seen = false;
    // P8 re-entry (--rejoin, rejoin_logic.h): the host's EPOCH frames and "ready" frames, as the guest's mod reads them.
    rjl::GuestRejoin rejoin(opt.rejoin > 0 ? (opt.rejoin_probe ? rjl::Mode::Probe : rjl::Mode::On) : rjl::Mode::Off);
    uint64_t epochs_in = 0;
    troc::TrocLink link(session, troc::Dir::ToHostServer, troc::Dir::ToGuestClient,
                        [&](std::vector<uint8_t>&& p, uint32_t now) {
                            // The host's control states ("session 2 ready", its character, P8 S25's own-save
                            // states): not game packets.
                            if (auto v = troc::parse(p); v && v->opcode == troc::kControlOpcode) {
                                if (const auto e = rjl::parse_epoch_frame(p)) {
                                    const rjl::Phase before = rejoin.phase();
                                    rejoin.on_epoch(*e, now);
                                    if (++epochs_in == 1 || before != rejoin.phase() ||
                                        e->reason != rjl::Reason::Heartbeat) {
                                        std::printf("Rejoin: host EPOCH: C4 %s epoch %u (%s, conversation %u) | %s -> %s\n",
                                                    rjl::host_state_name(e->state), e->epoch,
                                                    rjl::reason_name(e->reason), e->conversation,
                                                    rjl::phase_name(before), rjl::phase_name(rejoin.phase()));
                                    }
                                    return;
                                }
                                if (!v->payload.empty() && v->payload[0] == troc::kControlHostReady) rejoin.on_ready();
                                if (!save.on_host_frame(p, save_replies)) host_seen = true;
                                return;
                            }
                            // parse_wire: also the 7-byte logout Acks 2002/2003 (C4).
                            rec.write(capture::Dir::ServerToClient, p);
                            if (auto v = troc::parse_wire(p, troc::Dir::ToGuestClient)) {
                                ++acks[v->opcode];
                                save.on_host_packet(v->opcode, v->payload);
                                // Nak 1013: the server refused a Req (b2-login-pasos.md: e.g.
                                // UserLogin with the save manager busy). Its payload has the
                                // error code; shown so a failed step says why.
                                if (v->opcode == kNakOpcode && acks[v->opcode] <= 10) {
                                    std::string hexs;
                                    for (uint8_t c : v->payload) {
                                        char h[4];
                                        std::snprintf(h, sizeof h, "%02X", c);
                                        hexs += h;
                                    }
                                    std::printf("Server sent Nak 1013 (a Req was refused): payload %s\n", hexs.c_str());
                                }
                            }
                            source->on_server_packet(p, now);
                        });

    link.set_announce_flags(static_cast<uint8_t>((opt.announce_compress ? troc::kCapsCompressed : 0) |
                                                 (save.enabled() ? troc::kCapsGuestSave : 0) |
                                                 (opt.rejoin > 0 ? troc::kCapsRejoin : 0)));
    int round = 1;               // P8 re-entry: the login being played (1 = the first)
    uint32_t rejoin_since = 0;   // when the asking began (0 = not asking)
    int rounds_failed = 0;
    std::vector<uint8_t> buf(proto::kMaxPacketSize);
    const uint32_t started = now_ms();
    uint32_t connected_at = 0, last_report = now_ms(), finished_at = 0;
    bool playing = false;
    // P8 S25 phase B: the final save's 2910 at --save-at, and the goodbye held behind one (--save-before-bye).
    uint32_t play_started = 0, bye_at = 0, bye_stored = 0, bye_failed = 0;
    bool save_at_done = false, bye_done = false, ack_waited = false;
    std::deque<std::vector<uint8_t>> bye_held;
    auto push_save_req = [&](const char* why) {
        const auto r = SaveSim::final_save_req();
        rec.write(capture::Dir::ClientToServer, r);
        if (!link.push(r)) std::printf("Tunnel refused the SaveToFileCurrentPlayerReq 2910\n");
        std::printf("Guest save: SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op') -> the host's server (%s)\n", why);
    };
    while (g_replay_running) {
        net::Endpoint from;
        for (int i = 0; i < 512; ++i) {
            auto n = sock.recv_from(from, buf, i == 0 ? 2 : 0);
            if (!n) break;
            if (!client) {
                auto p = proto::decode({buf.data(), *n});
                if (!p || !std::holds_alternative<proto::Hello>(p->body)) continue;
                client = from;
                std::printf("Player at %s is joining\n", from.str().c_str());
            }
            if (from == *client) session.on_datagram({buf.data(), *n}, now_ms());
        }
        const uint32_t now = now_ms();
        session.tick(now);
        link.tick(now);
        if (link.connected() && !connected_at) connected_at = now;
        if (!link.connected()) connected_at = 0;
        if (link.broken()) {
            std::printf("The connection restarted: the server's session 2 is in an unknown state. Stopping.\n");
            break;
        }
        // P8 S25 phase B: the own-save answers, and the upload at the host's first control packet (before any Req).
        for (auto& r : save_replies) link.push(r);
        save_replies.clear();
        if (host_seen && link.connected() && save.uploads() && !save.offered()) {
            for (auto& f : save.on_host_seen()) link.push(f);
        }
        // The script's first Req waits for the host's SAVE_ACK (30 s at most), as the guest's 3093 does.
        const bool wait_ack = save.uploads() && !save.ack_result() && connected_at &&
                              now - connected_at < static_cast<uint32_t>(opt.start_after_ms) + 30000;
        if (!playing && connected_at && now - connected_at >= static_cast<uint32_t>(opt.start_after_ms) && !wait_ack &&
            !rejoin_since) {
            playing = true;
            play_started = now;
            if (opt.rejoin > 0) {
                rejoin.on_remote_begin(now);
                std::printf("Rejoin: round %d of %d plays (conversation %u)\n", round, opt.rejoin + 1,
                            rejoin.conversation());
            }
            if (save.uploads() && !ack_waited) {
                ack_waited = true;
                if (!save.ack_result()) {
                    std::printf("Guest save: NO SAVE_ACK in 30 s (the host does not speak states 6-11, or never sent a "
                                "control packet): playing without the own save\n");
                } else if (*save.ack_result() != 0) {
                    std::printf("Guest save: the host REFUSED the upload: playing without the own save\n");
                }
            }
            std::printf("Playing the Req now\n");
        }
        if (playing && !save_at_done && opt.save.save_at_s >= 0 &&
            now - play_started >= static_cast<uint32_t>(opt.save.save_at_s * 1000.0)) {
            save_at_done = true;
            push_save_req("--save-at");
        }
        if (playing && !finished_at) {
            for (auto& p : source->tick(now)) {
                const uint16_t op = p.size() >= 2 ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : 0;
                if (opt.save.save_before_bye && !bye_done && op == troc::kOpLogoutSessionReq) {
                    bye_done = true;
                    bye_at = now;
                    bye_stored = save.stored();
                    bye_failed = save.saves_failed();
                    push_save_req("before the goodbye; the 2001 waits for the SAVE_RESULT, 15 s at most");
                }
                if (bye_at && bye_held.size() < 4096 && (!bye_held.empty() || op == troc::kOpLogoutSessionReq)) {
                    bye_held.push_back(std::move(p));
                    continue;
                }
                rec.write(capture::Dir::ClientToServer, p);
                if (!link.push(p)) std::printf("Tunnel refused a packet\n");
            }
            const bool bye_refused = save.saves_failed() > bye_failed;
            if (!bye_held.empty() && (save.stored() > bye_stored || bye_refused || now - bye_at >= 15000)) {
                std::printf("Guest save: the goodbye goes (%s)\n",
                            save.stored() > bye_stored ? "the final save came back"
                            : bye_refused              ? "the server's 3016 says the final save failed"
                                                       : "no SAVE_RESULT in 15 s");
                for (auto& p : bye_held) {
                    rec.write(capture::Dir::ClientToServer, p);
                    if (!link.push(p)) std::printf("Tunnel refused a packet\n");
                }
                bye_held.clear();
            }
            if (source->finished() && bye_held.empty()) {
                finished_at = now;
                std::printf("%s\n", source->describe().c_str());
                std::printf("Collecting the server's answers for 10 s more\n");
            }
        }
        // P8 re-entry: the round is over; ask for the next one on the same session (REJOIN once a second).
        if (finished_at && now - finished_at >= static_cast<uint32_t>(opt.linger_ms) && round <= opt.rejoin) {
            if (!rejoin_since) {
                rejoin_since = now;
                rejoin.on_remote_over(now);
                std::printf("Rejoin: round %d over; asking the host for conversation %u (%s)\n", round,
                            rejoin.asking(), opt.rejoin_probe ? "probe" : "real");
            }
            if (const auto r = rejoin.due(now, link.connected())) {
                const auto f = rjl::rejoin_frame(*r);
                if (!link.push(std::span<const uint8_t>(f.data(), f.size()))) std::printf("Tunnel refused the REJOIN\n");
                if (rejoin.asks() <= 3) {
                    std::printf("Rejoin: REJOIN #%u for conversation %u (last epoch %u)\n", rejoin.asks(),
                                r->conversation, r->last_epoch);
                }
            }
            const rjl::Verdict v = rejoin.judge(now);
            if (v.remote) {
                std::printf("Rejoin: %s -> round %d\n", v.why.c_str(), round + 1);
                ++round;
                rejoin_since = 0;
                finished_at = 0;
                playing = false;
                connected_at = now;  // --start-after again
                save_at_done = false;
                bye_done = false;
                bye_at = 0;
                bye_held.clear();
                // The old source stays until the new one loads (the summary below reads it).
                auto next = load_source(opt);
                if (!next) {
                    std::printf("Rejoin: the script could not be loaded again; stopping\n");
                    ++rounds_failed;
                    break;
                }
                source = std::move(next);
            } else if (v.would) {
                // Probe on either side (--rejoin-probe here, or rejoin=probe on the host): the host answered "would
                // begin"; nothing was started.
                std::printf("Rejoin: %s; stopping (probe)\n", v.why.c_str());
                break;
            } else if (rejoin.phase() == rjl::Phase::Refused || now - rejoin_since >= 60000) {
                std::printf("Rejoin: no next round: %s\n", v.why.c_str());
                ++rounds_failed;
                break;
            }
            continue;
        }
        if (finished_at && now - finished_at >= static_cast<uint32_t>(opt.linger_ms)) break;
        if (opt.seconds > 0 && now - started >= static_cast<uint32_t>(opt.seconds) * 1000) break;
        if (!opt.quiet && now - last_report >= 2000) {
            last_report = now;
            std::printf("%s | %s%s\n", link.describe().c_str(), source->describe().c_str(),
                        save.enabled() ? (" | " + save.describe()).c_str() : "");
            std::fflush(stdout);
        }
    }
    if (save.enabled()) std::printf("%s\n", save.describe().c_str());
    std::printf("%s\n%s\nAcks received by opcode:", link.describe().c_str(), source->describe().c_str());
    std::vector<std::pair<uint64_t, uint16_t>> top;
    for (const auto& [op, n] : acks) top.push_back({n, op});
    std::sort(top.rbegin(), top.rend());
    for (size_t i = 0; i < top.size() && i < 40; ++i) std::printf(" %u x%llu", top[i].second, top[i].first);
    std::printf("\n");
    if (opt.rejoin > 0) {
        std::printf("Rejoin: %d of %d round(s) played%s | %s\n", round, opt.rejoin + 1,
                    rounds_failed ? " (the next one was not granted)" : "", rejoin.judge(now_ms()).why.c_str());
    }
    session.disconnect(now_ms());
    return source->failed() ? 3 : rounds_failed ? 4 : 0;
}

int run_tunnel_measure(const ReplayOptions& opt) {
    capture::Reader reader;
    if (!reader.open(opt.capture)) {
        std::printf("Cannot read the capture: %s\n", reader.error().c_str());
        return 1;
    }
    std::vector<std::vector<uint8_t>> packets;
    uint64_t first_us = 0, bytes = 0, truncated = 0;
    bool have_first = false;
    while (auto r = reader.next()) {
        if (!have_first) {
            first_us = r->t_us;
            have_first = true;
        }
        const double t = static_cast<double>(r->t_us - first_us) / 1e6;
        if (r->dir != capture::Dir::ServerToClient || t < opt.from_s || t > opt.to_s) continue;
        if (!r->whole()) {
            ++truncated;
            continue;
        }
        if (!troc::parse_wire(r->data, troc::Dir::ToGuestClient)) continue;
        bytes += r->data.size();
        packets.push_back(std::move(r->data));
    }
    std::printf("%zu server->client packets, %llu bytes (%llu truncated ones left out)\n", packets.size(),
                static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(truncated));
    for (const bool compress : {false, true}) {
        uint64_t wire = 0, messages = 0, fragments = 0;
        std::vector<std::vector<uint8_t>> sent;
        troc::TunnelSender sender(troc::Dir::ToGuestClient, [&](const std::vector<uint8_t>& m) {
            wire += m.size();
            ++messages;
            fragments += (m.size() + proto::kRelChunk - 1) / proto::kRelChunk;
            sent.push_back(m);
            return true;
        });
        sender.set_compress(compress);
        size_t flushes = 0;
        uint64_t busy_us = 0;
        auto drain = [&](size_t keep) {
            const uint64_t a = ::cdmp::now_us();
            while (sender.backlog_bytes() > keep && sender.flush() > 0) ++flushes;
            busy_us += ::cdmp::now_us() - a;
        };
        for (const auto& p : packets) {
            if (!sender.push(p)) std::printf("the sender refused a packet\n");
            if (sender.backlog_bytes() > 8u * 1024 * 1024) drain(1024 * 1024);  // stays under its 16 MB bound
        }
        drain(0);
        const uint64_t t0 = 0, t1 = busy_us;
        // The other end: every message decodes back to the same packets, in order.
        troc::TunnelReceiver receiver(troc::Dir::ToGuestClient);
        size_t i = 0;
        bool same = true;
        const uint64_t t2 = ::cdmp::now_us();
        for (const auto& m : sent) {
            for (auto& p : receiver.on_message(m)) {
                same = same && i < packets.size() && p == packets[i];
                ++i;
            }
        }
        const uint64_t t3 = ::cdmp::now_us();
        same = same && i == packets.size() && receiver.rejected() == 0;
        std::printf("%s: %llu messages, %llu fragments, %llu bytes on the wire (x%.1f), batching%s %.1f ms in %zu "
                    "flushes, decoding %.1f ms, back to the same packets in order: %s | at 1 MB/s: %.2f s\n",
                    compress ? "compressed" : "raw", static_cast<unsigned long long>(messages),
                    static_cast<unsigned long long>(fragments), static_cast<unsigned long long>(wire),
                    wire ? static_cast<double>(sender.raw_bytes_sent()) / static_cast<double>(wire) : 0.0,
                    compress ? "+compression" : "", (t1 - t0) / 1000.0, flushes, (t3 - t2) / 1000.0,
                    same ? "yes" : "NO", static_cast<double>(wire) / (1024.0 * 1024.0));
    }
    return 0;
}
