// P8 session 25, the guest's own save, PHASE B (src/mod/session2/guest_save_wire.h, src/common/sha256.h;
// docs/research/noche/progreso-invitado.md, "Implementado (fase B)").
//
// SCOPE: offline logic only. SHA-256 (NIST vectors, streaming, against BCrypt), the tunnel states 6-11 (encode,
// decode, exact sizes, malformed forms, the control-frame filter), chunking and reassembly (order, bounds, stale
// transfers, the hash and header checks, the real template files when this PC has them), the host's result
// bookkeeping (newest only, supersede, retries, parking, reconnection, resume), the mailbox choice, the guest's holds
// (start, 3093, re-offer, final), the host's final save on a lost link, the Req policy, the status mode, the slot and
// account picks, and the manifests. They do NOT test the hooks, the threads, the files on disk nor the game.

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "common/sha256.h"
#include "common/troc_frame.h"
#include "mod/session2/guest_save_wire.h"

#pragma comment(lib, "bcrypt.lib")

using namespace cdmp;
using namespace cdmp::session2;
using gsw::AckResult;
using gsw::GuestHold;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using Bytes = std::vector<uint8_t>;

Bytes bytes_of(std::string_view s) { return Bytes(s.begin(), s.end()); }

// A save file the game's header check takes: "SAVE", v2, 0x80, flags 2, raw = data + 10, then `data` bytes.
Bytes make_save(uint32_t data, uint8_t seed) {
    Bytes f(0x80 + data);
    const uint32_t magic = gsv::kMagic, raw = data + 10;
    const uint16_t version = 2, hs = 0x80;
    const uint32_t flags = 2;
    std::memcpy(f.data(), &magic, 4);
    std::memcpy(f.data() + 4, &version, 2);
    std::memcpy(f.data() + 6, &hs, 2);
    std::memcpy(f.data() + 0xC, &flags, 4);
    std::memcpy(f.data() + 0x12, &raw, 4);
    std::memcpy(f.data() + 0x16, &data, 4);
    std::mt19937 rng(seed);
    for (size_t i = 0x1A; i < f.size(); ++i) f[i] = static_cast<uint8_t>(rng());
    return f;
}

sha256::Digest bcrypt_sha256(const Bytes& data) {
    sha256::Digest d{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return d;
    BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), d.data(),
               static_cast<ULONG>(d.size()));
    BCryptCloseAlgorithmProvider(alg, 0);
    return d;
}

// ---------------------------------------------------------------- SHA-256

