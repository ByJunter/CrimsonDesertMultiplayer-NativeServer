// P8 session 25, the guest's own save on the host, phase A
// (src/mod/session2/guest_save_logic.h; docs/research/noche/progreso-invitado.md,
// "Implementado (fase A)").
//
// SCOPE: offline logic only. The guest_own_save modes, the slot rules (204, not
// the memory-only 201/205-207), the save header check (the game's, plus the
// file size), the LoadSlot / SavePlayerToSlot decisions per caller and owner
// with the fail-closed properties, the slot-file plan for AddFile, the caller
// tables and the mailbox generations. They do NOT test the hooks, the game's
// functions they call, the byte checks against the running game nor the
// game's behaviour with the guest's slot (L1-L6, live).

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "mod/session2/guest_save_logic.h"

using namespace cdmp::session2;
using gsv::Action;
using gsv::LoadCaller;
using gsv::Mode;
using gsv::Owner;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// ---------------------------------------------------------------- the option

void test_parse_mode() {
    struct Case {
        const char* raw;
        Mode mode;
        bool known;
    };
    const Case cases[] = {
        {"", Mode::Off, true},          {"0", Mode::Off, true},           {" off ", Mode::Off, true},
        {"probe", Mode::Probe, true},   {"PROBE\r", Mode::Probe, true},   {"probe ; S25", Mode::Probe, true},
        {"1", Mode::On, true},          {"on", Mode::On, true},           {"1#x", Mode::On, true},
        {"2", Mode::Off, false},        {"probes", Mode::Off, false},     {"redirect", Mode::Off, false},
    };
    for (const Case& c : cases) {
        const gsv::ParsedMode p = gsv::parse_mode(c.raw);
        CHECK(p.mode == c.mode);
        CHECK(p.known == c.known);
    }
    CHECK(std::string(gsv::mode_name(Mode::Probe)) == "probe");
}

// ---------------------------------------------------------------- slots

void test_slots() {
    for (uint32_t n = 0; n < 400; ++n) {
        const bool valid = n <= 3 || (n >= 100 && n <= 108) || (n >= 200 && n <= 207);
        CHECK(gsv::slot_number_valid(n) == valid);
        const bool memory = n == 201 || n == 205 || n == 206 || n == 207;
        CHECK(gsv::slot_memory_only(n) == memory);
        const bool allowed = n == 200 || n == 202 || n == 203 || n == 204;
        CHECK(gsv::guest_slot_allowed(n) == allowed);
    }
    CHECK(gsv::slot_number_valid(0xFFFFFFFFu) == false);
    CHECK(gsv::guest_slot_allowed(gsv::kGuestSlot));
    CHECK(!gsv::guest_slot_allowed(207));  // the report's choice: memory-only through GetOrCreateSlot
    CHECK(gsv::slot_name(204) == "slot204");
    static_assert(gsv::guest_slot_allowed(gsv::kGuestSlot));
}

// ---------------------------------------------------------------- the header

gsv::Header make_header(uint16_t version, uint32_t flags, uint32_t raw, uint32_t data) {
    gsv::Header h{};
    const uint32_t magic = gsv::kMagic;
    const uint16_t hs = 0x80;
    std::memcpy(h.data(), &magic, 4);
    std::memcpy(h.data() + 4, &version, 2);
    std::memcpy(h.data() + 6, &hs, 2);
    std::memcpy(h.data() + 0xC, &flags, 4);
    std::memcpy(h.data() + 0x12, &raw, 4);
    std::memcpy(h.data() + 0x16, &data, 4);
    for (size_t i = 0x1A; i < h.size(); ++i) h[i] = static_cast<uint8_t>(i * 7);
    return h;
}

