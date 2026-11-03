// Option D: cdmp_peer's replay mode end to end over real UDP sockets on
// loopback, against a fake host: a ClientSession + TrocLink that answers each
// login Req with the Acks the real server sends (opcodes of build 2976). No
// game. Checks that the script runs to the end, that learned variables go
// into later Req, and that the recording holds both directions.

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../tools/peer_sim/replay.h"
#include "common/capture_file.h"
#include "common/client_session.h"
#include "common/clock.h"
#include "common/troc_frame.h"
#include "common/troc_link.h"
#include "common/udp.h"

using namespace cdmp;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using Bytes = std::vector<uint8_t>;
// Not cdmp_peer's default (a running one is not disturbed), and different per
// process so two test runs (two worktrees) do not collide.
const uint16_t kPort = static_cast<uint16_t>(37115 + GetCurrentProcessId() % 400);

std::wstring temp_file(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::wstring(dir) + name;
}

void write_text(const std::wstring& path, const std::string& text) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"wb");
    if (!f) return;
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

Bytes read_bytes(const std::wstring& path) {
    Bytes b;
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return b;
    std::fseek(f, 0, SEEK_END);
    b.resize(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    if (!b.empty() && std::fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    std::fclose(f);
    return b;
}

// A save file the game's header check takes ("SAVE", v2, 0x80, flags 2, raw = data + 10).
Bytes make_save(uint32_t data, uint8_t seed) {
    Bytes f(0x80 + data);
    const uint32_t magic = 0x45564153, raw = data + 10, flags = 2;
    const uint16_t version = 2, hs = 0x80;
    std::memcpy(f.data(), &magic, 4);
    std::memcpy(f.data() + 4, &version, 2);
    std::memcpy(f.data() + 6, &hs, 2);
    std::memcpy(f.data() + 0xC, &flags, 4);
    std::memcpy(f.data() + 0x12, &raw, 4);
    std::memcpy(f.data() + 0x16, &data, 4);
    for (size_t i = 0x1A; i < f.size(); ++i) f[i] = static_cast<uint8_t>(i * 31 + seed);
    return f;
}

void remove_tree(const std::wstring& dir) {
    for (const wchar_t* sub : {L"", L"\\gen1"}) {
        for (const wchar_t* f : {L"lobby.save", L"save.save", L"manifest.txt"}) {
            DeleteFileW((dir + sub + L"\\" + f).c_str());
        }
    }
    RemoveDirectoryW((dir + L"\\gen1").c_str());
    RemoveDirectoryW(dir.c_str());
}

// P8 S25 phase B (save_sim.h): cdmp_peer as the guest's mod for its own save, against a fake host that speaks the
// tunnel states 6-11 with guest_save_wire.h: the upload at the host's first control packet and before the first Req,
// the script waiting for SAVE_ACK, the goodbye held behind the final SaveToFileCurrentPlayerReq 2910 (slot 100,
// 'Co-op'; a 3037 does not save now: guest_save_wire.h) until the SAVE_RESULT came back and was stored (checked byte
// for byte), and the caps bit. With fail_final the server's 3016 says the save failed: the goodbye goes at once.
void test_guest_save_round_trip(bool fail_final) {
    namespace gsw = cdmp::session2::gsw;
    const uint16_t port = static_cast<uint16_t>(kPort + (fail_final ? 2 : 1));
    const std::wstring script = temp_file(L"cdmp_optd_replay_save.troc");
    const std::wstring in_dir = temp_file(L"cdmp_optd_replay_save_in");
    const std::wstring out_dir = temp_file(L"cdmp_optd_replay_save_out");
    remove_tree(in_dir);
    remove_tree(out_dir);
    CreateDirectoryW(in_dir.c_str(), nullptr);
    const Bytes up_lobby = make_save(390, 1), up_save = make_save(70000, 2);
    const Bytes down_lobby = make_save(400, 3), down_save = make_save(65000, 4);
    write_text(in_dir + L"\\lobby.save", std::string(up_lobby.begin(), up_lobby.end()));
    write_text(in_dir + L"\\save.save", std::string(up_save.begin(), up_save.end()));
    write_text(script, R"(send crypto 2319 -1 0404000000
expect 2840 5000
send login 2490 -1 01000000000000000000000000000000000000
expect 2716 5000
logout_session
expect 2003 10000
expect 2002 10000
)");
    ReplayOptions opt;
    opt.port = port;
    opt.quiet = true;
    opt.seconds = 30;
    opt.script = script;
    opt.start_after_ms = 100;
    opt.linger_ms = 300;
    opt.save.guest_save = in_dir;
    opt.save.results = out_dir;
    opt.save.slot = 101u;
    opt.save.guest_id = 76561198000000001ull;
    opt.save.base_gen = 2;
    opt.save.save_before_bye = true;
    int rc = -1;
    std::thread peer([&] { rc = run_replay(opt); });
    Sleep(300);

    net::UdpSocket sock;
    CHECK(sock.open(0, true));
    const auto peer_ep = net::Endpoint::loopback(port);
    ClientSession session("host", 0, [&](const Bytes& b) { sock.send_to(peer_ep, b); });
    std::vector<Bytes> got;
    troc::TrocLink link(session, troc::Dir::ToGuestClient, troc::Dir::ToHostServer,
                        [&](Bytes&& p, uint32_t) { got.push_back(std::move(p)); });
    gsw::Assembler up;
    std::optional<gsw::Offer> offer;
    std::vector<std::string> order;  // what reached the fake host, in order
    bool upload_ok = false, caps_seen = false;
    uint32_t ready_ms = 0;
    uint32_t result_xfer = 0, final_at_ms = 0, bye_at_ms = 0;
    bool final_req_ok = false;
    std::optional<gsw::Ack> result_ack;
    size_t handled = 0;
    std::vector<uint8_t> buf(proto::kMaxPacketSize);
    const uint32_t t0 = now_ms();
    bool peer_done = false;
    while (now_ms() - t0 < 25000 && !peer_done) {
        net::Endpoint from;
        while (auto n = sock.recv_from(from, buf, 2)) session.on_datagram({buf.data(), *n}, now_ms());
        session.tick(now_ms());
        // The host's "ready" every 500 ms while connected, as the real host does every second (the first one may be
        // lost to the link's reset at the connection's start).
        if (link.connected() && now_ms() - ready_ms >= 500) {
            const uint8_t ready = troc::kControlHostReady;
            link.push(troc::make(troc::kControlOpcode, -1, std::span<const uint8_t>(&ready, 1)));
            ready_ms = now_ms();
        }
        caps_seen = caps_seen || (link.peer_caps() & troc::kCapsGuestSave) != 0;
        for (; handled < got.size(); ++handled) {
            const Bytes& p = got[handled];
            if (const auto sp = gsw::save_payload(p)) {
                const auto s = *sp;
                if (s[0] == gsw::kStateOffer) {
                    offer = gsw::decode_offer(s);
                    CHECK(offer.has_value());
                    if (offer) CHECK(up.begin(offer->xfer, offer->files) == gsw::AckResult::Ok);
                    order.push_back("offer");
                } else if (s[0] == gsw::kStateChunk) {
                    gsw::AckResult err = gsw::AckResult::Ok;
                    if (up.add(*gsw::decode_chunk(s), err) == gsw::Assembler::Step::Complete) {
                        std::string why;
                        upload_ok = up.verify(why) == gsw::AckResult::Ok && up.file(gsw::Kind::Lobby) == up_lobby &&
                                    up.file(gsw::Kind::Save) == up_save;
                        link.push(gsw::frame(gsw::encode_ack(gsw::kStateAck, gsw::Ack{offer->xfer, 0, 0})));
                        order.push_back("upload complete");
                    }
                } else if (s[0] == gsw::kStateResultAck) {
                    result_ack = gsw::decode_ack(s, gsw::kStateResultAck);
                    order.push_back("result ack");
                }
                continue;
            }
            const auto v = troc::parse_wire(p, troc::Dir::ToHostServer);
            if (!v) continue;
            order.push_back(std::to_string(v->opcode));
            switch (v->opcode) {
                case 2319: link.push(troc::make(2840, -1, Bytes(16, 0x11))); break;
                case 2490: link.push(troc::make(2716, -1, Bytes{})); break;
                case 2910: {
                    const auto req = gsw::parse_save_req(v->payload);
                    final_req_ok = req && req->slot == 100 && req->name == u"Co-op" && v->slot == 0;
                    final_at_ms = now_ms();
                    if (fail_final) {
                        // The server refused it (e.g. CanSave 0x186F000): a 3016 with ok 0, and nothing exported.
                        link.push(troc::make(3016, -1, Bytes{0x01, 0x02, 0x10, 0xA0, 100, 0, 0, 0, 0, 1, 0x08, 0x76, 0xCF, 0x06}));
                        break;
                    }
                    // The server saved user 2: its 3016 (ok), then the export as SAVE_RESULT gen1 + chunks.
                    link.push(troc::make(3016, -1, Bytes{0x01, 0x02, 0x10, 0xA0, 100, 0, 0, 0, 1, 1, 0, 0, 0, 0}));
                    result_xfer = 500;
                    const gsw::Result r{result_xfer, 1, 101, static_cast<uint8_t>(gsw::Reason::Final), 1791770400ull,
                                        gsw::files_of(down_lobby, down_save),
                                        offer ? offer->files[1].sha : sha256::Digest{},
                                        offer ? offer->files[1].sha : sha256::Digest{}};
                    link.push(gsw::frame(gsw::encode_result(r)));
                    gsw::Outgoing out(result_xfer, down_lobby, down_save);
                    Bytes payload;
                    while (out.next(payload)) link.push(gsw::frame(payload));
                    break;
                }
                case troc::kOpLogoutSessionReq:
                    bye_at_ms = now_ms();
                    link.push(Bytes{0xD3, 0x07, 0xFF, 4, 0, 0, 0});
                    link.push(Bytes{0xD2, 0x07, 0xFF, 4, 0, 0, 0});
                    break;
                default: break;
            }
        }
        link.tick(now_ms());
        peer_done = WaitForSingleObject(reinterpret_cast<HANDLE>(peer.native_handle()), 0) == WAIT_OBJECT_0;
    }
    peer.join();
    CHECK(rc == 0);
    CHECK(caps_seen);
    CHECK(offer && offer->slot == 101 && offer->guest_id == 76561198000000001ull && offer->base_gen == 2);
    CHECK(upload_ok);
    // The upload first, then the script's Req; the 2910 before the goodbye, which waited for the result's ack.
    const auto at = [&](const std::string& what) {
        return static_cast<size_t>(std::find(order.begin(), order.end(), what) - order.begin());
    };
    CHECK(at("offer") == 0 && at("upload complete") < at("2319"));
    CHECK(final_req_ok && at("3037") == order.size());  // the final save is a 2910 (slot 100, 'Co-op'), never a 3037
    if (fail_final) {
        // No result: the goodbye went as soon as the failed 3016 arrived, not after 15 s.
        CHECK(at("2490") < at("2910") && at("2910") < at("2001") && at("2001") < order.size());
        CHECK(!result_ack && bye_at_ms && bye_at_ms - final_at_ms < 5000);
        DeleteFileW(script.c_str());
        remove_tree(in_dir);
        remove_tree(out_dir);
        return;
    }
    CHECK(at("2490") < at("2910") && at("2910") < at("result ack") && at("result ack") < at("2001") &&
          at("2001") < order.size());
    CHECK(result_ack && result_ack->xfer == result_xfer && result_ack->result == 0 && result_ack->gen == 1);
    CHECK(read_bytes(out_dir + L"\\gen1\\lobby.save") == down_lobby);
    CHECK(read_bytes(out_dir + L"\\gen1\\save.save") == down_save);
    const Bytes manifest = read_bytes(out_dir + L"\\gen1\\manifest.txt");
    const auto m = gsw::parse_incoming_manifest(std::string_view(reinterpret_cast<const char*>(manifest.data()), manifest.size()));
    CHECK(m && m->gen == 1 && m->slot == 101 && m->reason == static_cast<uint8_t>(gsw::Reason::Final) &&
          m->files == gsw::files_of(down_lobby, down_save) && offer && m->root == offer->files[1].sha &&
          m->base == offer->files[1].sha);
    DeleteFileW(script.c_str());
    remove_tree(in_dir);
    remove_tree(out_dir);
}

}  // namespace

int main() {
    if (!net::startup()) return 1;
    test_guest_save_round_trip(false);
    test_guest_save_round_trip(true);
    const std::wstring script = temp_file(L"cdmp_optd_replay_test.troc");
    const std::wstring record = temp_file(L"cdmp_optd_replay_test.bin");
    DeleteFileW(record.c_str());
    write_text(script, R"(var ACCOUNT u64 1
var SAVE_SLOT u32 1
learn USER_KEY u32 2646 0
learn CHARACTER_NO u64 2869 5
learn_child CHILD_KEY 2005 USER_KEY
send crypto 2319 -1 0404000000
expect 2840 5000
send login 2490 -1 01000000000000000000000000000000000000
put u64 0 ACCOUNT
expect 2716 5000
send character 3093 -1 04ffffffffffffff7f01000000d4ab54414242
put u64 1 CHARACTER_NO
put u32 9 SAVE_SLOT
expect 2174 5000
expect_learned CHILD_KEY 5000
send child 2223 0 0000000000000000
put u32 0 CHILD_KEY
send done 2445 0 01
expect 2762 5000
)");

    ReplayOptions opt;
    opt.port = kPort;
    opt.quiet = true;
    opt.seconds = 20;
    opt.script = script;
    opt.record = record;
    opt.start_after_ms = 100;
    opt.linger_ms = 300;
    opt.vars = {{"ACCOUNT", 7}, {"SAVE_SLOT", 2}};
    int rc = -1;
    std::thread peer([&] { rc = run_replay(opt); });
    Sleep(300);  // let it bind

    // The fake host: its mod's network side (Initiator) and a fake server.
    net::UdpSocket sock;
    CHECK(sock.open(0, true));
    const auto peer_ep = net::Endpoint::loopback(kPort);
    ClientSession session("host", 0, [&](const Bytes& b) { sock.send_to(peer_ep, b); });
    std::vector<Bytes> got;
    troc::TrocLink link(session, troc::Dir::ToGuestClient, troc::Dir::ToHostServer,
                        [&](Bytes&& p, uint32_t) { got.push_back(std::move(p)); });
    auto answer = [&](uint16_t op, Bytes payload) { link.push(troc::make(op, -1, payload)); };
    size_t handled = 0;
    std::vector<uint8_t> buf(proto::kMaxPacketSize);
    const uint32_t t0 = now_ms();
    bool peer_done = false;
    while (now_ms() - t0 < 15000 && !peer_done) {
        net::Endpoint from;
        while (auto n = sock.recv_from(from, buf, 2)) session.on_datagram({buf.data(), *n}, now_ms());
        session.tick(now_ms());
        link.tick(now_ms());
        for (; handled < got.size(); ++handled) {
            auto v = troc::parse(got[handled]);
            if (!v) continue;
            switch (v->opcode) {
                case 2319: answer(2840, Bytes(256, 0x11)); break;
                case 2490: {
                    answer(2188, Bytes(20, 0));                              // ServerTimeInfoAck
                    answer(2646, Bytes{0x01, 0x17, 0x10, 0x90, 0, 0, 0, 0});  // MainUserDataStartAck
                    Bytes lobby(20, 0);
                    for (int i = 0; i < 8; ++i) lobby[5 + i] = static_cast<uint8_t>(0xA0 + i);
                    answer(2869, lobby);  // CharacterLobbyDataAck
                    answer(2716, {});     // MainUserDataEndAck
                    break;
                }
                case 3093: {
                    answer(2005, Bytes(30000, 0x33));  // record lengths that run past the end: ignored
                    // D-53r: user 2's record (9 bytes) and its child (type 6, key 0xB0101703).
                    Bytes child = {9, 0, 0, 0x01, 0x17, 0x10, 0x90, 0, 0, 0, 0, 16, 0, 6, 0x03, 0x17, 0x10, 0xB0,
                                   0x01, 0x17, 0x10, 0x90, 1};
                    child.resize(child.size() + 6, 0);
                    answer(2005, child);
                    answer(2174, Bytes(16, 3));
                    break;
                }
                case 2445: answer(2762, {}); break;
                default: break;
            }
        }
        DWORD code = WaitForSingleObject(reinterpret_cast<HANDLE>(peer.native_handle()), 0);
        peer_done = code == WAIT_OBJECT_0;
    }
    peer.join();
    CHECK(rc == 0);

    // What the fake server received, rewritten by the script.
    CHECK(got.size() == 5);
    if (got.size() == 5) {
        auto login = troc::parse(got[1]);
        CHECK(login && login->opcode == 2490 && login->payload[0] == 7);  // --var ACCOUNT=7
        auto ch = troc::parse(got[2]);
        CHECK(ch && ch->opcode == 3093 && ch->payload[1] == 0xA0 && ch->payload[8] == 0xA7);  // learned
        CHECK(ch && ch->payload[9] == 2);  // --var SAVE_SLOT=2
        auto child = troc::parse(got[3]);  // learn_child: the key from the second 2005
        CHECK(child && child->opcode == 2223 && child->payload[0] == 0x03 && child->payload[3] == 0xB0);
        CHECK(troc::parse(got[4])->slot == 0);
    }

    // The recording: 5 Req and 9 Acks, whole.
    capture::Reader r;
    CHECK(r.open(record));
    std::map<capture::Dir, int> dirs;
    bool big = false;
    while (auto rec = r.next()) {
        ++dirs[rec->dir];
        if (rec->opcode() == 2005 && rec->data.size() == 30005) big = rec->whole();
        CHECK(rec->session == 2);
    }
    CHECK(dirs[capture::Dir::ClientToServer] == 5 && dirs[capture::Dir::ServerToClient] == 9 && big);

    DeleteFileW(script.c_str());
    DeleteFileW(record.c_str());
    net::cleanup();
    if (g_failures == 0) std::printf("All option D replay tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
