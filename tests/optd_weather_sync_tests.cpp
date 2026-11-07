// P8 session 25, weather (docs/research/noche/clima-sincronizar.md §5, "Implementado"; build 3549:
// docs/research/s25/clima-3549.md): the pure logic of weather_sync (src/mod/session2/weather_sync_logic.h): both
// builds' field tables and their block reads, the state-12 frame byte for byte (wire versions 1 and 2), sanity,
// interpolation (tick and receive-time bases, accumulator extrapolation, Raw words), the precipitation command, the
// gates, the owned-X records, the selftest state machine, the modes, the game tables and the lock-free double buffer
// (including two threads).

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#include "common/troc_frame.h"
#include "mod/session2/weather_sync_logic.h"

using namespace cdmp::session2;
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

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

const wsy::Layout& L76 = wsy::kLayout2976;
const wsy::Layout& L49 = wsy::kLayout3549;

wsy::Snapshot sample(const wsy::Layout& L, uint32_t seq, uint64_t tick, float base) {
    wsy::Snapshot s;
    s.seq = seq;
    s.tick = tick;
    s.mask = wsy::kHasA | wsy::kHasB | wsy::kHasC | wsy::kHasX | wsy::kHasPos;
    s.pos[0] = 100.0f;
    s.pos[1] = 50.0f;
    s.pos[2] = -200.0f;
    for (size_t i = 0; i < L.count(); ++i) s.w[i] = wsy::as_word(base * 0.01f + static_cast<float>(i) * 0.001f);
    return s;
}

void test_field_table_2976() {
    const wsy::Layout& L = L76;
    CHECK(L.count() == 61 && L.wire_version == 1);
    CHECK(L.idx_precip == 0 && L[L.idx_precip].off == 0x12C);
    CHECK(L[L.idx_rain].off == 0x16C && L[L.idx_snow].off == 0x168);
    CHECK(L[L.idx_cloud_base].block == wsy::Block::B && L[L.idx_cloud_base].off == 0xA8);
    CHECK(L[L.idx_scroll_x].kind == wsy::Kind::Accum && L[L.idx_scroll_z].off == 0x128);
    // Every B word from +0x64 to +0xF0, in order, no gap; the colours and the seed are Raw.
    size_t b = 0;
    for (size_t i = 0; i < L.count(); ++i) {
        if (L[i].block != wsy::Block::B) continue;
        CHECK(L[i].off == 0x64 + 4 * b);
        ++b;
    }
    CHECK(b == 36);
    // Review 4: the seed and the angle are floats held from the newest sample; only the colours are Raw.
    CHECK(L[L.index(wsy::Block::B, 0xE8)].kind == wsy::Kind::Hold);
    CHECK(L[L.index(wsy::Block::A, 0x154)].kind == wsy::Kind::Hold);
    CHECK(L[L.index(wsy::Block::B, 0xEC)].kind == wsy::Kind::Raw);
    CHECK(L[L.index(wsy::Block::C, 0x1B4)].kind == wsy::Kind::Raw);
    CHECK(L[L.index(wsy::Block::C, 0x178)].kind == wsy::Kind::Unknown);
    // Review 6: B+0x78/7C/80 are of unknown type: logged, never written.
    for (uint16_t off : {uint16_t{0x78}, uint16_t{0x7C}, uint16_t{0x80}}) {
        const size_t i = L.index(wsy::Block::B, off);
        CHECK(L[i].kind == wsy::Kind::Unknown && !wsy::written(L, i, wsy::kKnownMask) && !wsy::kept_for_release(L, i));
    }
    CHECK(wsy::written(L, L.index(wsy::Block::A, 0x154), wsy::kHasA));
    CHECK(wsy::written(L, L.index(wsy::Block::B, 0xEC), wsy::kHasB));
    CHECK(L.index(wsy::Block::A, 0x140) == L.count());  // not in the schema
    // The rain/snow outputs are sent but never written; the precipitation command is.
    CHECK(!wsy::written(L, L.idx_rain, wsy::kKnownMask) && !wsy::written(L, L.idx_snow, wsy::kKnownMask));
    CHECK(wsy::written(L, L.idx_precip, wsy::kKnownMask));
    CHECK(!wsy::written(L, L.index(wsy::Block::C, 0x178), wsy::kKnownMask));
    CHECK(!wsy::written(L, L.idx_precip, wsy::kHasB | wsy::kHasX));  // A absent
    CHECK(wsy::written(L, L.index(wsy::Block::B, 0xE8), wsy::kHasB));
    CHECK(!wsy::written(L, L.count(), wsy::kKnownMask) && !wsy::kept_for_release(L, L.count()));  // past the table
    // The selftest's A words and the local sample stay 2976's.
    const auto ra = wsy::restore_a(L);
    CHECK(ra[0] == 0x12C && ra[1] == 0x160 && ra[2] == 0x164 && ra[3] == 0x168 && ra[4] == 0x16C);
    CHECK(L.a_temp_sample == 0x198);
}