void test_header() {
    using C = gsv::HeaderCheck;
    // The template's lobby.save: v2, flags 2, raw 0x21B, data 0x186, 518 bytes.
    gsv::Header h = make_header(2, 2, 0x21B, 0x186);
    gsv::HeaderInfo info;
    CHECK(gsv::check_header(h, 518, &info) == C::Ok);
    CHECK(info.version == 2 && info.flags == 2 && info.raw_size == 0x21B && info.data_size == 0x186);
    CHECK(gsv::check_header(make_header(1, 0, 10, 10), 0x80 + 10) == C::Ok);
    CHECK(gsv::check_header(std::span<const uint8_t>(h.data(), 0x7F), 518) == C::TooShort);
    CHECK(gsv::check_header(h, 0x7F) == C::TooShort);
    gsv::Header bad = h;
    bad[0] = 'X';
    CHECK(gsv::check_header(bad, 518) == C::Magic);
    CHECK(gsv::check_header(make_header(0, 2, 0x21B, 0x186), 518) == C::Version);
    CHECK(gsv::check_header(make_header(3, 2, 0x21B, 0x186), 518) == C::Version);
    bad = h;
    bad[6] = 0x40;
    CHECK(gsv::check_header(bad, 518) == C::HeaderSize);
    CHECK(gsv::check_header(make_header(2, 1, 0x21B, 0x186), 518) == C::Flags);
    CHECK(gsv::check_header(make_header(2, 6, 0x21B, 0x186), 518) == C::Flags);
    CHECK(gsv::check_header(make_header(2, 2, 0, 0x186), 518) == C::RawSize);
    CHECK(gsv::check_header(make_header(2, 2, 0x21B, 0), 0x80) == C::DataSize);
    CHECK(gsv::check_header(make_header(2, 2, 0x100, 0x101), 0x80 + 0x101) == C::DataSize);
    CHECK(gsv::check_header(h, 519) == C::FileSize);
    CHECK(gsv::check_header(h, 517) == C::FileSize);
    CHECK(gsv::check_header(make_header(2, 2, 0x2000000, 0x1000001), 0x80 + 0x1000001ull) == C::TooBig);
    // Phase B review: the raw size the loader allocates is bounded (64 MB, 255 x the data); the real files pass.
    CHECK(gsv::check_header(make_header(2, 2, 0x4000001, 0x100000), 0x80 + 0x100000) == C::RawTooBig);
    CHECK(gsv::check_header(make_header(2, 2, 0x4000000, 0x100000), 0x80 + 0x100000) == C::Ok);
    CHECK(gsv::check_header(make_header(2, 2, 256 * 100, 100), 0x80 + 100) == C::RawTooBig);
    CHECK(gsv::check_header(make_header(2, 2, 255 * 100, 100), 0x80 + 100) == C::Ok);
    CHECK(gsv::check_header(make_header(2, 2, 2759863, 736603), 736731) == C::Ok);
    for (int c = 0; c <= static_cast<int>(C::RawTooBig); ++c) CHECK(*gsv::header_check_name(static_cast<C>(c)) != '?');

    // header_matches: all 0x80 bytes count (the loader copies them all to +0x98).
    gsv::Header other = h;
    CHECK(gsv::header_matches(h, other));
    other[0x7F] ^= 1;
    CHECK(!gsv::header_matches(h, other));
    other = h;
    other[0x1A] ^= 0x80;  // the nonce
    CHECK(!gsv::header_matches(h, other));
}

// The real files of another account's slot (A4/B3 loaded them as slot 108 and as user 2), when this PC has them.
void test_template_files() {
    const std::string base = "C:\\Emulador CD\\backups\\plantillas\\chapter2-normal-nexus2863\\slot108";
    int seen = 0;
    for (const char* name : {"lobby.save", "save.save"}) {
        std::ifstream f(base + "\\" + name, std::ios::binary);
        if (!f) continue;
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        gsv::HeaderInfo info;
        CHECK(gsv::check_header(bytes, bytes.size(), &info) == gsv::HeaderCheck::Ok);
        CHECK(info.version == 2);
        ++seen;
    }
    std::printf("template files checked: %d of 2 (%s)\n", seen, base.c_str());
}

// ---------------------------------------------------------------- the slot's files