void test_sha256() {
    CHECK(sha256::hex(sha256::of({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256::hex(sha256::of(bytes_of("abc"))) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256::hex(sha256::of(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const Bytes million(1000000, 'a');
    CHECK(sha256::hex(sha256::of(million)) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Streaming in odd pieces is the one-shot digest; BCrypt agrees on many sizes (the host hashes with both).
    std::mt19937 rng(7);
    for (const size_t n : {0u, 1u, 55u, 56u, 63u, 64u, 65u, 119u, 120u, 128u, 1000u, 28672u, 100001u}) {
        Bytes data(n);
        for (auto& b : data) b = static_cast<uint8_t>(rng());
        sha256::Hasher h;
        size_t at = 0, step = 1;
        while (at < n) {
            const size_t take = std::min(step, n - at);
            h.update(std::span<const uint8_t>(data).subspan(at, take));
            at += take;
            step = step * 3 + 1;
        }
        const sha256::Digest streamed = h.finish();
        CHECK(streamed == sha256::of(data));
        CHECK(streamed == bcrypt_sha256(data));
    }
    const auto d = sha256::of(bytes_of("abc"));
    CHECK(sha256::parse_hex(sha256::hex(d)) == d);
    std::string upper = sha256::hex(d);
    for (char& c : upper) c = static_cast<char>(c >= 'a' && c <= 'f' ? c - 32 : c);
    CHECK(sha256::parse_hex(upper) == d);
    CHECK(!sha256::parse_hex(upper.substr(1)));
    CHECK(!sha256::parse_hex(std::string(63, '0') + "g"));
}

// ---------------------------------------------------------------- the wire

gsw::Files sample_files() {
    return gsw::files_of(make_save(390, 1), make_save(5000, 2));
}

void test_wire() {
    const gsw::Files files = sample_files();
    gsw::Offer o{0x01020304, 76561198000000001ull, 101, 7, files};
    const Bytes po = gsw::encode_offer(o);
    CHECK(po.size() == gsw::kOfferSize && po[0] == gsw::kStateOffer && po[1] == gsw::kWireVersion);
    const auto oo = gsw::decode_offer(po);
    CHECK(oo && oo->xfer == o.xfer && oo->guest_id == o.guest_id && oo->slot == 101 && oo->base_gen == 7 &&
          oo->files == files);
    Bytes bad = po;
    bad[1] = 2;  // version
    CHECK(!gsw::decode_offer(bad));
    bad = po;
    bad.pop_back();
    CHECK(!gsw::decode_offer(bad));
    bad = po;
    bad[22] = 3;  // nfiles
    CHECK(!gsw::decode_offer(bad));
    bad = po;
    bad[23] = 1;  // the first file must be the lobby
    CHECK(!gsw::decode_offer(bad));
    gsw::Offer zero = o;
    zero.files[1].size = 0;
    CHECK(!gsw::decode_offer(gsw::encode_offer(zero)));
    zero.files[1].size = static_cast<uint32_t>(gsv::kMaxFileSize) + 1;
    CHECK(!gsw::decode_offer(gsw::encode_offer(zero)));

    const Bytes data = {1, 2, 3, 4, 5};
    const Bytes pc = gsw::encode_chunk(9, gsw::Kind::Save, 28672, data);
    CHECK(pc.size() == gsw::kChunkHeader + data.size());
    const auto c = gsw::decode_chunk(pc);
    CHECK(c && c->xfer == 9 && c->kind == gsw::Kind::Save && c->offset == 28672 && Bytes(c->data.begin(), c->data.end()) == data);
    bad = pc;
    bad[5] = 2;  // kind
    CHECK(!gsw::decode_chunk(bad));
    bad = pc;
    bad[10] = 6;  // length says 6, 5 follow
    CHECK(!gsw::decode_chunk(bad));
    CHECK(!gsw::decode_chunk(gsw::encode_chunk(9, gsw::Kind::Lobby, 0, {})));  // empty
    const Bytes big(gsw::kMaxChunk + 1, 0);
    CHECK(!gsw::decode_chunk(gsw::encode_chunk(9, gsw::Kind::Lobby, 0, big)));
    const Bytes max(gsw::kMaxChunk, 0);
    CHECK(gsw::decode_chunk(gsw::encode_chunk(9, gsw::Kind::Lobby, 0, max)).has_value());

    for (const uint8_t state : {gsw::kStateAck, gsw::kStateResultAck}) {
        const Bytes pa = gsw::encode_ack(state, gsw::Ack{77, static_cast<uint8_t>(AckResult::Hash), 12});
        CHECK(pa.size() == gsw::kAckSize);
        const auto a = gsw::decode_ack(pa, state);
        CHECK(a && a->xfer == 77 && a->result == 1 && a->gen == 12);
        CHECK(!gsw::decode_ack(pa, state == gsw::kStateAck ? gsw::kStateResultAck : gsw::kStateAck));
    }
    CHECK(!gsw::decode_ack(gsw::encode_ack(gsw::kStateOffer, gsw::Ack{}), gsw::kStateOffer));

    gsw::Result r{5, 3, 1, static_cast<uint8_t>(gsw::Reason::Final), 1760000000ull, files, files[1].sha, files[0].sha};
    const Bytes pr = gsw::encode_result(r);
    CHECK(pr.size() == gsw::kResultSize && pr.size() == 162 && pr[1] == gsw::kResultVersion);
    const auto rr = gsw::decode_result(pr);
    CHECK(rr && rr->xfer == 5 && rr->gen == 3 && rr->slot == 1 && rr->reason == 3 && rr->time_unix == 1760000000ull &&
          rr->files == files && rr->root == files[1].sha && rr->base == files[0].sha);
    CHECK(!gsw::other_version_result_xfer(pr));
    // Session 25: a version-1 result (no line) is refused, so a generation without a line is never stored as one.
    Bytes v1(pr.begin(), pr.begin() + 98);
    v1[1] = 1;
    CHECK(!gsw::decode_result(v1));
    Bytes v1long = pr;
    v1long[1] = 1;
    CHECK(!gsw::decode_result(v1long));
    // ...and answered (review of 12f8dfb, item 5): its xfer is at the same offset in every version.
    CHECK(gsw::other_version_result_xfer(v1) == 5u && gsw::other_version_result_xfer(v1long) == 5u);
    CHECK(!gsw::other_version_result_xfer(Bytes{gsw::kStateResult, 1}));
    gsw::Status s{static_cast<uint8_t>(gsw::HostMode::Active), 204, 0xDEAD, 76561198000000002ull, 4};
    const Bytes ps = gsw::encode_status(s);
    CHECK(ps.size() == gsw::kStatusSize);
    const auto ss = gsw::decode_status(ps);
    CHECK(ss && ss->mode == 2 && ss->guest_slot == 204 && ss->last_error == 0xDEAD &&
          ss->host_id == 76561198000000002ull && ss->last_gen == 4);
    for (uint8_t m = 0; m <= 5; ++m) CHECK(*gsw::host_mode_name(m) != '?');
    for (uint8_t a = 0; a <= 11; ++a) CHECK(*gsw::ack_result_name(a) != '?');

    // The frame and the filter: exactly opcode 1, slot -1, the length, states 6-11; the other control states
    // (1 ready, 2 dead, 3 re-announce, 4 host character, 5 mount_sync) and game packets go on as before.
    const Bytes f = gsw::frame(po);
    CHECK(f.size() == gsw::kTrocHeader + po.size() && f[0] == 1 && f[1] == 0 && f[2] == 0xFF);
    const auto v = troc::parse(f);
    CHECK(v && v->opcode == troc::kControlOpcode && v->slot == -1 && v->payload.size() == po.size());
    CHECK(gsw::save_payload(f).has_value());
    for (const uint8_t st : std::initializer_list<uint8_t>{1, 2, 3, 4, 5, 12, 0}) {
        const uint8_t p[1] = {st};
        CHECK(!gsw::save_payload(gsw::frame(p)));
    }
    Bytes other = f;
    other[2] = 0;  // slot 0
    CHECK(!gsw::save_payload(other));
    other = f;
    other.push_back(0);  // length no longer the rest
    CHECK(!gsw::save_payload(other));
    other = f;
    other[0] = 0xDD;  // opcode 0x0BDD... not 1
    CHECK(!gsw::save_payload(other));
    static_assert(gsw::kCapsGuestSave == troc::kCapsGuestSave && gsw::kControlOpcode == troc::kControlOpcode);
}

// ---------------------------------------------------------------- chunks and reassembly

void test_transfer() {
    const Bytes lobby = make_save(390, 11), save = make_save(100000, 12);
    const gsw::Files files = gsw::files_of(lobby, save);
    for (const size_t max : {gsw::kMaxChunk, size_t{1000}, size_t{1}}) {
        gsw::Outgoing out(42, lobby, save, max);
        gsw::Assembler in;
        CHECK(in.begin(42, files) == AckResult::Ok);
        Bytes payload;
        uint32_t n = 0;
        gsw::Assembler::Step last = gsw::Assembler::Step::Need;
        while (out.next(payload)) {
            ++n;
            const auto c = gsw::decode_chunk(payload);
            CHECK(c.has_value());
            AckResult err = AckResult::Ok;
            last = in.add(*c, err);
            CHECK(last != gsw::Assembler::Step::Error);
        }
        CHECK(out.done() && out.sent() == lobby.size() + save.size());
        CHECK(n == gsw::Outgoing::chunk_count(lobby.size(), save.size(), max) && n == out.chunks());
        CHECK(last == gsw::Assembler::Step::Complete);
        std::string why;
        CHECK(in.verify(why) == AckResult::Ok);
        CHECK(in.file(gsw::Kind::Lobby) == lobby && in.file(gsw::Kind::Save) == save);
    }
    CHECK(gsw::Outgoing::chunk_count(390 + 0x80, 736731) == 1 + 26);

    // The two files may interleave (each in its own order); anything out of order, past the end, of another
    // transfer, or after the end is refused.
    gsw::Assembler in;
    CHECK(in.begin(7, files) == AckResult::Ok);
    AckResult err = AckResult::Ok;
    const auto chunk = [](uint32_t xfer, gsw::Kind k, uint32_t off, std::span<const uint8_t> d) {
        static Bytes keep;
        keep = gsw::encode_chunk(xfer, k, off, d);
        return *gsw::decode_chunk(keep);
    };
    CHECK(in.add(chunk(8, gsw::Kind::Lobby, 0, std::span(lobby).subspan(0, 10)), err) == gsw::Assembler::Step::Ignored);
    CHECK(in.add(chunk(7, gsw::Kind::Save, 0, std::span(save).subspan(0, 100)), err) == gsw::Assembler::Step::Need);
    CHECK(in.add(chunk(7, gsw::Kind::Lobby, 0, std::span(lobby).subspan(0, 100)), err) == gsw::Assembler::Step::Need);
    CHECK(in.add(chunk(7, gsw::Kind::Lobby, 101, std::span(lobby).subspan(101, 10)), err) == gsw::Assembler::Step::Error);
    CHECK(err == AckResult::Protocol && !in.active());
    CHECK(in.begin(7, files) == AckResult::Ok);
    CHECK(in.add(chunk(7, gsw::Kind::Lobby, 0, lobby), err) == gsw::Assembler::Step::Need);
    const Bytes extra(10, 0);
    CHECK(in.add(chunk(7, gsw::Kind::Lobby, static_cast<uint32_t>(lobby.size()), extra), err) == gsw::Assembler::Step::Error);
    // A tampered byte: the hash; a bad header with its true hash: the header.
    const auto feed = [](gsw::Assembler& a, uint32_t xfer, const Bytes& l, const Bytes& s) {
        gsw::Outgoing out(xfer, l, s);
        Bytes payload;
        AckResult e = AckResult::Ok;
        gsw::Assembler::Step step = gsw::Assembler::Step::Need;
        while (out.next(payload)) step = a.add(*gsw::decode_chunk(payload), e);
        return step;
    };
    Bytes tampered = save;
    tampered[5000] ^= 1;
    CHECK(in.begin(9, files) == AckResult::Ok);
    CHECK(feed(in, 9, lobby, tampered) == gsw::Assembler::Step::Complete);
    std::string why;
    CHECK(in.verify(why) == AckResult::Hash && !why.empty());
    Bytes no_magic = save;
    no_magic[0] = 'X';
    const gsw::Files bad_files = gsw::files_of(lobby, no_magic);
    CHECK(in.begin(10, bad_files) == AckResult::Ok);
    CHECK(feed(in, 10, lobby, no_magic) == gsw::Assembler::Step::Complete);
    CHECK(in.verify(why) == AckResult::Header);
    gsw::Files zero = files;
    zero[0].size = 0;
    CHECK(in.begin(11, zero) == AckResult::Size && !in.active());
    CHECK(in.add(chunk(11, gsw::Kind::Lobby, 0, lobby), err) == gsw::Assembler::Step::Ignored);
    // check_pair: a file one byte longer than its header says.
    Bytes longer = save;
    longer.push_back(0);
    CHECK(gsw::check_pair(lobby, longer, why) == AckResult::Header);
    CHECK(gsw::check_pair(lobby, save, why) == AckResult::Ok);
}

// The template of another account (A4/B3), when this PC has it: whole files through the protocol.
void test_template() {
    const std::string base = "C:\\Emulador CD\\backups\\plantillas\\chapter2-normal-nexus2863\\slot108";
    std::ifstream fl(base + "\\lobby.save", std::ios::binary), fs(base + "\\save.save", std::ios::binary);
    if (!fl || !fs) {
        std::printf("template files: not on this PC (%s), skipped\n", base.c_str());
        return;
    }
    const Bytes lobby((std::istreambuf_iterator<char>(fl)), std::istreambuf_iterator<char>());
    const Bytes save((std::istreambuf_iterator<char>(fs)), std::istreambuf_iterator<char>());
    std::string why;
    CHECK(gsw::check_pair(lobby, save, why) == AckResult::Ok);
    const gsw::Files files = gsw::files_of(lobby, save);
    CHECK(files[0].sha == bcrypt_sha256(lobby) && files[1].sha == bcrypt_sha256(save));
    gsw::Outgoing out(1, lobby, save);
    gsw::Assembler in;
    CHECK(in.begin(1, files) == AckResult::Ok);
    Bytes payload;
    AckResult err = AckResult::Ok;
    while (out.next(payload)) in.add(*gsw::decode_chunk(payload), err);
    CHECK(in.verify(why) == AckResult::Ok);
    std::printf("template files: %zu + %zu bytes in %u chunks, round trip ok\n", lobby.size(), save.size(), out.chunks());
}

// ---------------------------------------------------------------- the host's results

gsw::Generation gen(uint32_t n) {
    gsw::Generation g;
    g.gen = n;
    g.slot = 1;
    g.reason = static_cast<uint8_t>(gsw::Reason::Autosave);
    g.files = sample_files();
    return g;
}

void test_result_sender() {
    using E = gsw::ResultSender::Export;
    using V = gsw::ResultSender::AckVerdict;
    const auto ok = [](uint32_t xfer, uint32_t g) { return gsw::Ack{xfer, 0, g}; };
    gsw::ResultSender s;
    s.set_delivered(2);
    CHECK(s.on_export(gen(2)) == E::Old);  // already delivered
    CHECK(s.on_export(gen(3)) == E::Queued);
    CHECK(s.on_export(gen(3)) == E::Old);
    auto g = s.start(100, 0);
    CHECK(g && g->gen == 3 && s.in_flight() && s.sending());
    CHECK(!s.start(101, 0));  // one at a time
    CHECK(s.on_ack(ok(99, 3)) == V::Stale);
    s.all_sent(10);
    CHECK(!s.sending() && s.in_flight());
    CHECK(s.on_ack(ok(100, 3)) == V::Delivered && s.delivered() == 3 && !s.in_flight() && !s.pending());
    CHECK(!s.start(102, 20));

    // A newer export replaces a pending one and aborts one in flight (sent from the start, new xfer).
    CHECK(s.on_export(gen(4)) == E::Queued);
    CHECK(s.start(103, 30)->gen == 4);
    CHECK(s.on_export(gen(5)) == E::Superseded && !s.in_flight());
    CHECK(s.on_ack(ok(103, 4)) == V::Stale);  // the aborted one's late answer
    CHECK(s.start(104, 40)->gen == 5);

    // Failures: retried, then parked (kMaxResultAttempts) until the next connection.
    const gsw::Ack io{104, static_cast<uint8_t>(AckResult::Io), 5};
    CHECK(s.on_ack(io) == V::Retry);
    CHECK(s.start(105, 50)->gen == 5);
    CHECK(s.on_ack(gsw::Ack{105, static_cast<uint8_t>(AckResult::Io), 5}) == V::Retry);
    CHECK(s.start(106, 60)->gen == 5);
    CHECK(s.on_ack(gsw::Ack{106, static_cast<uint8_t>(AckResult::Hash), 5}) == V::GaveUp && s.parked());
    CHECK(!s.start(107, 70));
    s.on_link_lost();
    CHECK(!s.parked() && s.start(108, 80)->gen == 5);
    // An ok for another generation number is not a delivery.
    CHECK(s.on_ack(ok(108, 4)) == V::Retry);

    // The connection ends during the transfer: pending again, sent again from the start on the next one.
    CHECK(s.start(109, 90)->gen == 5);
    s.on_link_lost();
    CHECK(!s.in_flight() && s.pending() && s.start(110, 100)->gen == 5);
    // No answer kResultAckTimeoutMs after the last chunk: a failed attempt.
    s.all_sent(1000);
    CHECK(!s.check_timeout(1000 + gsw::kResultAckTimeoutMs - 1));
    CHECK(s.check_timeout(1000 + gsw::kResultAckTimeoutMs) == V::Retry);
    CHECK(s.start(111, 2000)->gen == 5);
    CHECK(s.on_ack(ok(111, 5)) == V::Delivered && s.delivered() == 5);

    // resume: a mailbox generation the guest never acknowledged (not if delivered or older than the pending one).
    gsw::ResultSender r;
    r.set_delivered(4);
    CHECK(!r.resume(gen(4)));
    CHECK(r.resume(gen(6)));
    CHECK(!r.resume(gen(5)));
    CHECK(r.pending()->gen == 6);

    // The undelivered generation: the newest above what the guest has.
    const std::vector<uint32_t> gens = {3, 7, 5};
    CHECK(gsw::newest_above(gens, 0) == 7u);
    CHECK(gsw::newest_above(gens, 6) == 7u);
    CHECK(!gsw::newest_above(gens, 7));
    CHECK(!gsw::newest_above(std::vector<uint32_t>{}, 0));
}

// ---------------------------------------------------------------- lines (session 25)

sha256::Digest digest_of(uint8_t seed) {
    const Bytes b(64, seed);
    return sha256::of(b);
}

void test_lines() {
    using gsw::LineGen;
    const sha256::Digest test_save = digest_of(1), advanced0 = digest_of(2), advanced1 = digest_of(3);
    const sha256::Digest g1 = digest_of(11), g2 = digest_of(12), g3 = digest_of(13), g4 = digest_of(14);

    // Session 25 as it happened: gen1 and gen2 continue the test save (slot1); then the guest offers its advanced slot0.
    // Before the fix gen2 replaced that upload (the guest played the test save); now no line continues it.
    std::vector<LineGen> mailbox = {{1, g1, test_save}, {2, g2, test_save}};
    auto c = gsw::find_continuation(mailbox, advanced0);
    CHECK(!c.found && !c.newer && c.root == advanced0 && c.at == 0);
    // The same test save offered again (never applied): its gen2 goes instead (progress never goes back).
    c = gsw::find_continuation(mailbox, test_save);
    CHECK(c.found && c.root == test_save && c.at == 0 && c.newer == 2u);
    // gen1 applied by the script and offered: gen2 is newer.
    c = gsw::find_continuation(mailbox, g1);
    CHECK(c.found && c.root == test_save && c.at == 1 && c.newer == 2u);
    // gen2 applied and offered: nothing newer, the upload itself (the same line).
    c = gsw::find_continuation(mailbox, g2);
    CHECK(c.found && c.root == test_save && c.at == 2 && !c.newer);

    // Two lines in one mailbox: each upload only meets its own.
    mailbox.push_back({3, g3, advanced0});
    mailbox.push_back({4, g4, advanced1});
    c = gsw::find_continuation(mailbox, advanced0);
    CHECK(c.found && c.root == advanced0 && c.newer == 3u);
    c = gsw::find_continuation(mailbox, advanced1);
    CHECK(c.found && c.root == advanced1 && c.newer == 4u);
    c = gsw::find_continuation(mailbox, test_save);
    CHECK(c.found && c.root == test_save && c.newer == 2u);
    c = gsw::find_continuation(mailbox, g3);
    CHECK(c.found && c.root == advanced0 && c.at == 3 && !c.newer);

    // Generations of an older build (no line) are never matched, not even by their own save.
    const std::vector<LineGen> old = {{5, g1, sha256::Digest{}}, {6, g2, sha256::Digest{}}};
    c = gsw::find_continuation(old, g1);
    CHECK(!c.found && !c.newer && c.root == g1);
    c = gsw::find_continuation(std::vector<LineGen>{}, test_save);
    CHECK(!c.found && !c.newer && c.root == test_save);
    CHECK(gsw::digest_zero(sha256::Digest{}) && !gsw::digest_zero(test_save));
}

// ---------------------------------------------------------------- the guest's holds

void test_guest_hold() {
    using U = GuestHold::Upload;
    using R = GuestHold::Release;
    const uint16_t k3093 = gsw::kOpLoginCharacterReq, k2001 = gsw::kOpLogoutSessionReq, k2490 = 2490, k2319 = 2319;
    const auto ack = [](uint32_t xfer, AckResult r, uint32_t g = 0) { return gsw::Ack{xfer, static_cast<uint8_t>(r), g}; };
    bool retry = false;

    // Off (probe or 0): nothing is ever held.
    {
        GuestHold h(false);
        h.prepare_started();
        CHECK(!h.on_send(k2319, {}, 0, false, 0).hold);
        CHECK(!h.on_send(k3093, 1u, 0, false, 0).hold);
        CHECK(!h.on_send(k2001, {}, 0, true, 0).hold);
    }
    // The start hold: the client's first packets wait while the offer is prepared; they go once it is queued.
    {
        GuestHold h(true);
        h.prepare_started();
        CHECK(h.upload() == U::Pending);
        CHECK(h.on_send(k2319, {}, 100, false, 0).hold);
        CHECK(h.on_send(k2490, {}, 110, false, 0).hold);
        CHECK(h.poll(200, 0) == R::None);
        h.offer_sent(5, 1);
        CHECK(h.poll(210, 0) == R::Queued && h.hold() == GuestHold::Hold::Off);
        // Its 3093 then waits for the ack.
        CHECK(h.on_send(k3093, 1u, 300, false, 0).hold);
        CHECK(h.on_send(2601, {}, 310, false, 0).hold);  // everything after it too
        CHECK(h.poll(400, 0) == R::None);
        CHECK(!h.on_ack(ack(4, AckResult::Ok), retry));  // another offer's
        CHECK(h.on_ack(ack(5, AckResult::Ok), retry) && !retry && h.upload() == U::Acked);
        CHECK(h.poll(410, 0) == R::Acked);
        CHECK(!h.on_send(k3093, 1u, 500, false, 0).hold);  // acknowledged: a later 3093 goes at once
    }
    // Nothing could be prepared: the held packets go, no own save.
    {
        GuestHold h(true);
        h.prepare_started();
        CHECK(h.on_send(k2319, {}, 0, false, 0).hold);
        h.prepare_failed();
        CHECK(h.upload() == U::None && h.poll(1, 0) == R::NoOffer);
        CHECK(!h.on_send(k3093, 1u, 2, false, 0).hold);
    }
    // A refused upload: offered once more, then given up (the 3093 goes; no own save).
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_send(k3093, 1u, 0, false, 0).hold);
        CHECK(h.on_ack(ack(1, AckResult::Hash), retry) && retry && h.upload() == U::Failed);
        CHECK(h.poll(10, 0) == R::None);
        h.offer_sent(2, 1);
        CHECK(h.on_ack(ack(2, AckResult::Hash), retry) && !retry && h.upload() == U::GaveUp);
        CHECK(h.poll(20, 0) == R::GaveUp);
    }
    // Off / slot / identity refusals are final at once.
    for (const AckResult r : {AckResult::Off, AckResult::Slot, AckResult::Identity}) {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_send(k3093, 1u, 0, false, 0).hold);
        CHECK(h.on_ack(ack(1, r), retry) && !retry && h.poll(1, 0) == R::GaveUp);
    }
    // No answer in 30 s.
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_send(k3093, 1u, 1000, false, 0).hold);
        CHECK(h.poll(1000 + gsw::kLoginHoldMs - 1, 0) == R::None);
        CHECK(h.poll(1000 + gsw::kLoginHoldMs, 0) == R::Timeout && h.upload() == U::GaveUp);
    }
    // A 3093 for another slot: that slot is offered first (once); its ack releases the 3093.
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry));
        const auto d = h.on_send(k3093, 102u, 0, false, 0);
        CHECK(d.hold && d.reoffer == 102u && h.upload() == U::Preparing);
        CHECK(h.poll(10, 0) == R::None);
        h.offer_sent(2, 102);
        CHECK(h.slot() == 102 && h.poll(20, 0) == R::None);
        CHECK(h.on_ack(ack(2, AckResult::Ok), retry) && h.poll(30, 0) == R::Acked);
        // Asked once: the same slot again does not ask again.
        CHECK(!h.on_send(k3093, 102u, 40, false, 0).hold);
        // Review item 9: the offered slot is the "asked" one now, so a 3093 back to slot 1 asks for slot 1 again.
        const auto back = h.on_send(k3093, 1u, 50, false, 0);
        CHECK(back.hold && back.reoffer == 1u && h.upload() == U::Preparing);
        h.offer_sent(3, 1);
        CHECK(h.on_ack(ack(3, AckResult::Ok), retry) && h.poll(60, 0) == R::Acked && h.slot() == 1);
    }
    // ... and when that slot cannot be offered, the acknowledged one stays (SlotKept).
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry));
        CHECK(h.on_send(k3093, 0u, 0, false, 0).reoffer == 0u);
        h.reoffer_failed();
        CHECK(h.upload() == U::Acked && h.poll(1, 0) == R::SlotKept);
    }
    // ... the first offer's ack arriving while the other slot is prepared counts if that one cannot be made.
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_send(k3093, 2u, 0, false, 0).reoffer == 2u);
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry) && h.upload() == U::Preparing);
        CHECK(h.poll(1, 0) == R::None);
        h.reoffer_failed();
        CHECK(h.poll(2, 0) == R::SlotKept);
    }
    // 3 and 206 (and anything else not 0-2 / 100-108) never ask for an offer.
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry));
        CHECK(!h.on_send(k3093, 3u, 0, false, 0).reoffer);
        CHECK(!h.on_send(k3093, 206u, 0, false, 0).reoffer);
    }
    // The final hold: only with the own save active (final_ok) and an acknowledged upload, once.
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(!h.on_send(k2001, {}, 0, true, 0).hold);  // not acknowledged
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry));
        CHECK(!h.on_send(k2001, {}, 0, false, 0).hold);  // not active
        const auto d = h.on_send(k2001, {}, 1000, true, 4);
        CHECK(d.hold && d.save_first && h.hold() == GuestHold::Hold::Final);
        CHECK(h.on_send(9999, {}, 1001, true, 4).hold && !h.on_send(9999, {}, 1001, true, 4).save_first);
        CHECK(h.poll(2000, 4) == R::None);
        CHECK(h.poll(2001, 5) == R::FinalDone);
        CHECK(!h.on_send(k2001, {}, 3000, true, 5).hold);  // once
    }
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry));
        CHECK(h.on_send(k2001, {}, 1000, true, 0).hold);
        CHECK(h.poll(1000 + gsw::kFinalHoldMs - 1, 0) == R::None);
        CHECK(h.poll(1000 + gsw::kFinalHoldMs, 0) == R::FinalTimeout);
    }
    // The server's 3016 says the final 2910 failed: the goodbye goes at once (no generation will come); a 3016 that
    // says it saved changes nothing (the generation itself ends the hold).
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_ack(ack(1, AckResult::Ok), retry));
        h.final_answer(false);  // not holding yet: ignored
        CHECK(h.on_send(k2001, {}, 1000, true, 0).hold);
        h.final_answer(true);
        CHECK(h.poll(1001, 0) == R::None);
        h.final_answer(false);
        CHECK(h.poll(1002, 0) == R::FinalFailed && h.hold() == GuestHold::Hold::Off);
    }
    // The link lost: the hold ends (the caller drops what it kept).
    {
        GuestHold h(true);
        h.offer_sent(1, 1);
        CHECK(h.on_send(k3093, 1u, 0, false, 0).hold);
        h.on_link_lost();
        CHECK(h.hold() == GuestHold::Hold::Off && h.poll(1, 0) == R::None);
    }
    for (int r = 0; r <= static_cast<int>(R::FinalFailed); ++r) CHECK(*gsw::release_name(static_cast<R>(r)) != '?');
    for (int u = 0; u <= static_cast<int>(U::GaveUp); ++u) CHECK(*gsw::upload_name(static_cast<U>(u)) != '?');
}