// Build 3549: the names are the game's reflected properties (clima-3549.md §3).
void test_field_table_3549() {
    const wsy::Layout& L = L49;
    CHECK(L.count() == 69 && L.wire_version == 2);
    const auto at = [&](wsy::Block b, uint16_t off) { return L.index(b, off); };
    // A: the old schema up to windDegree, then the new oceanWindDegree, altitudeWindRatio 4 bytes later, the outputs
    // 4 bytes later and the wind direction 8 bytes later than 2976's.
    CHECK(L.idx_precip == 0 && L[0].off == 0x12C);
    CHECK(L[at(wsy::Block::A, 0x154)].kind == wsy::Kind::Hold && std::strcmp(L[at(wsy::Block::A, 0x154)].name, "windDegree") == 0);
    CHECK(L[at(wsy::Block::A, 0x158)].kind == wsy::Kind::Hold &&
          std::strcmp(L[at(wsy::Block::A, 0x158)].name, "oceanWindDegree") == 0);
    CHECK(std::strcmp(L[at(wsy::Block::A, 0x15C)].name, "altitudeWindRatio") == 0);
    CHECK(L[L.idx_rain].off == 0x170 && L[L.idx_snow].off == 0x16C && L.idx_snow == 14 && L.idx_rain == 15);
    CHECK(L[at(wsy::Block::A, 0x164)].kind == wsy::Kind::Derived && L[at(wsy::Block::A, 0x168)].kind == wsy::Kind::Derived);
    CHECK(L[at(wsy::Block::A, 0x198)].kind == wsy::Kind::Lerp && L[at(wsy::Block::A, 0x19C)].kind == wsy::Kind::Lerp);
    CHECK(at(wsy::Block::A, 0x140) == L.count() && at(wsy::Block::A, 0x160) == L.count());  // not in the vector
    // B: the new colour +0x30, then +0x64..+0xF4 with no gap; 4 mie floats, cloud fields 4 bytes later than 2976's.
    CHECK(L[at(wsy::Block::B, 0x30)].kind == wsy::Kind::Raw);
    size_t b = 0;
    for (size_t i = at(wsy::Block::B, 0x64); i < L.count() && L[i].block == wsy::Block::B; ++i) {
        CHECK(L[i].off == 0x64 + 4 * b);
        ++b;
    }
    CHECK(b == 37);
    CHECK(L[L.idx_cloud_base].off == 0xAC && std::strcmp(L[L.idx_cloud_base].name, "cloudBaseDensity") == 0);
    CHECK(std::strcmp(L[at(wsy::Block::B, 0xB8)].name, "cloudScrollMultiplier") == 0);
    CHECK(std::strcmp(L[at(wsy::Block::B, 0xBC)].name, "cloudAltitude") == 0);
    CHECK(L[at(wsy::Block::B, 0xEC)].kind == wsy::Kind::Hold && std::strcmp(L[at(wsy::Block::B, 0xEC)].name, "cloudSeed") == 0);
    CHECK(L[at(wsy::Block::B, 0xF0)].kind == wsy::Kind::Raw && L[at(wsy::Block::B, 0xF4)].kind == wsy::Kind::Raw);
    for (uint16_t off : {uint16_t{0x78}, uint16_t{0x7C}, uint16_t{0x80}}) {
        const size_t i = at(wsy::Block::B, off);
        CHECK(L[i].kind == wsy::Kind::Unknown && !wsy::written(L, i, wsy::kKnownMask) && !wsy::kept_for_release(L, i));
    }
    // C: 2976's minus 0x108.
    const uint16_t c76[] = {0x178, 0x1B0, 0x1B4, 0x1B8, 0x1D4, 0x1E0};
    for (uint16_t off : c76) {
        const size_t i76 = L76.index(wsy::Block::C, off), i49 = at(wsy::Block::C, static_cast<uint16_t>(off - 0x108));
        CHECK(i49 < L.count() && L[i49].kind == L76[i76].kind);
    }
    // X: the 2976 scroll pair moved to +0x13C/+0x140; the new accumulators and the turning angle.
    CHECK(L[L.idx_scroll_x].off == 0x13C && L[L.idx_scroll_z].off == 0x140);
    for (uint16_t off : {uint16_t{0x13C}, uint16_t{0x140}, uint16_t{0x154}, uint16_t{0x158}, uint16_t{0x15C}, uint16_t{0x160}}) {
        CHECK(L[at(wsy::Block::X, off)].kind == wsy::Kind::Accum);
        CHECK(wsy::written(L, at(wsy::Block::X, off), wsy::kHasX) && !wsy::kept_for_release(L, at(wsy::Block::X, off)));
    }
    CHECK(L[at(wsy::Block::X, 0x14C)].kind == wsy::Kind::Hold && !wsy::kept_for_release(L, at(wsy::Block::X, 0x14C)));
    CHECK(at(wsy::Block::X, 0x144) == L.count() && at(wsy::Block::X, 0x148) == L.count());  // recomputed by the game
    // The selftest's A words and the local sample.
    const auto ra = wsy::restore_a(L);
    CHECK(ra[0] == 0x12C && ra[1] == 0x164 && ra[2] == 0x168 && ra[3] == 0x16C && ra[4] == 0x170);
    CHECK(L.a_temp_sample == 0x1A0);
    // Every word the guest may write is inside one guarded block read.
    CHECK(wsy::fields_inside_ranges(L) && wsy::well_formed(L));
    CHECK(L.read_a.off == 0x12C && L.read_a.size == 0x74 && L.read_b.off == 0x30 && L.read_b.size == 0xC8 &&
          L.read_c.off == 0x70 && L.read_c.size == 0x6C && L.read_x.off == 0x13C && L.read_x.size == 0x28);
}

// The exe tables: what the hook and the reader use (clima-3549.md §2 for 3549).
void test_game_tables() {
    const wsy::GameAddrs& g = wsy::kGame3549;
    CHECK(g.module_slot == 0x70D9FC0 && g.module_scene == 0x60 && g.scene_weather == 0xF70 && g.getter == 0x7AA9B0);
    CHECK(g.client_slot == 0x71BDEF8 && g.client_hop1 == 0x48 && g.client_hop2 == 0x38);
    CHECK(g.vt_weather_system == 0x611E330 && g.x_viewer == 0x31 && g.x_holder == 0x60 && g.x_wc_count == 0x88);
    CHECK(g.holder_a == 0x20 && g.holder_b == 0x28 && g.holder_c == 0x30);
    CHECK(g.update == 0x40C2580 && g.update_head.size() == 20 && g.update_call == 0x3DF5CB0);
    CHECK(g.clock_base == 0x70D9E60 && g.clock_frac == 0x70D9E70 && g.clock_bias == 0x71BE198);
    CHECK(g.update >= g.code_begin && g.update + g.update_head.size() <= g.code_end);
    // The call at the end of update_call_bytes reaches the update: E8 rel32 from its own end.
    const auto& cb = g.update_call_bytes;
    CHECK(cb.size() == 12 && cb[7] == 0xE8);
    const int32_t rel = static_cast<int32_t>(static_cast<uint32_t>(cb[8]) | (static_cast<uint32_t>(cb[9]) << 8) |
                                             (static_cast<uint32_t>(cb[10]) << 16) | (static_cast<uint32_t>(cb[11]) << 24));
    CHECK(g.update_call + cb.size() + static_cast<int64_t>(rel) == g.update);
    const auto& c76 = wsy::kGame2976.update_call_bytes;
    const int32_t rel76 = static_cast<int32_t>(static_cast<uint32_t>(c76[1]) | (static_cast<uint32_t>(c76[2]) << 8) |
                                               (static_cast<uint32_t>(c76[3]) << 16) | (static_cast<uint32_t>(c76[4]) << 24));
    CHECK(wsy::kGame2976.update_call + 5 + static_cast<int64_t>(rel76) == wsy::kGame2976.update);
    // The relocated prologue: whole verified instructions only, never a cut one, never other bytes.
    const std::vector<uint8_t> head(g.update_head.begin(), g.update_head.end());
    for (size_t n = 0; n <= head.size(); ++n) {
        const std::vector<uint8_t> part(head.begin(), head.begin() + static_cast<std::ptrdiff_t>(n));
        const bool whole = n == 5 || n == 6 || n == 7 || n == 9 || n == 11 || n == 13 || n == 20;
        CHECK(wsy::whole_head(g, part) == whole);
    }
    std::vector<uint8_t> five(head.begin(), head.begin() + 5);
    five[4] = 0x90;
    CHECK(!wsy::whole_head(g, five));
    const std::vector<uint8_t> h76(wsy::kGame2976.update_head.begin(), wsy::kGame2976.update_head.end());
    CHECK(wsy::whole_head(wsy::kGame2976, std::vector<uint8_t>(h76.begin(), h76.begin() + 5)));
    CHECK(!wsy::whole_head(wsy::kGame2976, std::vector<uint8_t>(h76.begin(), h76.begin() + 9)));
    CHECK(wsy::whole_head(wsy::kGame2976, h76));
}