void test_slot_files() {
    const char lobby[8] = {'l', 'o', 'b', 'b', 'y', 0, 'x', 'x'};
    const char save[8] = {'s', 'a', 'v', 'e', 0, 'q', 'q', 'q'};
    const char saves[8] = {'s', 'a', 'v', 'e', 's', 0, 0, 0};
    const char full[8] = {'l', 'o', 'b', 'b', 'y', 'l', 'o', 'b'};
    const char empty[8] = {};
    CHECK(gsv::classify_file_name(lobby) == gsv::FileName::Lobby);
    CHECK(gsv::classify_file_name(save) == gsv::FileName::Save);
    CHECK(gsv::classify_file_name(saves) == gsv::FileName::Other);
    CHECK(gsv::classify_file_name(full) == gsv::FileName::Other);
    CHECK(gsv::classify_file_name(empty) == gsv::FileName::Other);

    using P = gsv::FilesPlan;
    // (names vector, name map): both structures must agree.
    CHECK(gsv::plan_slot_files({0, 0, 0}, {0, 0, 0}) == P::AddBoth);
    CHECK(gsv::plan_slot_files({1, 0, 0}, {1, 0, 0}) == P::AddSave);
    CHECK(gsv::plan_slot_files({0, 1, 0}, {0, 1, 0}) == P::AddLobby);
    CHECK(gsv::plan_slot_files({1, 1, 0}, {1, 1, 0}) == P::Ready);
    CHECK(gsv::plan_slot_files({1, 1, 3}, {1, 1, 2}) == P::Ready);
    CHECK(gsv::plan_slot_files({2, 1, 0}, {1, 1, 0}) == P::Duplicate);
    CHECK(gsv::plan_slot_files({1, 1, 0}, {1, 2, 0}) == P::Duplicate);
    CHECK(gsv::plan_slot_files({1, 1, 0}, {0, 1, 0}) == P::Inconsistent);
    CHECK(gsv::plan_slot_files({0, 0, 0}, {0, 1, 0}) == P::Inconsistent);
    CHECK(gsv::slot_flags_disk(0, 0));
    CHECK(!gsv::slot_flags_disk(1, 0));
    CHECK(!gsv::slot_flags_disk(0, 1));

    CHECK(gsv::load_kind(0) == gsv::FileKind::Lobby);
    CHECK(gsv::load_kind(1) == gsv::FileKind::Save);
    CHECK(gsv::load_kind(0xFF) == gsv::FileKind::Save);
    CHECK(std::wstring(gsv::file_kind_file(gsv::FileKind::Save)) == L"save.save");
}

// A fake image of game memory at a fixed base, with the slot laid out as build 2976 keeps it
// (guest_save_logic.h, "the real layout"; the live failure of 12-10 00:36 read the name vector as file objects).
class FakeMem {
public:
    static constexpr uintptr_t kBase = 0x7FF600000000ull;
    explicit FakeMem(size_t size) : bytes_(size, 0) {}
    bool read(uintptr_t a, void* out, size_t n) const {
        if (a < kBase || a - kBase + n > bytes_.size()) return false;
        std::memcpy(out, bytes_.data() + (a - kBase), n);
        return true;
    }
    template <class T>
    void put(uintptr_t a, T v) {
        std::memcpy(bytes_.data() + (a - kBase), &v, sizeof v);
    }
    void put_str(uintptr_t a, const char* s) { std::memcpy(bytes_.data() + (a - kBase), s, std::strlen(s) + 1); }
    size_t size() const { return bytes_.size(); }

private:
    std::vector<uint8_t> bytes_;
};

constexpr uintptr_t B = FakeMem::kBase;
constexpr uintptr_t kSlot = B + 0x1000, kVec = B + 0x3000, kLobbyChars = B + 0x4000, kSaveChars = B + 0x4010;
constexpr uintptr_t kBuckets = B + 0x5000, kEntries = B + 0x6000, kLobbyEntry = B + 0x7000, kLobbyKey = B + 0x7100;
constexpr uintptr_t kSaveEntry = B + 0x7200, kSaveKey = B + 0x7300, kLobbyObj = B + 0x8000, kSaveObj = B + 0x9000;

// What two AddFile calls leave in an empty slot: the vector points at the keys' characters; the map (2 buckets)
// holds one entry per name with the file object at +0x10.
FakeMem registered_slot() {
    FakeMem m(0xA000);
    m.put_str(kLobbyChars, "lobby");
    m.put_str(kSaveChars, "save");
    m.put<uint32_t>(kSlot + gsv::kSlotNameCount, 2);
    m.put<uintptr_t>(kSlot + gsv::kSlotNames, kVec);
    m.put<uintptr_t>(kVec, kLobbyChars);
    m.put<uintptr_t>(kVec + 8, kSaveChars);
    const uintptr_t map = kSlot + gsv::kSlotNameMap;
    m.put<uint32_t>(map + gsv::kMapBucketCount, 2);
    m.put<uint32_t>(map + 4, 2);  // the element count (callers test it; the walk does not need it)
    m.put<uintptr_t>(map + gsv::kMapBuckets, kBuckets);
    m.put<uintptr_t>(map + gsv::kMapEntries, kEntries);
    m.put<uint32_t>(kBuckets, 1);  // bucket 0: {hash, index 1}
    m.put<uint32_t>(kBuckets + 8, 0x2222);
    m.put<uint32_t>(kBuckets + 12, 1);
    m.put<uint32_t>(kBuckets + 0x100, 1);  // bucket 1: {hash, index 0}
    m.put<uint32_t>(kBuckets + 0x108, 0x1111);
    m.put<uint32_t>(kBuckets + 0x10C, 0);
    m.put<uintptr_t>(kEntries, kLobbyEntry);
    m.put<uintptr_t>(kEntries + 8, kSaveEntry);
    m.put<uintptr_t>(kLobbyEntry + gsv::kEntryKey, kLobbyKey);
    m.put<uintptr_t>(kLobbyEntry + gsv::kEntryValue, kLobbyObj);
    m.put<uintptr_t>(kLobbyKey + gsv::kKeyChars, kLobbyChars);
    m.put<uint32_t>(kLobbyKey + gsv::kKeyLength, 5);
    m.put<uintptr_t>(kSaveEntry + gsv::kEntryKey, kSaveKey);
    m.put<uintptr_t>(kSaveEntry + gsv::kEntryValue, kSaveObj);
    m.put<uintptr_t>(kSaveKey + gsv::kKeyChars, kSaveChars);
    m.put<uint32_t>(kSaveKey + gsv::kKeyLength, 0xFFFFFFFFu);  // not counted yet
    // The file objects as AddFile makes them: name string inline at +0 (heap pointer +0x88 null), header at +0x98.
    m.put_str(kLobbyObj, "lobby");
    m.put_str(kSaveObj, "save");
    m.put<uint32_t>(kLobbyObj + 0x98, gsv::kMagic);
    m.put<uint32_t>(kSaveObj + 0x98, gsv::kMagic);
    return m;
}