// ---------------------------------------------------------------- the host's final save on a lost link

void test_final_save() {
    using S = gsw::FinalSave::Step;
    using O = gsw::FinalSave::Outcome;
    gsw::FinalSave f;
    CHECK(f.step(0, 0) == S::Go && f.outcome() == O::None);  // not armed: the 2001 as before
    f.arm();
    CHECK(f.step(0, 3) == S::Inject);
    f.injected(true, 100, 3);
    CHECK(f.step(200, 3) == S::Wait);
    CHECK(f.step(300, 4) == S::Go && f.outcome() == O::Exported);
    f.arm();  // once per logout
    CHECK(f.step(400, 4) == S::Go);
    f.reset();
    f.arm();
    f.injected(true, 1000, 0);
    CHECK(f.step(1000 + gsw::kFinalHoldMs - 1, 0) == S::Wait);
    CHECK(f.step(1000 + gsw::kFinalHoldMs, 0) == S::Go && f.outcome() == O::TimedOut);
    f.reset();
    f.arm();
    f.injected(false, 0, 0);
    CHECK(f.step(1, 0) == S::Go && f.outcome() == O::NotQueued);
    for (int o = 0; o <= static_cast<int>(O::NotQueued); ++o) {
        CHECK(*gsw::final_outcome_name(static_cast<O>(o)) != '?');
    }
}