void test_extract(const wsy::Layout& L) {
    uint8_t a[wsy::kMaxRead] = {}, b[wsy::kMaxRead] = {}, c[wsy::kMaxRead] = {}, x[wsy::kMaxRead] = {};
    for (size_t i = 0; i < L.read_a.size; ++i) a[i] = static_cast<uint8_t>(i);
    for (size_t i = 0; i < L.read_b.size; ++i) b[i] = static_cast<uint8_t>(0x80 + i);
    for (size_t i = 0; i < L.read_c.size; ++i) c[i] = static_cast<uint8_t>(0x40 + i);
    const float scroll[2] = {123.5f, -7.25f};
    std::memcpy(x + (L[L.idx_scroll_x].off - L.read_x.off), scroll, sizeof scroll);
    uint32_t w[wsy::kMaxFields];
    wsy::extract(L, a, b, c, x, w);
    const auto word_at = [](const uint8_t* blk, size_t o) {
        uint32_t v = 0;
        std::memcpy(&v, blk + o, 4);
        return v;
    };
    // A+0x12C is the first 4 bytes of the A copy; the rain word wherever it sits.
    CHECK(w[L.idx_precip] == 0x03020100u);
    CHECK(w[L.idx_rain] == word_at(a, L[L.idx_rain].off - L.read_a.off));
    for (size_t i = 0; i < L.count(); ++i) {
        const wsy::Field& f = L[i];
        const uint8_t* src = f.block == wsy::Block::A ? a : f.block == wsy::Block::B ? b : f.block == wsy::Block::C ? c : x;
        CHECK(w[i] == word_at(src, f.off - L.read_range(f.block).off));
    }
    for (size_t i = L.count(); i < wsy::kMaxFields; ++i) CHECK(w[i] == 0);
    CHECK(near(wsy::as_float(w[L.idx_scroll_x]), 123.5f) && near(wsy::as_float(w[L.idx_scroll_z]), -7.25f));
    // A missing block leaves zeros.
    wsy::extract(L, a, nullptr, c, x, w);
    CHECK(w[L.index(wsy::Block::B, 0x64)] == 0 && w[L.idx_precip] == 0x03020100u);
}

void test_frame_bytes_2976() {
    const wsy::Layout& L = L76;
    wsy::Snapshot s;
    s.seq = 0x01020304;
    s.tick = 0x1122334455667788ull;
    s.mask = wsy::kHasA | wsy::kHasB | wsy::kHasX | wsy::kHasPos;
    s.pos[0] = 1.0f;   // 0x3F800000
    s.pos[1] = -2.0f;  // 0xC0000000
    s.pos[2] = 0.5f;   // 0x3F000000
    for (size_t i = 0; i < L.count(); ++i) s.w[i] = 0xA0000000u + static_cast<uint32_t>(i);
    const wsy::Frame fr = wsy::frame(L, s);
    const auto f = fr.bytes();
    CHECK(f.size() == 277);
    // u16 opcode 1 | i8 -1 | u16 272
    CHECK(f[0] == 0x01 && f[1] == 0x00 && f[2] == 0xFF && f[3] == 0x10 && f[4] == 0x01);
    // u8 12 | u8 1 | u16 mask 0x001B | u32 seq | u64 tick
    CHECK(f[5] == 12 && f[6] == 1 && f[7] == 0x1B && f[8] == 0x00);
    CHECK(f[9] == 0x04 && f[10] == 0x03 && f[11] == 0x02 && f[12] == 0x01);
    CHECK(f[13] == 0x88 && f[14] == 0x77 && f[15] == 0x66 && f[16] == 0x55 && f[17] == 0x44 && f[18] == 0x33 &&
          f[19] == 0x22 && f[20] == 0x11);
    CHECK(f[21] == 0x00 && f[22] == 0x00 && f[23] == 0x80 && f[24] == 0x3F);  // pos x
    CHECK(f[25] == 0x00 && f[26] == 0x00 && f[27] == 0x00 && f[28] == 0xC0);  // pos y
    CHECK(f[33] == 0x00 && f[34] == 0x00 && f[35] == 0x00 && f[36] == 0xA0);  // word 0
    CHECK(f[276] == 0xA0 && f[273] == 60);                                    // word 60
    // The tunnel takes it as a whole packet of opcode 1 (troc_frame.h kControlOpcode) in ToGuestClient batches.
    const auto v = troc::parse(f);
    CHECK(v && v->opcode == troc::kControlOpcode && v->slot == -1 && v->payload.size() == wsy::payload_size(L));
    CHECK(troc::parse_wire(f, troc::Dir::ToGuestClient).has_value());
    static_assert(wsy::kControlWeather != troc::kControlHostReady && wsy::kControlWeather != troc::kControlHostDead &&
                  wsy::kControlWeather != troc::kControlHostCharacter);
    static_assert(wsy::kControlOpcode == troc::kControlOpcode);
    // Caps 0x04: neither the compression flag nor phase B's own-save flag (0x02, main worktree).
    static_assert(wsy::kCapsWeatherSync != troc::kCapsCompressed && wsy::kCapsWeatherSync != 0x02);
    // Round trip.
    const auto d = wsy::decode(L, f);
    CHECK(d && d->seq == s.seq && d->tick == s.tick && d->mask == s.mask && near(d->pos[1], -2.0f));
    CHECK(d && std::memcmp(d->w, s.w, sizeof s.w) == 0);
    // Exactly this form only.
    std::vector<uint8_t> bad(f.begin(), f.end());
    bad[5] = 5;  // another state
    CHECK(!wsy::is_frame(L, bad) && !wsy::decode(L, bad));
    bad.assign(f.begin(), f.end());
    bad[6] = 2;  // another version: still the frame (taken out), not decoded
    CHECK(wsy::is_frame(L, bad) && !wsy::decode(L, bad));
    bad.assign(f.begin(), f.end());
    bad[8] = 0x01;  // unknown mask bit 0x100
    CHECK(!wsy::decode(L, bad));
    std::vector<uint8_t> shorter(f.begin(), f.end() - 1);
    shorter[3] = 0x0F;
    CHECK(!wsy::is_frame(L, shorter));
    bad.assign(f.begin(), f.end());
    bad[2] = 0x00;  // slot 0
    CHECK(!wsy::is_frame(L, bad));
}

