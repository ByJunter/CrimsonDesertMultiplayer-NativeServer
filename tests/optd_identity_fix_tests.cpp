// Option D, C2: the identity fix's pure logic (src/mod/session2/identity_fix.cpp)
// on fake game memory: the lobby list edit (incl. the ambiguous cases), the
// write through Status+0x70 (a pointer to the u64), the invariant, the
// uniqueness scan and the admission of user 2's logins. The hooks need the game.

#include <cstdio>
#include <cstring>
#include <vector>

#include "mod/session2/identity_fix.h"
#include "mod/session2/identity_probe.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game); here no user 2 exists.
bool server_users(uintptr_t&, uintptr_t&) { return false; }
bool server_host_user(uintptr_t&) { return false; }
bool server_user2(uintptr_t&) { return false; }
bool server_user2_present() { return false; }
uint32_t server_thread_id() { return 0; }
uint32_t server_epoch() { return 1; }
}  // namespace cdmp::session2

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

constexpr uint64_t kNative = idf::kNative;
constexpr uint64_t kF002 = idf::kUser2;

template <class T>
void put(uint8_t* bytes, uint32_t off, T v) {
    std::memcpy(bytes + off, &v, sizeof v);
}
template <class T>
T get(const uint8_t* bytes, uint32_t off) {
    T v{};
    std::memcpy(&v, bytes + off, sizeof v);
    return v;
}
uintptr_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

// A lobby component: [comp+8] = its user, [comp+0x18] = {entry* data; u32 count; u32 cap}.
struct FakeLobby {
    alignas(8) uint8_t comp[0x40] = {};
    alignas(8) uint8_t list[0x10] = {};
    std::vector<uint64_t> storage;  // 8-aligned entries of 0xD0 bytes
    explicit FakeLobby(std::vector<uint64_t> numbers) { set(numbers); }
    void set(const std::vector<uint64_t>& numbers) {
        storage.assign(numbers.size() * idf::kEntryStride / 8 + 1, 0);
        for (size_t i = 0; i < numbers.size(); ++i) {
            uint8_t* e = entry(i);
            put(e, idf::kEntryNumber, numbers[i]);
            // A recognisable tail: the edit must touch only the u64 at +0.
            for (uint32_t b = 8; b < idf::kEntryStride; ++b) e[b] = static_cast<uint8_t>(0xA0 + i);
        }
        put(list, 0, addr(storage.data()));
        put(list, idf::kListCount, static_cast<uint32_t>(numbers.size()));
        put(comp, idf::kLobbyList, addr(list));
    }
    uint8_t* entry(size_t i) { return reinterpret_cast<uint8_t*>(storage.data()) + i * idf::kEntryStride; }
    uint64_t number(size_t i) { return get<uint64_t>(entry(i), idf::kEntryNumber); }
    bool tail_intact(size_t i) {
        for (uint32_t b = 8; b < idf::kEntryStride; ++b) {
            if (entry(i)[b] != static_cast<uint8_t>(0xA0 + i)) return false;
        }
        return true;
    }
    uintptr_t a() { return addr(comp); }
};

// A character: actor -> table -> Status -> u64* number.
const uint64_t g_status_vt_block[4] = {};
const uintptr_t kStatusVt = addr(g_status_vt_block);
struct FakeCharacter {
    alignas(8) uint8_t actor[0x100] = {};
    alignas(8) uint8_t table[0x40] = {};
    alignas(8) uint8_t status[0x100] = {};
    uint64_t number = kNative;
    explicit FakeCharacter(uint32_t key) {
        put(actor, idf::kActorKey, key);
        put(actor, idf::kUserComponents, addr(table));
        put(table, idf::kTableStatus, addr(status));
        put(status, 0, kStatusVt);
        put(status, idf::kStatusActor, addr(actor));
        put(status, idf::kStatusNumber, addr(&number));
    }
    uintptr_t a() { return addr(actor); }
};

void test_allowed() {
    CHECK(identity_number_allowed(kF002));
    CHECK(identity_number_allowed(0x7FFFFFFFFFFFF003));
    CHECK(!identity_number_allowed(kNative));
    CHECK(!identity_number_allowed(~0ull));
    CHECK(!identity_number_allowed(0));
    CHECK(!identity_number_allowed(0x7FFFFFFFFFFFF001));
}