void test_slot_layout() {
    using P = gsv::FilesPlan;
    // The registered slot: both names in both structures, the loader's objects found.
    {
        const FakeMem m = registered_slot();
        const gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok);
        CHECK(v.names.lobby == 1 && v.names.save == 1 && v.names.other == 0);
        CHECK(v.mapped.lobby == 1 && v.mapped.save == 1 && v.mapped.other == 0);
        CHECK(v.lobby == kLobbyObj && v.save == kSaveObj);
        CHECK(gsv::plan_slot_files(v.names, v.mapped) == P::Ready);
        CHECK(v.name_list.size() == 2 && v.map_list.size() == 2);
        const std::string d = gsv::describe_slot_files(v);
        CHECK(d.find("'lobby'") != std::string::npos && d.find("'save'") != std::string::npos);
        CHECK(d.find("hash 0x00001111") != std::string::npos && d.find("len ?") != std::string::npos);
        CHECK(d.find("READ FAILED") == std::string::npos);
        // The map's objects are the file objects (header at +0x98), not the vector's entries (names).
        uint32_t magic = 0;
        CHECK(m.read(v.lobby + 0x98, &magic, sizeof magic) && magic == gsv::kMagic);
    }
    // An empty slot (GetOrCreateSlot's new one): nothing in either structure -> both are added.
    {
        FakeMem m(0xA000);
        const gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.name_list.empty() && v.map_list.empty());
        CHECK(gsv::plan_slot_files(v.names, v.mapped) == P::AddBoth);
        CHECK(!v.lobby && !v.save);
    }
    // The map lost the save (or never had it): the structures disagree -> refused, AddFile is not called.
    {
        FakeMem m = registered_slot();
        m.put<uint32_t>(kBuckets, 0);
        const gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.mapped.save == 0 && v.mapped.lobby == 1);
        CHECK(gsv::plan_slot_files(v.names, v.mapped) == P::Inconsistent);
    }
    // A map entry with no object does not count as a file.
    {
        FakeMem m = registered_slot();
        m.put<uintptr_t>(kSaveEntry + gsv::kEntryValue, 0);
        const gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.mapped.save == 0 && !v.save);
        CHECK(gsv::plan_slot_files(v.names, v.mapped) == P::Inconsistent);
    }
    // A name twice in the vector (AddFile called twice): refused.
    {
        FakeMem m = registered_slot();
        m.put<uint32_t>(kSlot + gsv::kSlotNameCount, 3);
        m.put<uintptr_t>(kVec + 16, kLobbyChars);
        const gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.names.lobby == 2);
        CHECK(gsv::plan_slot_files(v.names, v.mapped) == P::Duplicate);
    }
    // Case and length matter (the game compares with memcmp / strcmp): "Lobby", "lobbyX" and a name with no NUL in
    // its first 8 bytes are other files.
    {
        FakeMem m = registered_slot();
        m.put_str(kLobbyChars, "Lobby");
        gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.names.lobby == 0 && v.names.other == 1);
        m.put_str(kLobbyChars, "lobbyX");
        v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.names.other == 1);
        m.put_str(kLobbyChars, "lobbylobby");
        v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.names.other == 1 && v.name_list[0].length == 8);
        CHECK(gsv::describe_slot_files(v).find("'lobbylob...'") != std::string::npos);
        m.put<uint8_t>(kLobbyChars, 0x01);  // not printable: shown in hex
        v = gsv::read_slot_files(m, kSlot);
        CHECK(gsv::describe_slot_files(v).find(":016F") != std::string::npos);
    }
    // A name ending right at the end of readable memory is still read (byte by byte).
    {
        FakeMem m = registered_slot();
        const uintptr_t edge = FakeMem::kBase + m.size() - 5;  // "save" + NUL fill the last 5 bytes
        m.put_str(edge, "save");
        m.put<uintptr_t>(kVec + 8, edge);
        m.put<uintptr_t>(kSaveKey + gsv::kKeyChars, edge);
        const gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(v.ok && v.names.save == 1 && v.mapped.save == 1);
    }
    // Unreadable pieces: never a guess, the reason in the view (and in the log line).
    {
        FakeMem m = registered_slot();
        m.put<uintptr_t>(kVec + 8, 0x10);  // a name pointer outside the image
        gsv::SlotFilesView v = gsv::read_slot_files(m, kSlot);
        CHECK(!v.ok && std::string(v.error) == "a name is unreadable");
        CHECK(gsv::describe_slot_files(v).find("READ FAILED") != std::string::npos);
        m = registered_slot();
        m.put<uint32_t>(kSlot + gsv::kSlotNameCount, 1000);
        v = gsv::read_slot_files(m, kSlot);
        CHECK(!v.ok);
        m = registered_slot();
        m.put<uint32_t>(kBuckets, 40);  // more pairs than a 0x100-byte bucket holds
        v = gsv::read_slot_files(m, kSlot);
        CHECK(!v.ok && std::string(v.error) == "a bucket is unreadable or too full");
        m = registered_slot();
        m.put<uint32_t>(kBuckets + 12, 7);  // an index past the entries
        v = gsv::read_slot_files(m, kSlot);
        CHECK(!v.ok && std::string(v.error) == "a map entry is unreadable");
        CHECK(!gsv::read_slot_files(m, 0x10).ok);
    }
}

