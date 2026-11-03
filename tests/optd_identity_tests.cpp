// Option D, C2-0: the identity probe's pure logic (src/mod/session2/identity_probe.cpp)
// on fake game memory: the actor -> Status -> characterNo chain, the Status ->
// actor back link and the user-2 predicate. The hooks themselves need the game.

#include <cstdio>
#include <cstring>

#include "mod/session2/identity_probe.h"

namespace cdmp::session2 {
// The real ones are in server.cpp (need the game); the logic tested here does not use them.
bool server_users(uintptr_t&, uintptr_t&) { return false; }
bool server_host_user(uintptr_t&) { return false; }
bool server_user2(uintptr_t&) { return false; }
bool server_user2_present() { return false; }
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

constexpr uint64_t kProvisional = 0x7FFFFFFFFFFFFFFF;
// A stand-in for ServerUserActor's vtable: only its address is compared.
const uint64_t g_user_vtable_block[4] = {};
const uintptr_t kUserVt = reinterpret_cast<uintptr_t>(g_user_vtable_block);
const uint64_t g_actor_vtable_block[4] = {};

template <class T>
void put(uint8_t* bytes, uint32_t off, T v) {
    std::memcpy(bytes + off, &v, sizeof v);
}

struct FakeUser {
    alignas(8) uint8_t bytes[0x220] = {};
    explicit FakeUser(uint32_t key) {
        put(bytes, 0, kUserVt);
        put(bytes, idp::kKey, key);
    }
    uintptr_t addr() { return reinterpret_cast<uintptr_t>(bytes); }
};

// A character: actor, its component table and its Status with the u64 it points to.
struct FakeCharacter {
    alignas(8) uint8_t actor[0x100] = {};
    alignas(8) uint8_t table[0x40] = {};
    alignas(8) uint8_t status[0x100] = {};
    uint64_t number = kProvisional;
    explicit FakeCharacter(uint32_t key) {
        put(actor, 0, reinterpret_cast<uintptr_t>(g_actor_vtable_block));
        put(actor, idp::kKey, key);
        put(actor, idp::kActorComponents, reinterpret_cast<uintptr_t>(table));
        put(table, idp::kTableStatus, reinterpret_cast<uintptr_t>(status));
        put(status, idp::kComponentOwner, reinterpret_cast<uintptr_t>(actor));
        put(status, idp::kStatusCharacterNo, reinterpret_cast<uintptr_t>(&number));
    }
    void set_user(FakeUser* u) { put(actor, idp::kActorUser, u ? u->addr() : uintptr_t{0}); }
    uintptr_t addr() { return reinterpret_cast<uintptr_t>(actor); }
    uintptr_t status_addr() { return reinterpret_cast<uintptr_t>(status); }
};

void test_character_no() {
    FakeCharacter c(0xA010052C);
    uint64_t no = 0;
    CHECK(identity_character_no(c.addr(), no) && no == kProvisional);
    // The chain follows the pointer each time: a normalised value is seen at once.
    c.number = 0x7FFFFFFFFFFFF002;
    CHECK(identity_character_no(c.addr(), no) && no == 0x7FFFFFFFFFFFF002);
    // Any null link: false, nothing read.
    no = 1;
    put(c.status, idp::kStatusCharacterNo, uintptr_t{0});
    CHECK(!identity_character_no(c.addr(), no) && no == 1);
    put(c.status, idp::kStatusCharacterNo, reinterpret_cast<uintptr_t>(&c.number));
    put(c.table, idp::kTableStatus, uintptr_t{0});
    CHECK(!identity_character_no(c.addr(), no));
    put(c.table, idp::kTableStatus, reinterpret_cast<uintptr_t>(c.status));
    put(c.actor, idp::kActorComponents, uintptr_t{0});
    CHECK(!identity_character_no(c.addr(), no));
    CHECK(!identity_character_no(0, no));
    // An unreadable address (never mapped below 64 KB).
    CHECK(!identity_character_no(0x1000, no));
}

void test_status_actor() {
    FakeCharacter a(0xA0100001), b(0xA010052C);
    CHECK(identity_status_actor(a.status_addr()) == a.addr());
    CHECK(identity_status_actor(b.status_addr()) == b.addr());
    // A component whose owner's Status is another one: not taken for that actor's Status.
    put(b.status, idp::kComponentOwner, a.addr());
    CHECK(identity_status_actor(b.status_addr()) == 0);
    put(b.status, idp::kComponentOwner, uintptr_t{0});
    CHECK(identity_status_actor(b.status_addr()) == 0);
    CHECK(identity_status_actor(0) == 0);
}

void test_who() {
    constexpr uint32_t kHostKey = 0x90100000, kUser2Key = 0x9010052B;
    FakeUser host(kHostKey), user2(kUser2Key), other(0x90100002);
    FakeCharacter c_host(0xA0100001), c_user2(0xA010052C), c_other(0xA0100003), c_none(0xA0100004);
    c_host.set_user(&host);
    c_user2.set_user(&user2);
    c_other.set_user(&other);
    c_none.set_user(nullptr);
    const IdUsers both{host.addr(), kHostKey, user2.addr()};
    bool self = true;

    // Session 2's validated user known: exact matches only.
    CHECK(identity_who(c_user2.addr(), both, kUserVt, self) == IdWho::User2 && !self);
    CHECK(identity_who(c_host.addr(), both, kUserVt, self) == IdWho::Host && !self);
    CHECK(identity_who(c_other.addr(), both, kUserVt, self) == IdWho::Other);  // not "anyone but the host"
    CHECK(identity_who(c_none.addr(), both, kUserVt, self) == IdWho::Other);   // user link not set
    CHECK(identity_who(0, both, kUserVt, self) == IdWho::Other);
    // The user actor itself (its lobby load).
    CHECK(identity_who(user2.addr(), both, kUserVt, self) == IdWho::User2 && self);
    CHECK(identity_who(host.addr(), both, kUserVt, self) == IdWho::Host && self);

    // Session 2's user not readable yet: a user whose key is not the host's.
    const IdUsers no_user2{host.addr(), kHostKey, 0};
    CHECK(identity_who(c_user2.addr(), no_user2, kUserVt, self) == IdWho::User2ByKey && !self);
    CHECK(identity_who(c_other.addr(), no_user2, kUserVt, self) == IdWho::User2ByKey);
    CHECK(identity_who(user2.addr(), no_user2, kUserVt, self) == IdWho::User2ByKey && self);
    CHECK(identity_who(c_host.addr(), no_user2, kUserVt, self) == IdWho::Host);
    // The host's user pointer not readable, its key cached: still the host by key.
    const IdUsers key_only{0, kHostKey, 0};
    CHECK(identity_who(c_host.addr(), key_only, kUserVt, self) == IdWho::Host);
    CHECK(identity_who(c_user2.addr(), key_only, kUserVt, self) == IdWho::User2ByKey);
    // Nothing known about the host: nobody is user 2 by key.
    const IdUsers nothing{};
    CHECK(identity_who(c_user2.addr(), nothing, kUserVt, self) == IdWho::Other);
    CHECK(identity_who(c_host.addr(), nothing, kUserVt, self) == IdWho::Other);
    // A "user" without ServerUserActor's vtable is never taken by key.
    FakeUser fake(kUser2Key);
    put(fake.bytes, 0, uintptr_t{0x1234});
    FakeCharacter c_fake(0xA0100005);
    c_fake.set_user(&fake);
    CHECK(identity_who(c_fake.addr(), no_user2, kUserVt, self) == IdWho::Other);

    // identity_owner: the actor itself for a user actor, else its +0xA0.
    CHECK(identity_owner(c_user2.addr(), kUserVt, self) == user2.addr() && !self);
    CHECK(identity_owner(user2.addr(), kUserVt, self) == user2.addr() && self);
    CHECK(identity_owner(c_none.addr(), kUserVt, self) == 0);
}

}  // namespace

int main() {
    test_character_no();
    test_status_actor();
    test_who();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_identity: all tests passed\n");
    return 0;
}