void test_plan() {
    const std::vector<uint64_t> one{kNative};
    LobbyPlan p = plan_lobby(one, kF002);
    CHECK(p.what == LobbyEdit::Write && p.index == 0 && p.count == 1 && p.native == 1 && p.assigned == 0);
    // The constant in the second entry, another character's number in the first.
    const std::vector<uint64_t> second{0x1234, kNative};
    p = plan_lobby(second, kF002);
    CHECK(p.what == LobbyEdit::Write && p.index == 1);
    const std::vector<uint64_t> done{kF002};
    p = plan_lobby(done, kF002);
    CHECK(p.what == LobbyEdit::Already && p.index == 0);
    CHECK(plan_lobby({}, kF002).what == LobbyEdit::Empty);
    // Ambiguous: two with the constant, none, or the constant next to the assigned number.
    const std::vector<uint64_t> two{kNative, kNative};
    p = plan_lobby(two, kF002);
    CHECK(p.what == LobbyEdit::Ambiguous && p.native == 2);
    const std::vector<uint64_t> none{0x11, 0x22};
    CHECK(plan_lobby(none, kF002).what == LobbyEdit::Ambiguous);
    const std::vector<uint64_t> both{kF002, kNative};
    p = plan_lobby(both, kF002);
    CHECK(p.what == LobbyEdit::Ambiguous && p.native == 1 && p.assigned == 1);
    const std::vector<uint64_t> dup{kF002, kF002};
    CHECK(plan_lobby(dup, kF002).what == LobbyEdit::Ambiguous);
    // Never one of the forbidden numbers.
    CHECK(plan_lobby(one, kNative).what == LobbyEdit::NotAllowed);
    CHECK(plan_lobby(one, ~0ull).what == LobbyEdit::NotAllowed);
    CHECK(plan_lobby(one, 0).what == LobbyEdit::NotAllowed);
    CHECK(plan_lobby(one, 0x7FFFFFFFFFFFF001).what == LobbyEdit::NotAllowed);

    // The lobby's number for the invariant.
    CHECK(lobby_identity(one, kF002) == std::optional<uint64_t>(kNative));
    CHECK(lobby_identity(done, kF002) == std::optional<uint64_t>(kF002));
    const std::vector<uint64_t> other_and_done{0x1234, kF002};
    CHECK(lobby_identity(other_and_done, kF002) == std::optional<uint64_t>(kF002));
    CHECK(!lobby_identity(both, kF002));
    CHECK(!lobby_identity(two, kF002));
    CHECK(!lobby_identity({}, kF002));
}

void test_lobby_memory() {
    // One entry with the constant: written, nothing else touched; a second load: Already.
    FakeLobby l({kNative});
    std::vector<uint64_t> numbers;
    uintptr_t data = 0;
    CHECK(read_lobby(l.a(), numbers, data) && numbers.size() == 1 && numbers[0] == kNative &&
          data == addr(l.storage.data()));
    LobbyPlan p = lobby_normalise(l.a(), kF002);
    CHECK(p.what == LobbyEdit::Write && p.index == 0);
    CHECK(l.number(0) == kF002 && l.tail_intact(0));
    CHECK(get<uint32_t>(l.list, idf::kListCount) == 1);
    p = lobby_normalise(l.a(), kF002);
    CHECK(p.what == LobbyEdit::Already);
    CHECK(l.number(0) == kF002);

    // Three entries, the constant in the middle: only that one changes.
    FakeLobby l3({0x1111, kNative, 0x3333});
    p = lobby_normalise(l3.a(), kF002);
    CHECK(p.what == LobbyEdit::Write && p.index == 1 && p.count == 3);
    CHECK(l3.number(0) == 0x1111 && l3.number(1) == kF002 && l3.number(2) == 0x3333);
    CHECK(l3.tail_intact(0) && l3.tail_intact(1) && l3.tail_intact(2));

    // Ambiguous lobbies are left exactly as they were.
    FakeLobby amb({kNative, kNative});
    CHECK(lobby_normalise(amb.a(), kF002).what == LobbyEdit::Ambiguous);
    CHECK(amb.number(0) == kNative && amb.number(1) == kNative);
    FakeLobby mixed({kF002, kNative});
    CHECK(lobby_normalise(mixed.a(), kF002).what == LobbyEdit::Ambiguous);
    CHECK(mixed.number(0) == kF002 && mixed.number(1) == kNative);
    FakeLobby empty({});
    CHECK(lobby_normalise(empty.a(), kF002).what == LobbyEdit::Empty);

    // Unreadable: no list, a count beyond the bound, data null with entries.
    FakeLobby bad({kNative});
    put(bad.comp, idf::kLobbyList, uintptr_t{0});
    CHECK(lobby_normalise(bad.a(), kF002).what == LobbyEdit::Unreadable);
    CHECK(!read_lobby(bad.a(), numbers, data));
    FakeLobby big({kNative});
    put(big.list, idf::kListCount, idf::kMaxEntries + 1);
    CHECK(lobby_normalise(big.a(), kF002).what == LobbyEdit::Unreadable);
    CHECK(big.number(0) == kNative);
    FakeLobby nodata({kNative});
    put(nodata.list, 0, uintptr_t{0});
    CHECK(lobby_normalise(nodata.a(), kF002).what == LobbyEdit::Unreadable);
    CHECK(lobby_normalise(0, kF002).what == LobbyEdit::Unreadable);
    CHECK(lobby_normalise(0x1000, kF002).what == LobbyEdit::Unreadable);  // never mapped

    // user -> [[user+0x68]+0x10] = lobby, only with the back link [comp+8] == user.
    alignas(8) uint8_t user[0x100] = {};
    alignas(8) uint8_t table[0x40] = {};
    put(user, idf::kUserComponents, addr(table));
    put(table, idf::kTableLobby, l.a());
    CHECK(user_lobby(addr(user)) == 0);  // back link not set
    put(l.comp, idf::kLobbyOwner, addr(user));
    CHECK(user_lobby(addr(user)) == l.a());
    put(l.comp, idf::kLobbyOwner, addr(table));
    CHECK(user_lobby(addr(user)) == 0);
    CHECK(user_lobby(0) == 0);
}