// ---------------------------------------------------------------- callers

void test_callers() {
    CHECK(gsv::load_caller(0x2C17886) == LoadCaller::UserLogin);
    CHECK(gsv::load_caller(0x2C3E1D3) == LoadCaller::Lobby3093);
    CHECK(gsv::load_caller(0x1042ABF8) == LoadCaller::Character3093);
    CHECK(gsv::load_caller(0x2C17881) == LoadCaller::Other);  // the call itself, not its return
    CHECK(gsv::load_caller(0) == LoadCaller::Other);
    CHECK(gsv::save_caller(0xCBD86D3) == gsv::SaveCaller::Autosave);
    CHECK(gsv::save_caller(0x103BE99F) == gsv::SaveCaller::Manual2910);
    CHECK(gsv::save_caller(0x186E450) == gsv::SaveCaller::Other);
    CHECK(std::string(gsv::flush_caller_name(0x28F273F)) == "logout phase");
    CHECK(std::string(gsv::flush_caller_name(0x108393CD)) == "flush timer");
    CHECK(std::string(gsv::flush_caller_name(1)) == "OTHER");
}

// ---------------------------------------------------------------- decisions

gsv::SlotState st(bool staged, bool live, bool disabled = false, bool poisoned = false, bool lobby_ok = false,
                  bool save_ok = false) {
    gsv::SlotState s;
    s.staged = staged;
    s.live = live;
    s.disabled = disabled;
    s.poisoned = poisoned;
    s.lobby_ok = lobby_ok;
    s.save_ok = save_ok;
    return s;
}

// Every state of the six bits.
std::vector<gsv::SlotState> all_states() {
    std::vector<gsv::SlotState> v;
    for (int b = 0; b < 64; ++b) {
        v.push_back(st((b & 1) != 0, (b & 2) != 0, (b & 4) != 0, (b & 8) != 0, (b & 16) != 0, (b & 32) != 0));
    }
    return v;
}

void test_slot_ready() {
    CHECK(!gsv::slot_ready(st(false, false)));
    CHECK(gsv::slot_ready(st(true, false)));
    CHECK(gsv::slot_ready(st(false, true)));
    CHECK(!gsv::slot_ready(st(true, true, true)));
    CHECK(!gsv::slot_ready(st(true, true, false, true)));
    CHECK(gsv::slot_healthy(st(false, true, false, false, true, true)));
    CHECK(!gsv::slot_healthy(st(true, false, false, false, true, true)));   // staged only: not loaded
    CHECK(!gsv::slot_healthy(st(false, true, false, false, true, false)));  // the character not verified
    CHECK(!gsv::slot_healthy(st(false, true, false, false, false, true)));
    CHECK(!gsv::slot_healthy(st(false, true, true, false, true, true)));
    CHECK(!gsv::slot_healthy(st(false, true, false, true, true, true)));
}