void test_frame_bytes_3549() {
    const wsy::Layout& L = L49;
    wsy::Snapshot s = sample(L, 0x0A0B0C0D, 0x0102030405060708ull, 5.0f);
    for (size_t i = 0; i < L.count(); ++i) s.w[i] = 0xB0000000u + static_cast<uint32_t>(i);
    const wsy::Frame fr = wsy::frame(L, s);
    const auto f = fr.bytes();
    // u16 opcode 1 | i8 -1 | u16 304 | u8 12 | u8 2 | ... | 69 words
    CHECK(f.size() == 309 && wsy::frame_size(L) == 309 && wsy::payload_size(L) == 304);
    CHECK(f[0] == 0x01 && f[1] == 0x00 && f[2] == 0xFF && f[3] == 0x30 && f[4] == 0x01);
    CHECK(f[5] == 12 && f[6] == 2 && f[7] == 0x1F && f[8] == 0x00);
    CHECK(f[33] == 0x00 && f[36] == 0xB0);                   // word 0
    CHECK(f[33 + 4 * 68] == 68 && f[33 + 4 * 68 + 3] == 0xB0);  // word 68, the last
    const auto v = troc::parse(f);
    CHECK(v && v->opcode == troc::kControlOpcode && v->slot == -1 && v->payload.size() == 304);
    CHECK(troc::parse_wire(f, troc::Dir::ToGuestClient).has_value());
    const auto d = wsy::decode(L, f);
    CHECK(d && d->seq == s.seq && d->tick == s.tick && d->mask == s.mask && std::memcmp(d->w, s.w, sizeof s.w) == 0);
    // The two builds never take each other's frames: another size is not this layout's frame at all, and a frame of
    // the right size with the other version is taken out but not decoded.
    CHECK(!wsy::is_frame(L76, f) && !wsy::decode(L76, f));
    const wsy::Frame old = wsy::frame(L76, sample(L76, 1, 1000, 1.0f));
    CHECK(!wsy::is_frame(L, old.bytes()) && !wsy::decode(L, old.bytes()));
    std::vector<uint8_t> v1(f.begin(), f.end());
    v1[6] = 1;
    CHECK(wsy::is_frame(L, v1) && !wsy::decode(L, v1));
}

void test_sanity(const wsy::Layout& L) {
    wsy::Snapshot s = sample(L, 1, 1000, 10.0f);
    CHECK(wsy::check(L, s).ok() && wsy::applicable(L, s));
    s.w[L.idx_cloud] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    CHECK(wsy::check(L, s).nonfinite == 1 && !wsy::applicable(L, s));
    s = sample(L, 1, 1000, 10.0f);
    s.w[L.idx_cloud_base] = wsy::as_word(3.0e8f);
    CHECK(wsy::check(L, s).huge == 1 && !wsy::check(L, s).ok());
    // Colour words that are NaNs as floats are fine (Raw is not checked), nor are words of unknown type.
    s = sample(L, 1, 1000, 10.0f);
    for (size_t i = 0; i < L.count(); ++i) {
        if (L[i].kind == wsy::Kind::Raw || L[i].kind == wsy::Kind::Unknown) s.w[i] = 0xFFFFFFFFu;
    }
    CHECK(wsy::check(L, s).ok());
    // Review 4: the Hold floats are checked.
    const size_t deg = L.index(wsy::Block::A, 0x154);
    s.w[deg] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    CHECK(!wsy::check(L, s).ok() && wsy::check(L, s).first_bad == static_cast<int>(deg));
    s = sample(L, 1, 1000, 10.0f);
    const size_t seed = L.index(wsy::Block::B, L.probe.seed);
    s.w[seed] = wsy::as_word(5.0e7f);
    CHECK(wsy::check(L, s).huge == 1 && std::strcmp(wsy::field_name(L, wsy::check(L, s).first_bad), "cloudSeed") == 0);
    // Review 8: +-FLT_MAX ("not set") passes and is counted apart.
    s = sample(L, 1, 1000, 10.0f);
    s.w[L.idx_cloud_base] = wsy::kFltMaxBits;
    s.w[L.index(wsy::Block::C, L.probe.c[0])] = wsy::kNegFltMaxBits;
    CHECK(wsy::check(L, s).ok() && wsy::check(L, s).unset == 2 && wsy::check(L, s).huge == 0 && wsy::applicable(L, s));
    CHECK(wsy::check(L, s).first_bad == -1 && std::strcmp(wsy::field_name(L, -1), "-") == 0);
    // Out of [0, 1] is counted, not fatal.
    s.w[L.idx_precip] = wsy::as_word(1.5f);
    CHECK(wsy::check(L, s).outside_unit == 1 && wsy::check(L, s).ok());
    // A block that is absent is not looked at; A, B and X are needed to apply.
    s = sample(L, 1, 1000, 10.0f);
    s.mask = wsy::kHasB | wsy::kHasX;
    s.w[L.idx_cloud] = wsy::as_word(std::numeric_limits<float>::infinity());
    CHECK(wsy::check(L, s).ok() && !wsy::applicable(L, s));
    s.mask = wsy::kHasA | wsy::kHasB | wsy::kHasX;  // no C: still applicable
    s.w[L.idx_cloud] = wsy::as_word(0.5f);
    CHECK(wsy::applicable(L, s));
    // A bad position.
    s.mask |= wsy::kHasPos;
    s.pos[0] = std::numeric_limits<float>::infinity();
    CHECK(!wsy::check(L, s).ok() && std::strcmp(wsy::field_name(L, wsy::check(L, s).first_bad), "position") == 0);
    // An accumulator of X is checked too (3549 has five).
    s = sample(L, 1, 1000, 10.0f);
    s.w[L.idx_scroll_z] = wsy::as_word(std::numeric_limits<float>::infinity());
    CHECK(!wsy::check(L, s).ok());
}