void test_status() {
    CHECK(identity_status_number_offset_for_build(false) == idf::kStatusNumber);
    CHECK(identity_status_number_offset_for_build(true) == idf::kStatusNumber3527);
    FakeCharacter c(0xA010052C);
    uint64_t before = 0;
    const uintptr_t pointer_before = get<uintptr_t>(c.status, idf::kStatusNumber);
    CHECK(status_normalise(c.a(), kStatusVt, kF002, before) == StatusEdit::Written);
    CHECK(before == kNative && c.number == kF002);
    // Status+0x70 stays the same pointer: the u64 it points to changed.
    CHECK(get<uintptr_t>(c.status, idf::kStatusNumber) == pointer_before);
    CHECK(status_normalise(c.a(), kStatusVt, kF002, before) == StatusEdit::Already && before == kF002);

    // Build 3527: the identity pointer moved to +0x90. The old +0x70 is now
    // an unrelated scalar and +0x98 begins adjacent inline name storage.
    FakeCharacter current(0xA010052D);
    constexpr uint64_t old70 = 0x00000000FFFFFF06ull;
    constexpr uint8_t name98[] = {0x53, 0x32, 0x2D, 0x4E, 0x41, 0x4D, 0x45, 0x00,
                                  0xA5, 0x5A, 0xC3, 0x3C, 0x11, 0x22, 0x33, 0x44};
    put(current.status, idf::kStatusNumber, old70);
    put(current.status, idf::kStatusNumber3527, addr(&current.number));
    std::memcpy(current.status + 0x98, name98, sizeof name98);
    CHECK(status_normalise_at(current.a(), kStatusVt, kF002, before, idf::kStatusNumber3527) ==
          StatusEdit::Written);
    CHECK(before == kNative && current.number == kF002);
    CHECK(get<uint64_t>(current.status, idf::kStatusNumber) == old70);
    CHECK(get<uintptr_t>(current.status, idf::kStatusNumber3527) == addr(&current.number));
    CHECK(std::memcmp(current.status + 0x98, name98, sizeof name98) == 0);

    // Only the native constant is replaced (a child's -1, anything else: untouched).
    FakeCharacter child(0xA0100099);
    child.number = ~0ull;
    CHECK(status_normalise(child.a(), kStatusVt, kF002, before) == StatusEdit::NotNative && before == ~0ull);
    CHECK(child.number == ~0ull);

    // Wrong vtable, broken back link, null pointer, unaligned pointer: nothing written.
    FakeCharacter v(0xA0100002);
    put(v.status, 0, uintptr_t{0x1234});
    CHECK(status_normalise(v.a(), kStatusVt, kF002, before) == StatusEdit::Unreadable && v.number == kNative);
    CHECK(status_normalise(v.a(), 0, kF002, before) == StatusEdit::Written);  // vtable not checked
    FakeCharacter back(0xA0100003);
    put(back.status, idf::kStatusActor, uintptr_t{0});
    CHECK(status_normalise(back.a(), kStatusVt, kF002, before) == StatusEdit::Unreadable && back.number == kNative);
    FakeCharacter nul(0xA0100004);
    put(nul.status, idf::kStatusNumber, uintptr_t{0});
    CHECK(status_normalise(nul.a(), kStatusVt, kF002, before) == StatusEdit::Unreadable);
    FakeCharacter odd(0xA0100005);
    alignas(8) uint8_t raw[24] = {};
    put(raw, 1, kNative);
    put(odd.status, idf::kStatusNumber, addr(raw + 1));
    CHECK(status_normalise(odd.a(), kStatusVt, kF002, before) == StatusEdit::Unreadable);
    CHECK(get<uint64_t>(raw, 1) == kNative);
    CHECK(status_normalise(0, kStatusVt, kF002, before) == StatusEdit::Unreadable);
    CHECK(status_normalise_at(c.a(), kStatusVt, kF002, before, 0x88) == StatusEdit::Unreadable);
    FakeCharacter host(0xA0100001);
    CHECK(status_normalise(host.a(), kStatusVt, kNative, before) == StatusEdit::NotAllowed && host.number == kNative);
}