// Review items 1, 6, 7: whose a load or save is, from positive facts only.
void test_classify_owner() {
    using F = gsv::UserFacts;
    const auto with = [](auto set) {
        F f;
        f.any_guest_session = true;
        set(f);
        return gsv::classify_owner(f);
    };
    // Nothing known while a guest may exist: maybe user 2 (the old code said "not user 2" whenever server_user2 failed).
    CHECK(with([](F&) {}) == Owner::Unidentified);
    CHECK(with([](F& f) { f.is_user2 = true; }) == Owner::User2);
    CHECK(with([](F& f) { f.is_host = true; }) == Owner::Host);
    // server_user2 failing (a transient validation, or user 2 gone with an autosave of its character queued): its
    // user's session back pointer, or its character key, still says user 2.
    CHECK(with([](F& f) { f.session_known = true; f.session_is_guest = true; }) == Owner::User2);
    CHECK(with([](F& f) { f.character_is_user2 = true; }) == Owner::User2);
    // The host positively: its validated user, or a user on the game's own session.
    CHECK(with([](F& f) { f.session_known = true; f.session_is_host = true; }) == Owner::Host);
    // A session pointer that is neither: unknown.
    CHECK(with([](F& f) { f.session_known = true; }) == Owner::Unidentified);
    // session_is_* count only with a readable session.
    CHECK(with([](F& f) { f.session_is_host = true; }) == Owner::Unidentified);
    CHECK(with([](F& f) { f.session_is_guest = true; }) == Owner::Unidentified);
    // Any sign of user 2 wins (fail closed).
    CHECK(with([](F& f) { f.is_host = true; f.character_is_user2 = true; }) == Owner::User2);
    // No session 2 was ever built: nobody but the host can be in.
    F none;
    CHECK(gsv::classify_owner(none) == Owner::Host);
    none.character_is_user2 = true;
    CHECK(gsv::classify_owner(none) == Owner::User2);
}

// Review item 4: no verification outlives the load it was made for.
void test_verify_drop() {
    gsv::VerifyDrop d = gsv::drop_on_load_entry(true);
    CHECK(d.lobby && d.save);
    d = gsv::drop_on_load_entry(false);
    CHECK(!d.lobby && d.save);
    d = gsv::drop_on_load_result(true, 0);
    CHECK(d.lobby && d.save);
    d = gsv::drop_on_load_result(false, 0x1234);
    CHECK(d.lobby && d.save);
    d = gsv::drop_on_load_result(false, -1);
    CHECK(d.lobby && d.save);
    d = gsv::drop_on_load_result(false, 0);
    CHECK(!d.lobby && !d.save);
}