// ---------------------------------------------------------------- policy and status

void test_policy() {
    using P = gsw::ReqPolicy;
    gsv::SlotState live;
    live.live = true;
    gsv::SlotState none;
    gsv::SlotState poisoned = live;
    poisoned.poisoned = true;
    gsv::SlotState disabled = live;
    disabled.disabled = true;
    // A 2910 is judged with its payload: a manual slot 100-108, well formed.
    const Bytes final_req = gsw::make_final_save_req();
    const std::span<const uint8_t> final_payload = std::span<const uint8_t>(final_req).subspan(gsw::kTrocHeader);
    const Bytes auto_payload = {0, 0, 0, 0};
    for (const uint16_t op : {gsw::kOpSaveAutoReq, gsw::kOpSaveToFileCurrentPlayerReq}) {
        const std::span<const uint8_t> pl =
            op == gsw::kOpSaveAutoReq ? std::span<const uint8_t>(auto_payload) : final_payload;
        CHECK(gsw::req_policy(gsv::Mode::On, live, op, pl) == P::Allow);
        CHECK(gsw::req_policy(gsv::Mode::On, none, op, pl) == P::Default);  // blocked by "Save*" as before
        CHECK(gsw::req_policy(gsv::Mode::On, poisoned, op, pl) == P::Default);
        CHECK(gsw::req_policy(gsv::Mode::On, disabled, op, pl) == P::Default);
        CHECK(gsw::req_policy(gsv::Mode::Probe, live, op, pl) == P::Default);
        CHECK(gsw::req_policy(gsv::Mode::Off, live, op, pl) == P::Default);
    }
    const auto payload_of = [](const Bytes& f) { return Bytes(f.begin() + gsw::kTrocHeader, f.end()); };
    for (const uint32_t slot : {100u, 104u, 108u}) {
        CHECK(gsw::req_policy(gsv::Mode::On, live, gsw::kOpSaveToFileCurrentPlayerReq,
                              payload_of(gsw::make_save_req(slot, u"x"))) == P::Allow);
    }
    for (const uint32_t slot : {3u, 0u, 1u, 99u, 109u, 204u, 206u}) {  // CrossSave above all: never
        CHECK(gsw::req_policy(gsv::Mode::On, live, gsw::kOpSaveToFileCurrentPlayerReq,
                              payload_of(gsw::make_save_req(slot, u"x"))) == P::Block);
    }
    CHECK(gsw::req_policy(gsv::Mode::On, live, gsw::kOpSaveToFileCurrentPlayerReq, {}) == P::Block);  // malformed
    CHECK(gsw::req_policy(gsv::Mode::Probe, live, gsw::kOpSaveToFileCurrentPlayerReq, {}) == P::Default);
    for (const uint16_t op : {gsw::kOpCreateCharacterReq, gsw::kOpNewGameStartReq}) {
        CHECK(gsw::req_policy(gsv::Mode::On, none, op) == P::Block);
        CHECK(gsw::req_policy(gsv::Mode::On, live, op) == P::Block);
        CHECK(gsw::req_policy(gsv::Mode::Probe, live, op) == P::Default);
        CHECK(gsw::req_policy(gsv::Mode::Off, none, op) == P::Default);
    }
    for (const uint16_t op : {uint16_t{2579}, uint16_t{2337}, uint16_t{3093}, uint16_t{2490}, uint16_t{2223}}) {
        CHECK(gsw::req_policy(gsv::Mode::On, live, op) == P::Default);
    }

    using H = gsw::HostMode;
    gsv::SlotState healthy = live;
    healthy.lobby_ok = healthy.save_ok = true;
    CHECK(gsw::host_mode(gsv::Mode::Off, healthy) == H::Off);
    CHECK(gsw::host_mode(gsv::Mode::Probe, healthy) == H::Probe);
    CHECK(gsw::host_mode(gsv::Mode::On, healthy) == H::Active);
    CHECK(gsw::host_mode(gsv::Mode::On, live) == H::Ready);
    CHECK(gsw::host_mode(gsv::Mode::On, none) == H::Ready);
    CHECK(gsw::host_mode(gsv::Mode::On, poisoned) == H::Poisoned);
    CHECK(gsw::host_mode(gsv::Mode::On, disabled) == H::Off);
}