void test_blend(const wsy::Layout& L) {
    wsy::Timed prev{sample(L, 1, 10000, 0.0f), 50000};
    wsy::Timed cur{sample(L, 2, 10200, 0.0f), 50210};
    // Lerp field: cloudiness 0.2 -> 0.4; Hold: the seed differs; Accum: scroll 100 -> 102.
    prev.s.w[L.idx_cloud] = wsy::as_word(0.2f);
    cur.s.w[L.idx_cloud] = wsy::as_word(0.4f);
    const size_t seed = L.index(wsy::Block::B, L.probe.seed);
    prev.s.w[seed] = 0x11111111u;
    cur.s.w[seed] = 0x22222222u;
    prev.s.w[L.idx_scroll_x] = wsy::as_word(100.0f);
    cur.s.w[L.idx_scroll_x] = wsy::as_word(102.0f);
    uint32_t out[wsy::kMaxFields];
    // now = cur's arrival + 200 ms: host now ~ 10400, render 10100 -> t = 0.5.
    wsy::Blend b = wsy::blend(L, &prev, cur, 50410, out);
    CHECK(b.blended && !b.extrapolated && near(b.t, 0.5f));
    CHECK(near(wsy::as_float(out[L.idx_cloud]), 0.3f));
    CHECK(out[seed] == 0x22222222u);  // Hold: the newest
    CHECK(near(wsy::as_float(out[L.idx_scroll_x]), 101.0f));
    // Right at cur's arrival: render 9900 -> before prev: t = 0.
    b = wsy::blend(L, &prev, cur, 50210, out);
    CHECK(b.blended && near(b.t, 0.0f) && near(wsy::as_float(out[L.idx_cloud]), 0.2f));
    // Late: 900 ms after cur arrived -> render 10800, 600 ms past cur: Lerp holds cur, Accum goes on (slope 10/s).
    b = wsy::blend(L, &prev, cur, 51110, out);
    CHECK(b.extrapolated && near(wsy::as_float(out[L.idx_cloud]), 0.4f));
    CHECK(near(wsy::as_float(out[L.idx_scroll_x]), 108.0f, 1e-3f));
    // Very late: extrapolation capped at 1000 ms past cur.
    b = wsy::blend(L, &prev, cur, 60000, out);
    CHECK(near(wsy::as_float(out[L.idx_scroll_x]), 112.0f, 1e-3f));
    // No prev / same seq / gap too large / tick not growing and receive times not either: cur as it is.
    b = wsy::blend(L, nullptr, cur, 50410, out);
    CHECK(!b.blended && out[L.idx_cloud] == cur.s.w[L.idx_cloud]);
    wsy::Timed far_prev{sample(L, 1, 7000, 0.0f), 47000};
    b = wsy::blend(L, &far_prev, cur, 50410, out);
    CHECK(!b.blended);
    // Ticks unreadable (0): receive times are the base (210 ms apart).
    wsy::Timed p0 = prev, c0 = cur;
    p0.s.tick = 0;
    c0.s.tick = 0;
    b = wsy::blend(L, &p0, c0, 50210 + 300 + 105, out);  // render = c.recv + 405 - 300 = prev.recv + 315 -> t = 1.5 -> 1
    CHECK(b.blended && b.extrapolated);
    b = wsy::blend(L, &p0, c0, 50210 + 195, out);  // render = prev.recv + 105 -> t = 0.5
    CHECK(b.blended && near(b.t, 0.5f) && near(wsy::as_float(out[L.idx_cloud]), 0.3f));
    wsy::Timed backwards = c0;
    backwards.recv_ms = p0.recv_ms - 1;
    b = wsy::blend(L, &p0, backwards, 50410, out);
    CHECK(!b.blended);
    // A NaN in prev keeps cur's word for that field only.
    wsy::Timed pn = prev;
    pn.s.w[L.idx_cloud] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    b = wsy::blend(L, &pn, cur, 50410, out);
    CHECK(out[L.idx_cloud] == cur.s.w[L.idx_cloud] && near(wsy::as_float(out[L.idx_scroll_x]), 101.0f));
    // FLT_MAX ("not set") on either side is never interpolated: cur's word as it is.
    wsy::Timed pu = prev, cu = cur;
    pu.s.w[L.idx_cloud] = wsy::kFltMaxBits;
    b = wsy::blend(L, &pu, cur, 50410, out);
    CHECK(out[L.idx_cloud] == cur.s.w[L.idx_cloud]);
    cu.s.w[L.idx_cloud] = wsy::kFltMaxBits;
    b = wsy::blend(L, &prev, cu, 50410, out);
    CHECK(out[L.idx_cloud] == wsy::kFltMaxBits);
    // Hold: the newest, never mixed (the wind angle 350 -> 10 must not sweep through 180).
    const size_t deg = L.index(wsy::Block::A, 0x154);
    wsy::Timed ph = prev, ch = cur;
    ph.s.w[deg] = wsy::as_word(350.0f);
    ch.s.w[deg] = wsy::as_word(10.0f);
    b = wsy::blend(L, &ph, ch, 50410, out);
    CHECK(b.blended && out[deg] == wsy::as_word(10.0f));
}