void test_decide_load() {
    const uint32_t g = gsv::kGuestSlot;
    const LoadCaller callers[] = {LoadCaller::UserLogin, LoadCaller::Lobby3093, LoadCaller::Character3093,
                                  LoadCaller::Other};
    const Owner owners[] = {Owner::Host, Owner::User2, Owner::Unidentified};
    const uint32_t asked_slots[] = {0, 1, 2, 3, 100, 108, 200, 204, 206, 207};
    // Off and probe never change anything.
    for (const Mode m : {Mode::Off, Mode::Probe}) {
        for (const auto& s : all_states()) {
            for (const Owner o : owners) {
                for (const LoadCaller c : callers) {
                    for (const uint32_t a : asked_slots) {
                        const gsv::Decision d = gsv::decide_load(m, o, c, s, a, g);
                        CHECK(d.action == Action::Pass && d.slot == a);
                    }
                }
            }
        }
    }
    // Mode 1, the properties.
    for (const auto& s : all_states()) {
        for (const Owner o : owners) {
            for (const LoadCaller c : callers) {
                for (const uint32_t a : asked_slots) {
                    const gsv::Decision d = gsv::decide_load(Mode::On, o, c, s, a, g);
                    // A remap only for user 2, only to the guest slot, only when it is ready.
                    if (d.action == Action::Remap) CHECK(o == Owner::User2 && d.slot == g && gsv::slot_ready(s));
                    if (d.action != Action::Remap) CHECK(d.slot == a);
                    // Never the host's slot for user 2, on ANY path (review item 2: its UserLogin too).
                    if (o == Owner::User2) CHECK(d.action != Action::Pass);
                    // Maybe user 2: never the host's slot except at UserLogin (its live user actor's session is read).
                    if (o == Owner::Unidentified && c != LoadCaller::UserLogin) CHECK(d.action == Action::Refuse);
                    // The guest slot is never loaded for anyone else.
                    if (o != Owner::User2 && a == g) CHECK(d.action == Action::Refuse);
                    // User 2 with a ready slot always gets it, whoever calls.
                    if (o == Owner::User2 && gsv::slot_ready(s)) CHECK(d.action == Action::Remap);
                    // The host's own loads are never refused (review item 7).
                    if (o == Owner::Host && a != g) CHECK(d.action == Action::Pass);
                    CHECK(d.why && *d.why);
                }
            }
        }
    }
    const auto on = [&](Owner o, LoadCaller c, const gsv::SlotState& s, uint32_t a = 1) {
        return gsv::decide_load(Mode::On, o, c, s, a, g);
    };
    CHECK(on(Owner::User2, LoadCaller::UserLogin, st(false, false)).action == Action::Refuse);
    CHECK(on(Owner::User2, LoadCaller::UserLogin, st(true, false)).action == Action::Remap);
    CHECK(on(Owner::User2, LoadCaller::Lobby3093, st(false, false)).action == Action::Refuse);
    CHECK(on(Owner::User2, LoadCaller::Character3093, st(false, false)).action == Action::Refuse);
    CHECK(on(Owner::User2, LoadCaller::Lobby3093, st(false, true)).action == Action::Remap);
    CHECK(on(Owner::User2, LoadCaller::Character3093, st(false, true, false, true)).action == Action::Refuse);
    CHECK(on(Owner::User2, LoadCaller::Character3093, st(true, true, true)).action == Action::Refuse);
    CHECK(on(Owner::User2, LoadCaller::UserLogin, st(true, true, true)).action == Action::Refuse);
    CHECK(on(Owner::Unidentified, LoadCaller::UserLogin, st(true, true)).action == Action::Pass);
    CHECK(on(Owner::Unidentified, LoadCaller::Lobby3093, st(true, true)).action == Action::Refuse);
    CHECK(on(Owner::Unidentified, LoadCaller::Character3093, st(false, false)).action == Action::Refuse);
    CHECK(on(Owner::Host, LoadCaller::Lobby3093, st(true, true), 204).action == Action::Refuse);
    CHECK(on(Owner::Host, LoadCaller::Lobby3093, st(true, true), 0).action == Action::Pass);
    CHECK(on(Owner::Host, LoadCaller::Character3093, st(false, false, true, true), 108).action == Action::Pass);
    CHECK(on(Owner::User2, LoadCaller::Lobby3093, st(true, false), 3).slot == g);  // CrossSave's 3: remapped too
}

void test_decide_save() {
    const uint32_t g = gsv::kGuestSlot;
    const Owner owners[] = {Owner::Host, Owner::User2, Owner::Unidentified};
    const uint32_t asked_slots[] = {0, 1, 2, 3, 100, 108, 204};
    for (const auto& s : all_states()) {
        for (const Owner o : owners) {
            for (const uint32_t a : asked_slots) {
                for (const bool same : {false, true}) {
                    for (const Mode m : {Mode::Off, Mode::Probe}) {
                        const gsv::Decision d = gsv::decide_save(m, o, s, same, a, g);
                        CHECK(d.action == Action::Pass && d.slot == a);
                    }
                    const gsv::Decision d = gsv::decide_save(Mode::On, o, s, same, a, g);
                    // Fail closed: user 2's save goes to its own slot or nowhere.
                    if (o == Owner::User2) CHECK(d.action != Action::Pass);
                    if (d.action == Action::Remap) {
                        CHECK(o == Owner::User2 && d.slot == g && gsv::slot_healthy(s) && same);
                    }
                    if (o == Owner::User2 && gsv::slot_healthy(s) && same) CHECK(d.action == Action::Remap);
                    // Another login's (or a departed user 2's) character never writes the guest slot.
                    if (!same) CHECK(d.action != Action::Remap);
                    if (o == Owner::Unidentified) CHECK(d.action == Action::Refuse);
                    // The host's own saves always pass (review item 6), the guest slot excepted.
                    if (o == Owner::Host) CHECK(d.action == (a == g ? Action::Refuse : Action::Pass));
                    CHECK(d.why && *d.why);
                }
            }
        }
    }
    const gsv::SlotState healthy = st(false, true, false, false, true, true);
    // CrossSave (3) is never reached by user 2: its 3 becomes the guest slot or the save is refused.
    CHECK(gsv::decide_save(Mode::On, Owner::User2, healthy, true, 3, g).slot == g);
    CHECK(gsv::decide_save(Mode::On, Owner::User2, st(false, false), true, 3, g).action == Action::Refuse);
    CHECK(gsv::decide_save(Mode::On, Owner::User2, healthy, false, 1, g).action == Action::Refuse);
    CHECK(gsv::decide_save(Mode::On, Owner::Host, healthy, false, 1, g).action == Action::Pass);
}