// ---------------------------------------------------------------- slots and accounts

void test_slots() {
    for (uint32_t n = 0; n < 300; ++n) {
        CHECK(gsw::offer_slot_ok(n) == (n <= 2 || (n >= 100 && n <= 108)));
    }
    CHECK(!gsw::offer_slot_ok(3) && !gsw::offer_slot_ok(206) && !gsw::offer_slot_ok(204));

    using D = gsw::DirKind;
    CHECK(gsw::parse_slot_dir(L"slot1")->slot == 1 && gsw::parse_slot_dir(L"slot1")->kind == D::Plain);
    CHECK(gsw::parse_slot_dir(L"slot108")->slot == 108);
    CHECK(gsw::parse_slot_dir(L"slot0")->slot == 0);
    CHECK(gsw::parse_slot_dir(L"slot2_new")->kind == D::New);
    CHECK(gsw::parse_slot_dir(L"slot2_backup")->kind == D::Backup);
    for (const wchar_t* bad : {L"slot", L"slot01", L"slotX", L"slot1_backup_20261011_delete", L"slot1.incoming",
                               L"Slot1", L"save", L"slot12345"}) {
        CHECK(!gsw::parse_slot_dir(bad));
    }

    std::vector<gsw::SlotSeen> seen = {
        {0, true, true, 500, false, false},
        {1, true, true, 900, false, false},
        {108, true, true, 700, false, false},
        {3, true, true, 9999, false, false},    // CrossSave: never
        {206, true, true, 9999, false, false},  // one use: never
        {2, true, false, 9999, false, false},   // no save.save
        {100, false, true, 9999, false, false}, // no lobby.save
    };
    auto p = gsw::pick_continue_slot(seen);
    CHECK(p.slot == 1u);
    seen[2].save_time = 900;  // a tie: the lower number
    p = gsw::pick_continue_slot(seen);
    CHECK(p.slot == 1u);
    seen[1].has_backup = true;  // the newest had its save interrupted: nothing (never an older one silently)
    p = gsw::pick_continue_slot(seen);
    CHECK(!p.slot && p.why.find("slot1_backup") != std::string::npos);
    seen[1].has_backup = false;
    seen[1].has_new = true;
    CHECK(!gsw::pick_continue_slot(seen).slot);
    CHECK(!gsw::pick_continue_slot(std::vector<gsw::SlotSeen>{}).slot);

    const std::vector<std::wstring> one = {L"1234567890", L"cdmp_save_sandbox", L"x1"};
    CHECK(gsw::pick_account(one, 0) == std::wstring(L"1234567890"));
    const std::vector<std::wstring> two = {L"1234567890", L"42"};
    CHECK(!gsw::pick_account(two, 0));
    CHECK(gsw::pick_account(two, 76561197960265728ull + 42) == std::wstring(L"42"));

    // The account id is the SteamID64's low 32 bits.
    CHECK(gsw::pick_account(two, (0x0110000100000000ull | 1234567890ull)) == std::wstring(L"1234567890"));
    CHECK(!gsw::pick_account(std::vector<std::wstring>{L"abc"}, 0));
}