// 3549's new words: the ocean wind angle and X's turning angle are held, the new scroll pairs extrapolate.
void test_blend_3549() {
    const wsy::Layout& L = L49;
    wsy::Timed prev{sample(L, 1, 10000, 0.0f), 50000};
    wsy::Timed cur{sample(L, 2, 10200, 0.0f), 50210};
    const size_t ocean = L.index(wsy::Block::A, 0x158), turn = L.index(wsy::Block::X, 0x14C),
                 s2 = L.index(wsy::Block::X, 0x154), s3 = L.index(wsy::Block::X, 0x160);
    prev.s.w[ocean] = wsy::as_word(355.0f);
    cur.s.w[ocean] = wsy::as_word(5.0f);
    prev.s.w[turn] = wsy::as_word(359.0f);
    cur.s.w[turn] = wsy::as_word(1.0f);
    prev.s.w[s2] = wsy::as_word(10.0f);
    cur.s.w[s2] = wsy::as_word(12.0f);
    prev.s.w[s3] = wsy::as_word(-4.0f);
    cur.s.w[s3] = wsy::as_word(-6.0f);
    uint32_t out[wsy::kMaxFields];
    wsy::blend(L, &prev, cur, 50410, out);  // t = 0.5
    CHECK(out[ocean] == wsy::as_word(5.0f) && out[turn] == wsy::as_word(1.0f));
    CHECK(near(wsy::as_float(out[s2]), 11.0f) && near(wsy::as_float(out[s3]), -5.0f));
    wsy::blend(L, &prev, cur, 51110, out);  // 600 ms past cur: the accumulators go on
    CHECK(near(wsy::as_float(out[s2]), 18.0f, 1e-3f) && near(wsy::as_float(out[s3]), -12.0f, 1e-3f));
}

void test_precip_command(const wsy::Layout& L) {
    uint32_t w[wsy::kMaxFields] = {};
    w[L.idx_precip] = wsy::as_word(0.9f);  // the host's target: NOT what is written
    w[L.idx_rain] = wsy::as_word(0.35f);
    w[L.idx_snow] = wsy::as_word(0.0f);
    CHECK(near(wsy::precip_command(L, w), 0.35f));
    CHECK(near(wsy::as_float(wsy::value_to_write(L, L.idx_precip, w)), 0.35f));
    w[L.idx_snow] = wsy::as_word(0.6f);
    CHECK(near(wsy::precip_command(L, w), 0.6f));
    w[L.idx_rain] = wsy::as_word(std::numeric_limits<float>::quiet_NaN());
    w[L.idx_snow] = wsy::as_word(3.0f);
    CHECK(near(wsy::precip_command(L, w), 1.0f));
    w[L.idx_snow] = wsy::as_word(-1.0f);
    CHECK(near(wsy::precip_command(L, w), 0.0f));
    // "Not set" is no rain (FLT_MAX must never become a downpour).
    w[L.idx_rain] = wsy::kFltMaxBits;
    w[L.idx_snow] = wsy::as_word(0.2f);
    CHECK(near(wsy::precip_command(L, w), 0.2f));
    // Any other field is written as it is.
    w[L.idx_cloud] = 0x3F000000u;
    CHECK(wsy::value_to_write(L, L.idx_cloud, w) == 0x3F000000u);
}

void test_gates() {
    wsy::GateIn in;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NotRemote);
    in.remote_live = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NotInWorld);
    in.in_world = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NoPosition);
    in.host_pos = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::NoPosition);  // ours too
    in.own_pos = true;
    // Review 5: radius 2000 m, margin 100 m: a closed gate opens inside 1900 m, an open one closes outside 2100 m.
    const auto margin_is = [](uint32_t r, double m) { return std::fabs(wsy::radius_margin_m(r) - m) < 1e-9; };
    CHECK(margin_is(2000, 100.0) && margin_is(100, 10.0) && margin_is(10, 5.0) && margin_is(400, 20.0));
    CHECK(margin_is(10000, 100.0) && margin_is(0, 0.0));
    in.own[0] = 1000.0f;
    in.host[0] = -899.0f;  // 1899 m
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[0] = -950.0f;  // 1950 m: closed stays closed, open stays open
    CHECK(wsy::decide_gate(in) == wsy::Gate::TooFar);
    in.was_open = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[0] = -1099.0f;  // 2099 m
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[0] = -1101.0f;  // 2101 m
    CHECK(wsy::decide_gate(in) == wsy::Gate::TooFar);
    in.was_open = false;
    in.radius_m = 5000;
    CHECK(wsy::decide_gate(in) == wsy::Gate::Apply);
    in.host[1] = std::numeric_limits<float>::infinity();
    CHECK(wsy::decide_gate(in) == wsy::Gate::TooFar);
    in.host[1] = 0.0f;
    in.cutscene = true;
    CHECK(wsy::decide_gate(in) == wsy::Gate::Cutscene);
    // Freshness: under 1 s since it arrived.
    CHECK(wsy::fresh(10999, 10000) && !wsy::fresh(11000, 10000) && !wsy::fresh(5000, 0));
    // S25: a sample stamped after the reader's clock read (between it and the mailbox read) is fresh, age 0.
    CHECK(wsy::fresh(9999, 10000) && wsy::fresh(10000, 10016) && wsy::age_ms(10000, 10016) == 0 &&
          wsy::age_ms(10016, 10000) == 16);
}

void test_world_watch() {
    wsy::WorldWatch w;
    w.feed(1000, true);
    w.feed(3999, true);
    CHECK(!w.ready());
    w.feed(4000, true);
    CHECK(w.ready());
    // One failed read (or a few within 2 s) never ends it.
    w.feed(4200, false);
    w.feed(6100, false);
    CHECK(w.ready());
    w.feed(6200, false);
    CHECK(!w.ready());
    // Back: 3 s of good reads again.
    w.feed(7000, true);
    w.feed(9999, true);
    CHECK(!w.ready());
    w.feed(10000, true);
    CHECK(w.ready());
    // A good read in between restarts the leave timer.
    w.feed(11000, false);
    w.feed(12500, true);
    w.feed(13000, false);
    w.feed(14900, false);
    CHECK(w.ready());
    w.feed(15000, false);
    CHECK(!w.ready());
}