// ---------------------------------------------------------------- the mailbox

void test_generations() {
    CHECK(gsv::parse_generation(L"gen1") == 1u);
    CHECK(gsv::parse_generation(L"gen12") == 12u);
    CHECK(gsv::parse_generation(L"gen4294967295") == 4294967295u);
    CHECK(!gsv::parse_generation(L"gen4294967296"));
    CHECK(!gsv::parse_generation(L"gen"));
    CHECK(!gsv::parse_generation(L"gen0"));
    CHECK(!gsv::parse_generation(L"gen01"));
    CHECK(!gsv::parse_generation(L"gen1.partial"));
    CHECK(!gsv::parse_generation(L"Gen1"));
    CHECK(!gsv::parse_generation(L"gen-1"));
    CHECK(!gsv::parse_generation(L"generation1"));

    CHECK(gsv::next_generation({}) == 1);
    const uint32_t some[] = {3, 1, 7, 2};
    CHECK(gsv::next_generation(some) == 8);
    const uint32_t top[] = {0xFFFFFFFFu};
    CHECK(gsv::next_generation(top) == 0);

    const uint32_t seven[] = {7, 1, 3, 2, 6, 5, 4};
    const std::vector<uint32_t> drop = gsv::generations_to_drop(seven);
    CHECK(drop.size() == 2 && drop[0] == 1 && drop[1] == 2);
    const uint32_t five[] = {1, 2, 3, 4, 5};
    CHECK(gsv::generations_to_drop(five).empty());
    const uint32_t dup[] = {1, 1, 2, 3, 4, 5, 6};
    const std::vector<uint32_t> d2 = gsv::generations_to_drop(dup);
    CHECK(d2.size() == 1 && d2[0] == 1);
    CHECK(gsv::generations_to_drop(seven, 0).size() == 7);
}

void test_names_and_manifest() {
    CHECK(gsv::guest_folder_name("76561198000000000") == "76561198000000000");
    CHECK(gsv::guest_folder_name("local-debug") == "local-debug");
    CHECK(gsv::guest_folder_name("") == "unknown");
    CHECK(gsv::guest_folder_name("..\\x/y:z") == "___x_y_z");
    CHECK(gsv::guest_folder_name(std::string(100, 'a')).size() == 64);

    const uint8_t bytes[] = {0x00, 0x0F, 0xA5, 0xFF};
    CHECK(gsv::hex(bytes) == "000fa5ff");

    gsv::Manifest m;
    m.generation = 3;
    m.guest = "local-debug";
    m.reason = "autosave (ret exe+0xCBD86D3)";
    m.host_slot = 204;
    m.asked_slot = 1;
    m.time_utc = "2026-10-12T03:00:00Z";
    gsv::HeaderInfo h;
    h.version = 2;
    h.raw_size = 539;
    h.data_size = 390;
    m.files = {{"lobby.save", 518, std::string(64, 'a'), h}, {"save.save", 736731, std::string(64, 'b'), h}};
    const std::string t = gsv::manifest_text(m);
    CHECK(t.find("generation=3\n") != std::string::npos);
    CHECK(t.find("guest=local-debug\n") != std::string::npos);
    CHECK(t.find("reason=autosave (ret exe+0xCBD86D3)\n") != std::string::npos);
    CHECK(t.find("host_slot=204\n") != std::string::npos);
    CHECK(t.find("asked_slot=1\n") != std::string::npos);
    CHECK(t.find("file=lobby.save size=518 sha256=" + std::string(64, 'a')) != std::string::npos);
    CHECK(t.find("file=save.save size=736731 sha256=" + std::string(64, 'b') + " version=2 raw=539 data=390\n") !=
          std::string::npos);
}

}  // namespace

int main() {
    test_parse_mode();
    test_slots();
    test_header();
    test_template_files();
    test_slot_files();
    test_slot_layout();
    test_callers();
    test_slot_ready();
    test_classify_owner();
    test_verify_drop();
    test_decide_load();
    test_decide_save();
    test_generations();
    test_names_and_manifest();
    if (g_failures) {
        std::printf("%d check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("optd guest save: all checks passed\n");
    return 0;
}