// ---------------------------------------------------------------- manifests and reasons

void test_manifests() {
    // The phase-A mailbox manifest (with phase B's guest slot and Unix time) gives the files back.
    const Bytes lobby = make_save(390, 21), save = make_save(1000, 22);
    const gsw::Files files = gsw::files_of(lobby, save);
    gsv::Manifest m;
    m.generation = 4;
    m.guest = "76561198000000001";
    m.reason = "autosave (ret exe+0xCBD86D3)";
    m.host_slot = 204;
    m.asked_slot = 1;
    m.time_utc = "2026-10-12T02:00:00Z";
    m.guest_slot = 101;
    m.time_unix = 1791770400;
    gsv::HeaderInfo h;
    m.files = {{"lobby.save", lobby.size(), sha256::hex(files[0].sha), h}, {"save.save", save.size(), sha256::hex(files[1].sha), h}};
    const std::string text = gsv::manifest_text(m);
    CHECK(text.find("guest_slot=101\n") != std::string::npos && text.find("time_unix=1791770400\n") != std::string::npos);
    const auto kv = gsw::parse_key_values(text);
    CHECK(gsw::kv_files(kv) == files);
    CHECK(gsw::kv_u64(kv, "generation") == 4u && gsw::kv_u64(kv, "guest_slot") == 101u);
    CHECK(gsw::reason_from_text(gsw::kv_get(kv, "reason").value_or("")) == static_cast<uint8_t>(gsw::Reason::Autosave));
    gsv::Manifest a = m;
    a.guest_slot.reset();
    a.time_unix = 0;
    const std::string phase_a = gsv::manifest_text(a);
    CHECK(phase_a.find("guest_slot") == std::string::npos && phase_a.find("time_unix") == std::string::npos &&
          phase_a.find("lineage_root") == std::string::npos);
    CHECK(gsw::kv_files(gsw::parse_key_values(phase_a)) == files);
    // Session 25: slot 0 is a real slot (written), and the line goes with it.
    gsv::Manifest z = m;
    z.guest_slot = 0;
    z.lineage_root = sha256::hex(files[1].sha);
    z.lineage_base = sha256::hex(files[0].sha);
    const auto zkv = gsw::parse_key_values(gsv::manifest_text(z));
    CHECK(gsw::kv_u64(zkv, "guest_slot") == 0u && gsw::kv_root(zkv) == files[1].sha && gsw::kv_base(zkv) == files[0].sha);
    CHECK(!gsw::kv_root(kv));
    CHECK(gsw::kv_root(gsw::parse_key_values("lineage_root=zz\n")) == sha256::Digest{});

    // Broken manifests give nothing.
    CHECK(!gsw::kv_files(gsw::parse_key_values("file=lobby.save size=10 sha256=00\n")));
    CHECK(!gsw::kv_files(gsw::parse_key_values("")));
    CHECK(!gsw::kv_u64(gsw::parse_key_values("n=99999999999999999999999\n"), "n"));
    CHECK(gsw::kv_u64(gsw::parse_key_values("delivered=7\r\n"), "delivered") == 7u);

    // The guest's incoming manifest round trip.
    gsw::IncomingManifest im;
    im.gen = 3;
    im.host_id = 76561198000000002ull;
    im.slot = 1;
    im.reason = static_cast<uint8_t>(gsw::Reason::Final);
    im.time_unix = 1791770400;
    im.received_utc = "2026-10-12T02:00:05Z";
    im.files = files;
    const auto back = gsw::parse_incoming_manifest(gsw::incoming_manifest_text(im));
    CHECK(back && back->gen == 3 && back->host_id == im.host_id && back->slot == 1 && back->reason == 3 &&
          back->time_unix == im.time_unix && back->received_utc == im.received_utc && back->files == files &&
          gsw::digest_zero(back->root));
    CHECK(gsw::incoming_manifest_text(im).find("lineage_root") == std::string::npos);
    im.root = files[1].sha;
    im.base = files[0].sha;
    const auto with_root = gsw::parse_incoming_manifest(gsw::incoming_manifest_text(im));
    CHECK(with_root && with_root->root == files[1].sha && with_root->base == files[0].sha);
    gsw::IncomingManifest bad = im;
    bad.root = {};
    bad.base = {};
    CHECK(!gsw::parse_incoming_manifest(gsw::incoming_manifest_text(bad) + "lineage_base=nothex\n"));
    CHECK(!gsw::parse_incoming_manifest(gsw::incoming_manifest_text(bad) + "lineage_root=nothex\n"));
    im.slot = 3;  // never a slot 3 to apply
    CHECK(!gsw::parse_incoming_manifest(gsw::incoming_manifest_text(im)));

    // Reasons of a live export.
    using Rn = gsw::Reason;
    CHECK(gsw::reason_from_text("manual 2910 (ret exe+0x103BE99F)") == static_cast<uint8_t>(Rn::Manual));
    CHECK(gsw::reason_from_text("OTHER") == static_cast<uint8_t>(Rn::Unknown));
    CHECK(gsw::live_reason("autosave", 50000, 40000, 0) == static_cast<uint8_t>(Rn::LinkLost));
    CHECK(gsw::live_reason("autosave", 50000, 0, 45000) == static_cast<uint8_t>(Rn::Final));
    CHECK(gsw::live_reason("autosave", 50000, 0, 50000 - gsw::kRecentReqMs - 1) == static_cast<uint8_t>(Rn::Autosave));
    CHECK(gsw::live_reason("manual 2910", 50000, 0, 0) == static_cast<uint8_t>(Rn::Manual));
    for (uint8_t r = 0; r <= 4; ++r) CHECK(*gsw::reason_name(r) != '?');
}