void test_line_gate() {
    wsy::LineGate g(5000);
    CHECK(g.take(100));
    CHECK(!g.take(200) && !g.take(4000) && g.pending() == 2);
    CHECK(g.take(5100) && g.suppressed_and_reset() == 2 && g.pending() == 0);
    CHECK(!g.take(5200) && g.take(10100) && g.suppressed_and_reset() == 1);
}

void test_fault_latch() {
    wsy::FaultLatch f;
    CHECK(!f.blocked() && !f.on_frame(true) && !f.on_gate_epoch(0));
    f.on_gate_epoch(3);  // an edge before the fault is already seen: it never clears the fault
    f.on_fault();
    CHECK(f.blocked() && f.faults() == 1);
    CHECK(!f.on_gate_epoch(3));
    // Clean frames: a failed read restarts the count.
    for (uint32_t i = 0; i + 1 < wsy::FaultLatch::kCleanFrames; ++i) CHECK(!f.on_frame(true));
    CHECK(!f.on_frame(false));
    for (uint32_t i = 0; i + 1 < wsy::FaultLatch::kCleanFrames; ++i) CHECK(!f.on_frame(true));
    CHECK(f.on_frame(true) && !f.blocked());
    // A new open edge clears at once.
    f.on_fault();
    CHECK(f.blocked() && f.on_gate_epoch(4) && !f.blocked());
    // At most kMaxFaults: then it stays blocked for good.
    wsy::FaultLatch g;
    for (uint32_t i = 0; i < wsy::FaultLatch::kMaxFaults; ++i) {
        g.on_fault();
        g.on_gate_epoch(100 + i);
    }
    CHECK(g.exhausted() && g.blocked() && !g.on_gate_epoch(999));
    for (uint32_t i = 0; i < 2 * wsy::FaultLatch::kCleanFrames; ++i) CHECK(!g.on_frame(true));
    CHECK(g.blocked());
}

void test_restore_bits(const wsy::Layout& L) {
    // Kept: every holder word that may be written or that X's update rewrites; never X's words, never the words of
    // unknown type.
    CHECK(wsy::kept_for_release(L, L.idx_precip) && wsy::kept_for_release(L, L.idx_rain));
    CHECK(wsy::kept_for_release(L, L.index(wsy::Block::B, 0xF0)) && wsy::kept_for_release(L, L.index(wsy::Block::A, 0x154)));
    CHECK(!wsy::kept_for_release(L, L.idx_scroll_x) && !wsy::kept_for_release(L, L.index(wsy::Block::C, L.read_c.off)));
    wsy::Bits kept, written;
    for (size_t i = 0; i < L.count(); ++i) {
        if (wsy::kept_for_release(L, i)) kept.set(i);
        if (wsy::written(L, i, wsy::kHasA | wsy::kHasB | wsy::kHasX)) written.set(i);
    }
    // Written A/B words go back; C was never written (absent from the samples); the outputs only if the flag was ours.
    const wsy::Bits r = wsy::restore_bits(L, kept, written, /*flag_was_ours=*/true);
    CHECK(r.test(L.idx_precip) && r.test(L.idx_rain) && r.test(L.idx_cloud_base));
    CHECK(!r.test(L.index(wsy::Block::C, L.probe.c[0])) && !r.test(L.idx_scroll_x));
    const wsy::Bits r2 = wsy::restore_bits(L, kept, written, /*flag_was_ours=*/false);
    CHECK(!r2.test(L.idx_rain) && r2.test(L.idx_precip));
    CHECK(wsy::restore_bits(L, wsy::Bits{}, written, true).none());
    CHECK(wsy::field_bit(L.count() - 1).test(L.count() - 1) && wsy::field_bit(L.count() - 1).count() == 1);
    CHECK(wsy::field_bit(wsy::kMaxFields).none());
    // Every bit of a table fits (3549 has more than 64 words).
    CHECK(L.count() <= wsy::kMaxFields && kept.count() <= L.count());
}

void test_owned() {
    wsy::OwnedSet<2> s;
    CHECK(s.size() == 0 && !s.find(0x1000) && !s.add(0));
    wsy::Owned* a = s.add(0x1000);
    CHECK(a && a->x == 0x1000 && s.size() == 1);
    a->orig_flag = 0;
    a->has_orig_a = true;
    a->orig_a[0] = 7;
    CHECK(s.add(0x1000) == a && s.find(0x1000)->orig_a[0] == 7);
    CHECK(s.add(0x2000) && !s.add(0x3000) && s.size() == 2);  // full
    s.remove(0x1000);
    CHECK(!s.find(0x1000) && s.size() == 1);
    wsy::Owned* c = s.add(0x3000);
    CHECK(c && !c->has_orig_a && c->orig_a[0] == 0 && c->kept.none() && !c->flag_ours);  // a fresh record
    // Review 3: the records of any other X go (handed to the callback first), the current one stays.
    wsy::OwnedSet<4> t;
    t.add(0x1000)->flag_ours = true;
    t.add(0x2000);
    t.add(0x3000);
    std::vector<uintptr_t> seen;
    CHECK(t.evict_others(0x2000, [&](wsy::Owned& o) { seen.push_back(o.x); }) == 2);
    CHECK(seen.size() == 2 && seen[0] == 0x1000 && seen[1] == 0x3000 && t.size() == 1 && t.find(0x2000));
    // A re-created X never fills the set: each new address evicts the old one.
    for (uintptr_t x = 0x10000; x < 0x10000 + 64 * 0x100; x += 0x100) {
        t.evict_others(x, [](wsy::Owned&) {});
        CHECK(t.add(x) != nullptr);
    }
    CHECK(t.size() == 1);
}