void test_invariant() {
    std::string why;
    CHECK(identity_invariant(kF002, kF002, kF002, why) && why.empty());
    CHECK(!identity_invariant(kNative, kF002, kF002, why) && why.find("lobby 0x7FFFFFFFFFFFFFFF") != std::string::npos);
    CHECK(!identity_invariant(kF002, kNative, kF002, why) && why.find("Status 0x7FFFFFFFFFFFFFFF") != std::string::npos);
    CHECK(!identity_invariant(std::nullopt, kF002, kF002, why) && why.find("lobby unreadable") != std::string::npos);
    CHECK(!identity_invariant(kF002, std::nullopt, kF002, why) && why.find("Status unreadable") != std::string::npos);
    CHECK(!identity_invariant(kNative, kNative, kF002, why) && why.find("lobby") != std::string::npos &&
          why.find("Status") != std::string::npos);
    // Equal to each other but not to the assigned number: still a failure.
    CHECK(!identity_invariant(0x7FFFFFFFFFFFF003, 0x7FFFFFFFFFFFF003, kF002, why));

    // The invariant as before_phases reads it: lobby list + Status chain.
    FakeLobby l({kNative});
    FakeCharacter c(0xA010052C);
    std::vector<uint64_t> numbers;
    uintptr_t data = 0;
    CHECK(read_lobby(l.a(), numbers, data));
    CHECK(!identity_invariant(lobby_identity(numbers, kF002), c.number, kF002, why));
    CHECK(lobby_normalise(l.a(), kF002).what == LobbyEdit::Write);
    uint64_t before = 0;
    CHECK(status_normalise(c.a(), kStatusVt, kF002, before) == StatusEdit::Written);
    CHECK(read_lobby(l.a(), numbers, data));
    CHECK(identity_invariant(lobby_identity(numbers, kF002), c.number, kF002, why));
}

void test_unique() {
    FakeCharacter host(0xA0100001), mine(0xA010052C), stale(0xA0100500);
    FakeCharacter no_status(0xB0100002);
    put(no_status.table, idf::kTableStatus, uintptr_t{0});
    std::vector<uintptr_t> list{host.a(), 0, mine.a(), no_status.a()};
    CHECK(actors_with_number(addr(list.data()), 4, kF002, 0) == std::optional<uint32_t>(0u));
    CHECK(actors_with_number(addr(list.data()), 4, kNative, 0) == std::optional<uint32_t>(2u));
    mine.number = kF002;
    CHECK(actors_with_number(addr(list.data()), 4, kF002, 0) == std::optional<uint32_t>(1u));
    CHECK(actors_with_number(addr(list.data()), 4, kF002, mine.a()) == std::optional<uint32_t>(0u));
    // An old character still carrying the number is found.
    stale.number = kF002;
    list.push_back(stale.a());
    CHECK(actors_with_number(addr(list.data()), 5, kF002, mine.a()) == std::optional<uint32_t>(1u));
    CHECK(actors_with_number(addr(list.data()), 0, kF002, 0) == std::optional<uint32_t>(0u));
    CHECK(!actors_with_number(addr(list.data()), idf::kMaxActors + 1, kF002, 0));
    CHECK(!actors_with_number(0, 3, kF002, 0));
    CHECK(!actors_with_number(0x1000, 3, kF002, 0));
}

void test_admit() {
    std::string why;
    const uint8_t req3093[13] = {4, 0x02, 0xF0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 108, 0, 0, 0};
    // Not wanted: everything passes.
    CHECK(identity_fix_admit(3093, req3093, why) && why.empty());
    CHECK(identity_fix_admit(2490, {}, why));
    // Wanted but not installed: the two logins are refused, the rest (and the 2001) pass.
    identity_fix_set_wanted(true);
    CHECK(identity_fix_wanted() && !identity_fix_active());
    CHECK(!identity_fix_admit(2490, {}, why) && why.find("not active") != std::string::npos);
    CHECK(!identity_fix_admit(3093, req3093, why));
    CHECK(identity_fix_admit(2001, {}, why));
    CHECK(identity_fix_admit(2840, {}, why));
    std::string logout;
    CHECK(!identity_fix_take_logout(logout));
    CHECK(identity_fix_describe().find("WANTED BUT OFF") != std::string::npos);
    identity_fix_set_wanted(false);
}

}  // namespace

int main() {
    test_allowed();
    test_plan();
    test_lobby_memory();
    test_status();
    test_invariant();
    test_unique();
    test_admit();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_identity_fix: all tests passed\n");
    return 0;
}