// ---------------------------------------------------------------- the final save's Req (live test 12-10 04:12)

void test_save_req() {
    // 2910 = u32 slot | u32 n | n UTF-16 | u16 0, as the client's writer .xtls 0xA4624C0 builds it; slot byte 0.
    const Bytes f = gsw::make_final_save_req();
    const auto v = troc::parse(f);
    CHECK(v && v->opcode == 2910 && v->slot == 0 && v->payload.size() == 10 + 2 * gsw::kFinalSaveName.size());
    if (v) {
        const Bytes expect = {100, 0, 0, 0, 5, 0, 0, 0, 'C', 0, 'o', 0, '-', 0, 'o', 0, 'p', 0, 0, 0};
        CHECK(Bytes(v->payload.begin(), v->payload.end()) == expect);
        const auto r = gsw::parse_save_req(v->payload);
        CHECK(r && r->slot == gsw::kFinalSaveSlot && r->name == gsw::kFinalSaveName);
        CHECK(gsw::is_final_save_req(v->payload));
    }
    CHECK(troc::parse_wire(f, troc::Dir::ToHostServer).has_value());
    // An empty name and the longest one the handler takes (30); longer names are cut to 30.
    const Bytes empty = gsw::make_save_req(101, u"");
    CHECK(empty.size() == gsw::kTrocHeader + 10);
    const auto pe = gsw::parse_save_req(std::span<const uint8_t>(empty).subspan(gsw::kTrocHeader));
    CHECK(pe && pe->slot == 101 && pe->name.empty());
    CHECK(!gsw::is_final_save_req(std::span<const uint8_t>(empty).subspan(gsw::kTrocHeader)));
    const std::u16string long_name(40, u'a');
    const Bytes cut = gsw::make_save_req(102, long_name);
    const auto pc = gsw::parse_save_req(std::span<const uint8_t>(cut).subspan(gsw::kTrocHeader));
    CHECK(pc && pc->name.size() == gsw::kMaxSaveName);
    // Malformed forms: short, n above 30, the length not 10 + 2n, no terminator, a NUL inside.
    Bytes p(f.begin() + gsw::kTrocHeader, f.end());
    CHECK(!gsw::parse_save_req(std::span<const uint8_t>(p).first(9)));
    Bytes bad = p;
    bad.push_back(0);
    CHECK(!gsw::parse_save_req(bad));
    bad = p;
    bad[4] = 31;
    CHECK(!gsw::parse_save_req(bad));
    bad = p;
    bad[bad.size() - 2] = 'x';
    CHECK(!gsw::parse_save_req(bad));
    bad = p;
    bad[10] = 0;  // the second character NUL
    bad[11] = 0;
    CHECK(!gsw::parse_save_req(bad));

    // 3016 = u32 key | u32 slot | u8 ok | u8 flag | u32 result (writer 0x2A1FD40); the capture's failure and a save.
    const Bytes failed = {0x6B, 0x03, 0x10, 0xA0, 0, 0, 0, 0, 0, 0, 0x33, 0x2C, 0x3F, 0x12};
    const auto a = gsw::parse_save_ack(failed);
    CHECK(a && a->key == 0xA010036B && a->slot == 0 && !a->ok && a->flag == 0 && a->result == 0x123F2C33);
    const Bytes saved = {0x01, 0x02, 0x10, 0xA0, 100, 0, 0, 0, 1, 1, 0, 0, 0, 0};
    const auto b = gsw::parse_save_ack(saved);
    CHECK(b && b->slot == 100 && b->ok && b->result == 0);
    CHECK(!gsw::parse_save_ack(std::span<const uint8_t>(saved).first(13)));

    // Review item 6: the numbering stays above the guest's base / acknowledged generation after the mailbox was
    // emptied, and above the folder's newest otherwise.
    const std::vector<uint32_t> none, some = {3, 4};
    CHECK(gsv::next_generation_above(none, 0) == 1);
    CHECK(gsv::next_generation_above(none, 7) == 8);
    CHECK(gsv::next_generation_above(some, 2) == 5);
    CHECK(gsv::next_generation_above(some, 9) == 10);
    CHECK(gsv::next_generation_above(none, 0xFFFFFFFFu) == 0);

    // Review item 4: the reassembled bytes are moved out, not copied.
    const Bytes lobby = make_save(390, 61), save = make_save(30000, 62);
    gsw::Assembler in;
    CHECK(in.begin(5, gsw::files_of(lobby, save)) == AckResult::Ok);
    gsw::Outgoing out(5, lobby, save);
    Bytes payload;
    AckResult err = AckResult::Ok;
    while (out.next(payload)) in.add(*gsw::decode_chunk(payload), err);
    std::string why;
    CHECK(in.verify(why) == AckResult::Ok);
    const Bytes took = in.take(gsw::Kind::Save);
    CHECK(took == save && in.file(gsw::Kind::Save).empty());
}

}  // namespace

int main() {
    test_save_req();
    test_sha256();
    test_wire();
    test_transfer();
    test_template();
    test_result_sender();
    test_lines();
    test_guest_hold();
    test_final_save();
    test_policy();
    test_slots();
    test_manifests();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All guest save phase B tests passed\n");
    return 0;
}