void test_selftest() {
    wsy::Selftest st(wsy::Selftest::Config{1000, 3000});
    CHECK(st.phase() == wsy::StPhase::WaitWorld);
    wsy::StStep s = st.step(0, false, true);
    CHECK(!s.write && !s.restore && st.phase() == wsy::StPhase::WaitWorld);
    s = st.step(100, true, true);
    CHECK(st.phase() == wsy::StPhase::Settle && !s.write);
    s = st.step(600, false, true);  // the world left: wait again
    CHECK(st.phase() == wsy::StPhase::WaitWorld);
    st.step(700, true, true);
    s = st.step(1600, true, true);
    CHECK(!s.write && st.phase() == wsy::StPhase::Settle);
    s = st.step(1700, true, true);
    CHECK(s.write && s.began && !s.restore && st.phase() == wsy::StPhase::Apply);
    s = st.step(2000, true, true);
    CHECK(s.write && !s.began);
    s = st.step(4699, true, true);
    CHECK(s.write);
    s = st.step(4700, true, true);
    CHECK(!s.write && s.restore && st.phase() == wsy::StPhase::Done);
    s = st.step(9000, true, true);
    CHECK(!s.write && !s.restore && st.phase() == wsy::StPhase::Done);  // once per game start
    CHECK(!st.fault(9000));

    // Another X while applying: aborted, restore asked once.
    wsy::Selftest b(wsy::Selftest::Config{0, 3000});
    b.step(0, true, true);
    CHECK(b.step(1, true, true).write);
    s = b.step(2, true, false);
    CHECK(s.restore && !s.write && b.phase() == wsy::StPhase::Aborted);
    CHECK(!b.step(3, true, true).restore);
    // The world leaving while applying.
    wsy::Selftest c(wsy::Selftest::Config{0, 3000});
    c.step(0, true, true);
    c.step(1, true, true);
    CHECK(c.step(2, false, true).restore && c.phase() == wsy::StPhase::Aborted);
    // A fault while applying.
    wsy::Selftest d(wsy::Selftest::Config{0, 3000});
    d.step(0, true, true);
    d.step(1, true, true);
    CHECK(d.fault(5) && d.phase() == wsy::StPhase::Aborted && !d.fault(6));
    CHECK(!d.step(7, true, true).write);
    // Defaults: 15 s of natural weather, then 30 s.
    CHECK(wsy::Selftest().config().settle_ms == 15000 && wsy::Selftest().config().duration_ms == 30000);
}

void test_modes() {
    CHECK(wsy::parse_mode("0").mode == wsy::Mode::Off && wsy::parse_mode("").known);
    CHECK(wsy::parse_mode("probe").mode == wsy::Mode::Probe);
    CHECK(wsy::parse_mode(" SelfTest ; P1").mode == wsy::Mode::Selftest);
    CHECK(wsy::parse_mode("1").mode == wsy::Mode::On && wsy::parse_mode("on").mode == wsy::Mode::On);
    const auto bad = wsy::parse_mode("2");
    CHECK(!bad.known && bad.mode == wsy::Mode::Off);
    CHECK(std::strcmp(wsy::mode_name(wsy::Mode::Selftest), "selftest") == 0);
}

void test_box(const wsy::Layout& L) {
    wsy::Timed t{sample(L, 7, 0x123456789ull, 3.0f), 0xABCDEF012ull};
    uint32_t w[wsy::kBoxWords];
    wsy::pack(t, w);
    wsy::Timed u;
    wsy::unpack(w, u);
    CHECK(u.s.seq == 7 && u.s.tick == 0x123456789ull && u.recv_ms == 0xABCDEF012ull && u.s.mask == t.s.mask);
    CHECK(near(u.s.pos[2], -200.0f) && std::memcmp(u.s.w, t.s.w, sizeof t.s.w) == 0);

    wsy::DoubleBuffer<wsy::kBoxWords> box;
    uint64_t seq = 0;
    CHECK(box.seq() == 0 && !box.read(w, seq));
    box.publish(w);
    uint32_t r[wsy::kBoxWords];
    CHECK(box.read(r, seq) && seq == 1 && std::memcmp(r, w, sizeof w) == 0);
    wsy::Timed t2 = t;
    t2.s.seq = 8;
    wsy::pack(t2, w);
    box.publish(w);
    CHECK(box.read(r, seq) && seq == 2 && r[0] == 8);

    // History: the two newest distinct samples.
    wsy::History h;
    h.push(t, 1);
    CHECK(h.has_cur && !h.has_prev && h.cur.s.seq == 7);
    h.push(t, 2);  // the same sample again: no prev
    CHECK(!h.has_prev);
    h.push(t2, 3);
    CHECK(h.has_prev && h.prev.s.seq == 7 && h.cur.s.seq == 8 && h.seen == 3);
}

void test_box_threads() {
    // Two threads: every copy a reader keeps is one whole publish (all words carry the same value).
    constexpr size_t N = 64;
    wsy::DoubleBuffer<N> b2;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> torn{0}, good{0};
    std::thread reader([&] {
        uint32_t c[N];
        uint64_t s = 0;
        while (!stop.load()) {
            if (!b2.read(c, s)) continue;
            bool same = true;
            for (size_t i = 1; i < N; ++i) same = same && c[i] == c[0];
            (same && c[0] == static_cast<uint32_t>(s) ? good : torn).fetch_add(1);
        }
    });
    uint32_t src[N];
    for (uint32_t k = 1; k <= 200000; ++k) {
        for (size_t i = 0; i < N; ++i) src[i] = k;
        b2.publish(src);
    }
    stop.store(true);
    reader.join();
    CHECK(torn.load() == 0);
    CHECK(good.load() > 0);
}

}  // namespace

int main() {
    test_field_table_2976();
    test_field_table_3549();
    test_game_tables();
    for (const wsy::Layout* L : {&L76, &L49}) {
        test_extract(*L);
        test_sanity(*L);
        test_blend(*L);
        test_precip_command(*L);
        test_restore_bits(*L);
        test_box(*L);
    }
    test_frame_bytes_2976();
    test_frame_bytes_3549();
    test_blend_3549();
    test_gates();
    test_world_watch();
    test_line_gate();
    test_fault_latch();
    test_owned();
    test_selftest();
    test_modes();
    test_box_threads();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("optd_weather_sync: all checks passed\n");
    return 0;
}
