// S43 (docs/research/s25/s43-puertas-y-r42.md): scene_level_share's pure logic (scene_level_share_logic.h), with no
// game: the parsers on bytes of the captures (p8-2inst-s42-probe, p8-2inst-pose-anfitrion), the per-user loaded sets,
// the owner gate's decision (A), the delete's decision (B) and the kept book.
#include "mod/session2/scene_level_share_logic.h"

#include <cstdio>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace sls = cdmp::session2::sls;
namespace gsl = cdmp::session2::gsl;

namespace {

int failures = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                             \
        }                                                           \
    } while (0)

std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (; s[0] && s[1]; s += 2) out.push_back(static_cast<uint8_t>(nib(s[0]) * 16 + nib(s[1])));
    return out;
}

// Real frames (whole Reqs, 5-byte header), from the captures named.
// cdmp_packets.guest_20261015-155255.bin t=4458.501: the guest's first 2598 of the tavern's level.
const char* k2598Tavern = "260a000800010000 00aa527bde";
// ... t=4430.611: one of the four 2598 with u32 = 3 at the game's load.
const char* k2598Start = "260a000800030000 00ec635e2a";
// ... t=4502.479: the guest's 2834 of the tavern's level (type 0, flag 1).
const char* k2834Tavern = "120b000a00010000 0000aa527bde01";
// cdmp_packets.host_20261015-155239.bin t=4416.494: a host 2834 with flag 0.
const char* k2834Flag0 = "120b000a00010000 0000cca4ef2100";
// cdmp_packets.host_20261015-083504.bin t=69.252 (offset 22161498): the host's Resync after its 2283/2437, 86 hashes.
const char* k2104 =
    "3808005901046dd0f604402d6a98c9269a8b9292fe28ec635e2af4ba161bf4036a058057a8c92599f12bd8fd1f923089109c79bc812976ad"
    "0240cfe54abea8266c1fc76fc8f7a7efbd817fea585d0aa73a510c980254d56b848767dc319e0eeea03a40ce62705bcc5fba01d97d3dd00f"
    "4fb2c1fef74997dd8150682b215ed7ce899629fe1e0fb6a587633171853b5cc484fee86e935da5d6a72e9147969d1fe852f3553e85bf45cd"
    "b9cef2b3f64dcdda76a4985e69d4e098e7b7041fcac8d41da27e50c7590614b361f7b40822edd9b37f9ea661825df4cbad730fa9e7c96047"
    "5195eead33be1e546a78fb91cfdb2f6aea401e15da4f8439081144babdadab6f52b3640ffb7c15c43ebe3ccfd0bfc7233351d6ab2d4f63f1"
    "bbb38b0867a9639773e30099588aaa527bdea8980bcdce740982ad52875463a63a3173c445306a183f208adec22b86a627a666a70bbc0c2d"
    "24dc405ffd309ddf2ffe1108f7a8";

std::vector<uint8_t> clean(const char* s) {
    std::string t;
    for (; *s; ++s)
        if (*s != ' ') t.push_back(*s);
    return hex(t.c_str());
}

sls::Packet frame(const std::vector<uint8_t>& f) { return sls::parse_frame(f); }

std::vector<uint8_t> make(uint16_t op, std::vector<uint8_t> payload) {
    std::vector<uint8_t> f{static_cast<uint8_t>(op), static_cast<uint8_t>(op >> 8), 0,
                           static_cast<uint8_t>(payload.size()), static_cast<uint8_t>(payload.size() >> 8)};
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

void test_mode() {
    CHECK(sls::parse_mode("").mode == sls::Mode::Off && sls::parse_mode("").known);
    CHECK(sls::parse_mode("0").mode == sls::Mode::Off);
    CHECK(sls::parse_mode("probe").mode == sls::Mode::Probe);
    CHECK(sls::parse_mode(" 1 ").mode == sls::Mode::On);
    CHECK(!sls::parse_mode("2").known && sls::parse_mode("2").mode == sls::Mode::Off);
    CHECK(!sls::parse_mode("on").known);
}

void test_parsers() {
    auto p = frame(clean(k2598Tavern));
    CHECK(p.kind == sls::Kind::Load && p.hash == 0xDE7B52AA);
    p = frame(clean(k2598Start));
    CHECK(p.kind == sls::Kind::Load && p.hash == 0x2A5E63EC);
    p = frame(clean(k2834Tavern));
    CHECK(p.kind == sls::Kind::Unload && p.type == 0 && p.hash == 0xDE7B52AA);
    p = frame(clean(k2834Flag0));
    CHECK(p.kind == sls::Kind::Unload && p.type == 0 && p.hash == 0x21EFA4CC);
    const auto rs = hex(k2104);
    CHECK(rs.size() == 350);
    p = frame(rs);
    CHECK(p.kind == sls::Kind::Resync && p.count == 86);
    bool tavern = false;
    for (uint32_t i = 0; i < p.count; ++i) tavern |= sls::le32(p.list.data() + i * 4) == 0xDE7B52AA;
    CHECK(tavern);
    CHECK(sls::le32(p.list.data()) == 0x04F6D06D);
    // The handlers' own rejections are Bad, not tracked.
    CHECK(frame(make(2598, {1, 0, 0, 0, 0xAA, 0x52, 0x7B})).kind == sls::Kind::Bad);         // 7 bytes
    CHECK(frame(make(2598, {1, 0, 0, 0, 0, 0, 0, 0})).kind == sls::Kind::Bad);               // hash 0
    CHECK(frame(make(2598, {0, 0, 0, 0, 0xAA, 0x52, 0x7B, 0xDE})).kind == sls::Kind::Bad);   // u32 0
    CHECK(frame(make(2834, {1, 0, 0, 0, 4, 0xAA, 0x52, 0x7B, 0xDE, 1})).kind == sls::Kind::Bad);  // type 4
    CHECK(frame(make(2834, {1, 0, 0, 0, 1, 0xAA, 0x52, 0x7B, 0xDE, 1})).type == 1);
    CHECK(frame(make(2104, {4, 1, 2, 3})).kind == sls::Kind::Bad);  // 1 + 3
    CHECK(frame(make(2104, {4})).kind == sls::Kind::Resync && frame(make(2104, {4})).count == 0);
    auto broken = clean(k2598Tavern);
    broken.pop_back();  // the header's size no longer matches
    CHECK(frame(broken).kind == sls::Kind::Bad);
    for (const uint16_t op : {uint16_t{2283}, uint16_t{2362}, uint16_t{2437}, uint16_t{2001}})
        CHECK(frame(make(op, {1, 2, 3})).kind == sls::Kind::Flush);
    CHECK(frame(make(2928, {4, 1, 0, 0, 0})).kind == sls::Kind::None);
    CHECK(!sls::relevant(2928) && sls::relevant(2104));
}

void test_hashes() {
    const std::string names[] = {"a/b/c.ext", "noext", "dir\\x.y.z", "Level_Tavern", "x/", ".hidden"};
    for (const std::string& n : names) {
        const auto h = sls::name_hashes(reinterpret_cast<const uint8_t*>(n.data()), n.size());
        CHECK(h.raw == gsl::hashlittle(reinterpret_cast<const uint8_t*>(n.data()), n.size(), gsl::kNameHashInit));
        CHECK(h.base == gsl::schedule_key_of_name(n));
    }
    sls::HashCheck c;
    CHECK(c.usable() == sls::Variant::None);  // nothing proven yet: B never keeps
    c.record({0x11, 0x22}, 0x11);
    CHECK(c.usable() == sls::Variant::Raw);
    c.record({0x33, 0x44}, 0x44);  // raw misses once: raw is out; base matched once but missed before
    CHECK(c.usable() == sls::Variant::None);
    sls::HashCheck b;
    b.record({0x10, 0x20}, 0x20);
    CHECK(b.usable() == sls::Variant::Base);
    CHECK(sls::HashCheck::pick(sls::Variant::Base, {0x10, 0x20}) == 0x20);
    CHECK(sls::HashCheck::pick(sls::Variant::None, {0x10, 0x20}) == 0);
}

void test_set() {
    sls::HashSet<64> s;
    CHECK(!s.add(0) && !s.add(1));
    CHECK(s.add(5) && s.add(5) && s.size() == 1);
    CHECK(s.contains(5) == sls::HashSet<64>::Look::Yes && s.contains(6) == sls::HashSet<64>::Look::No);
    CHECK(s.remove(5) && !s.remove(5) && s.size() == 0);
    // Random operations against std::set (the backward-shift deletion keeps every chain reachable).
    std::mt19937 rng(43);
    std::set<uint32_t> ref;
    for (int i = 0; i < 20000; ++i) {
        const uint32_t h = 2 + rng() % 70;
        if (rng() % 3) {
            const bool added = s.add(h);
            if (added) ref.insert(h);
            else CHECK(s.overflow() && ref.size() >= sls::HashSet<64>::kMax);
        } else {
            CHECK(s.remove(h) == (ref.erase(h) == 1));
        }
        if (i % 97 == 0) {
            for (uint32_t k = 2; k < 72; ++k)
                CHECK((s.contains(k) == sls::HashSet<64>::Look::Yes) == (ref.count(k) == 1));
            CHECK(s.size() == ref.size());
        }
    }
    s.clear();
    CHECK(s.size() == 0 && !s.overflow() && s.contains(10) == sls::HashSet<64>::Look::No);
    for (uint32_t k = 0; k < sls::HashSet<64>::kMax; ++k) CHECK(s.add(1000 + k));
    CHECK(!s.add(5) && s.overflow());
}

using Tr = sls::Tracker<1024>;
sls::Packet load(uint32_t h) { return {sls::Kind::Load, 0, h}; }
sls::Packet unload(uint32_t h, uint8_t type = 0) { return {sls::Kind::Unload, type, h}; }
sls::Packet flushp() { return {sls::Kind::Flush}; }

void test_tracker() {
    static Tr t;
    using U = sls::User;
    CHECK(t.apply(U::User2, load(0xDE7B52AA)) == sls::Applied::Added);
    CHECK(t.apply(U::User2, load(0xDE7B52AA)) == sls::Applied::AlreadyIn);
    CHECK(t.has(U::User2, 0xDE7B52AA) == sls::Has::Yes && t.has(U::Host, 0xDE7B52AA) == sls::Has::No);
    CHECK(t.apply(U::User2, unload(0xDE7B52AA, 1)) == sls::Applied::OtherType);  // type 1: not this registry
    CHECK(t.has(U::User2, 0xDE7B52AA) == sls::Has::Yes);
    CHECK(t.apply(U::User2, unload(0xDE7B52AA)) == sls::Applied::Removed);
    CHECK(t.apply(U::User2, unload(0xDE7B52AA)) == sls::Applied::NotIn);
    CHECK(t.has(U::User2, 0xDE7B52AA) == sls::Has::No);
    // Emptied at its load (2283 / 2362 / 2437 / 2001).
    t.apply(U::User2, load(7));
    t.apply(U::User2, load(8));
    CHECK(t.apply(U::User2, frame(make(2437, {0}))) == sls::Applied::Flushed);
    CHECK(t.size(U::User2) == 0 && t.has(U::User2, 7) == sls::Has::No);
    // The Resync list replaces the set (the stale ones go).
    t.apply(U::Host, load(9));
    CHECK(t.apply(U::Host, frame(hex(k2104))) == sls::Applied::Replaced);
    CHECK(t.size(U::Host) == 86 && t.has(U::Host, 9) == sls::Has::No && t.has(U::Host, 0xDE7B52AA) == sls::Has::Yes);
    // A flush asked from another thread: at once No; the next packet of that user starts from empty.
    t.flush(U::Host);
    CHECK(t.has(U::Host, 0xDE7B52AA) == sls::Has::No && t.size(U::Host) == 0);
    CHECK(t.apply(U::Host, {sls::Kind::None}) == sls::Applied::Ignored);  // the request is honoured first
    CHECK(t.has(U::Host, 0xDE7B52AA) == sls::Has::No && t.size(U::Host) == 0);
    CHECK(t.apply(U::Host, load(11)) == sls::Applied::Added);
    CHECK(t.size(U::Host) == 1 && t.has(U::Host, 0xDE7B52AA) == sls::Has::No);
    // A lost update: Unknown until that user's next flush / Resync.
    t.invalidate(U::Host);
    CHECK(t.has(U::Host, 11) == sls::Has::Unknown && !t.valid(U::Host));
    t.apply(U::Host, load(12));
    CHECK(t.has(U::Host, 12) == sls::Has::Unknown);
    t.apply(U::Host, flushp());
    CHECK(t.valid(U::Host) && t.has(U::Host, 12) == sls::Has::No);
    // Overflow: the set stops being trusted.
    for (uint32_t k = 0; k < 768; ++k) t.apply(U::Host, load(100 + k));
    CHECK(t.apply(U::Host, load(5000)) == sls::Applied::Overflow);
    CHECK(t.has(U::Host, 100) == sls::Has::Unknown);
    t.apply(U::Host, flushp());
    CHECK(t.has(U::Host, 100) == sls::Has::No && t.valid(U::Host));
    CHECK(t.apply(U::Host, {sls::Kind::Bad}) == sls::Applied::Bad);
}

sls::GateIn gate_base(sls::Mode m) {
    sls::GateIn g;
    g.mode = m;
    g.keys_live = true;
    g.host_user = 0x90100000;
    g.user2_user = 0x90100001;
    g.owner = 0x90100000;
    g.eax = 0x90100001;
    g.field_ok = true;
    g.hooked = true;
    g.subject = 0x1234;
    g.actor = 0x1234;
    g.opcode = sls::kOpLoad;
    return g;
}

void test_gate() {
    using W = sls::GateWhy;
    CHECK(sls::decide_gate(gate_base(sls::Mode::Probe)) == W::WouldReady);
    CHECK(sls::decide_gate(gate_base(sls::Mode::On)) == W::Ready);
    auto g = gate_base(sls::Mode::On);
    g.opcode = sls::kOpResync;
    CHECK(sls::decide_gate(g) == W::Ready);
    g = gate_base(sls::Mode::On);
    g.eax = g.owner;  // the host's own load: native
    CHECK(sls::decide_gate(g) == W::Owner);
    g = gate_base(sls::Mode::On);
    g.keys_live = false;
    CHECK(sls::decide_gate(g) == W::NoKeys);
    g = gate_base(sls::Mode::On);
    g.user2_user = g.host_user;
    CHECK(sls::decide_gate(g) == W::NoKeys);
    g = gate_base(sls::Mode::On);
    g.owner = 0x90100005;  // a field the host does not own
    CHECK(sls::decide_gate(g) == W::NotHostField);
    g = gate_base(sls::Mode::On);
    g.field_ok = false;
    CHECK(sls::decide_gate(g) == W::NotHostField);
    g = gate_base(sls::Mode::On);
    g.eax = 0x90100009;
    CHECK(sls::decide_gate(g) == W::NotUser2);
    g = gate_base(sls::Mode::On);
    g.hooked = false;
    CHECK(sls::decide_gate(g) == W::NoSubjectHook);
    g = gate_base(sls::Mode::On);
    g.subject = 0;
    CHECK(sls::decide_gate(g) == W::NoTroc);
    g = gate_base(sls::Mode::On);
    g.actor = 0x9999;
    CHECK(sls::decide_gate(g) == W::OtherSubject);
    g = gate_base(sls::Mode::On);
    g.opcode = 2928;
    CHECK(sls::decide_gate(g) == W::WrongOpcode);
}

sls::DeleteIn del_base(sls::Mode m, sls::Who provoker, sls::Has host, sls::Has user2) {
    sls::DeleteIn d;
    d.mode = m;
    d.caller = sls::Caller::Unload2834;
    d.type_ok = true;
    d.type = 0;
    d.field_host = true;
    d.provoker = provoker;
    d.name_ok = true;
    d.packet_hash_ok = true;
    d.hash_matches = true;
    d.variant = sls::Variant::Raw;
    d.host = host;
    d.user2 = user2;
    d.ready = sls::Has::Yes;
    return d;
}

void test_delete() {
    using V = sls::Verdict;
    using W = sls::Why;
    using H = sls::Has;
    using P = sls::Who;
    // The guest unloads, the host still has it: kept (1) / would keep (probe).
    auto o = sls::decide_delete(del_base(sls::Mode::On, P::User2, H::Yes, H::No));
    CHECK(o.verdict == V::Keep && o.why == W::Kept && o.holders == 1);
    o = sls::decide_delete(del_base(sls::Mode::Probe, P::User2, H::Yes, H::No));
    CHECK(o.verdict == V::WouldKeep && o.why == W::Probe);
    // The host unloads, the guest still has it.
    o = sls::decide_delete(del_base(sls::Mode::On, P::Host, H::No, H::Yes));
    CHECK(o.verdict == V::Keep && o.holders == 2);
    // The last user: nobody else has it -> the native delete.
    o = sls::decide_delete(del_base(sls::Mode::On, P::User2, H::No, H::No));
    CHECK(o.verdict == V::Delete && o.why == W::NoHolder);
    // The provoker's own set never counts (a reload right after its unload).
    o = sls::decide_delete(del_base(sls::Mode::On, P::User2, H::No, H::Yes));
    CHECK(o.verdict == V::Delete && o.why == W::NoHolder);
    o = sls::decide_delete(del_base(sls::Mode::On, P::Host, H::Yes, H::No));
    CHECK(o.verdict == V::Delete);
    // An untrusted set of the other user: delete.
    o = sls::decide_delete(del_base(sls::Mode::On, P::User2, H::Unknown, H::No));
    CHECK(o.verdict == V::Delete && o.why == W::HolderUnknown);
    // Every missing datum: delete.
    auto d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.mode = sls::Mode::Off;
    CHECK(sls::decide_delete(d).why == W::Off);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.caller = sls::Caller::Other;
    CHECK(sls::decide_delete(d).why == W::OtherCaller);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.type = 1;
    CHECK(sls::decide_delete(d).why == W::NotType0);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.type_ok = false;
    CHECK(sls::decide_delete(d).why == W::NotType0);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.field_host = false;
    CHECK(sls::decide_delete(d).why == W::NotHostField);
    d = del_base(sls::Mode::On, P::Unknown, H::Yes, H::Yes);
    CHECK(sls::decide_delete(d).why == W::ProvokerUnknown);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.name_ok = false;
    CHECK(sls::decide_delete(d).why == W::NameUnreadable);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.packet_hash_ok = false;
    CHECK(sls::decide_delete(d).why == W::NoPacketHash);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.variant = sls::Variant::None;  // hash not verified -> delete
    CHECK(sls::decide_delete(d).verdict == V::Delete && sls::decide_delete(d).why == W::HashUnverified);
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::Yes);
    d.hash_matches = false;
    CHECK(sls::decide_delete(d).why == W::HashMismatch);
    // P2-4: a 2834 is kept only when its entry is ready; not ready or unreadable -> the native delete (holders kept).
    d = del_base(sls::Mode::On, P::User2, H::Yes, H::No);
    d.ready = H::No;
    o = sls::decide_delete(d);
    CHECK(o.verdict == V::Delete && o.why == W::NotReady && o.holders == 1);
    d.ready = H::Unknown;
    o = sls::decide_delete(d);
    CHECK(o.verdict == V::Delete && o.why == W::ReadyUnknown);
    d.mode = sls::Mode::Probe;  // probe says what mode 1 would do
    CHECK(sls::decide_delete(d).why == W::ReadyUnknown);
    d.ready = H::Yes;
    CHECK(sls::decide_delete(d).verdict == V::WouldKeep);
    // The ready byte is asked only once everything else holds: a missing holder still says so.
    d = del_base(sls::Mode::On, P::User2, H::No, H::No);
    d.ready = H::Unknown;
    CHECK(sls::decide_delete(d).why == W::NoHolder);
    // 0x2836110 (the owner's load) and the loader: never skipped, only observed.
    d = del_base(sls::Mode::On, P::Host, H::No, H::Yes);
    d.caller = sls::Caller::OwnerLoad;
    d.packet_hash_ok = false;
    d.hash_matches = false;
    d.ready = H::Unknown;  // not read for these callers
    o = sls::decide_delete(d);
    CHECK(o.verdict == V::WouldKeep && o.why == W::ObserveOnly);
    d.mode = sls::Mode::Probe;
    CHECK(sls::decide_delete(d).why == W::Probe);
    d = del_base(sls::Mode::On, P::Nobody, H::Yes, H::No);
    d.caller = sls::Caller::Loader;
    o = sls::decide_delete(d);
    CHECK(o.verdict == V::WouldKeep && o.holders == 1);
    CHECK(sls::skip_allowed(sls::Caller::Unload2834) && !sls::skip_allowed(sls::Caller::OwnerLoad) &&
          !sls::skip_allowed(sls::Caller::Loader) && !sls::skip_allowed(sls::Caller::Other));
}

void test_callers() {
    CHECK(sls::caller_of(0x29C3049) == sls::Caller::Unload2834);
    CHECK(sls::caller_of(0x28363D4) == sls::Caller::OwnerLoad);
    CHECK(sls::caller_of(0x2876460) == sls::Caller::Loader);
    CHECK(sls::caller_of(0x2877145) == sls::Caller::Other && sls::caller_of(0) == sls::Caller::Other);
    using P = sls::Who;
    CHECK(sls::provoker_of(sls::Caller::Unload2834, true, true, sls::kOpUnload) == P::User2);
    CHECK(sls::provoker_of(sls::Caller::Unload2834, true, true, 2598) == P::Unknown);
    CHECK(sls::provoker_of(sls::Caller::Unload2834, true, false, 0) == P::Host);
    CHECK(sls::provoker_of(sls::Caller::Unload2834, false, false, 0) == P::Unknown);
    CHECK(sls::provoker_of(sls::Caller::OwnerLoad, true, false, 0) == P::Host);
    CHECK(sls::provoker_of(sls::Caller::OwnerLoad, true, true, 2437) == P::User2);
    CHECK(sls::provoker_of(sls::Caller::Loader, false, false, 0) == P::Nobody);
    CHECK(sls::provoker_of(sls::Caller::Other, true, false, 0) == P::Unknown);
    // The 2834 handler's frame: name at C + 0x140, type at C + 0x98.
    CHECK(sls::frame_matches(0x1000, 0x1140, 0x1098));
    CHECK(!sls::frame_matches(0x1000, 0x1148, 0x1098) && !sls::frame_matches(0, 0x140, 0x98));
    const uint8_t ok[5] = {0x44, 0x88, 0x4C, 0x24, 0x20};
    CHECK(sls::delete_patch_ok(ok));
    CHECK(!sls::delete_patch_ok(std::span<const uint8_t>(ok, 4)));
    std::vector<uint8_t> fallback(ok, ok + 5);
    fallback.resize(14);
    CHECK(!sls::delete_patch_ok(fallback));
    CHECK(sls::kGateSig[sls::kGateSigPatchAt] == 0x41 && sls::kGateSig[sls::kGateSigPatchAt + 1] == 0x39 &&
          sls::kGateSig[sls::kGateSigPatchAt + 7] == 0x0F);
}

// The tavern, end to end on the pure pieces: the guest arrives first, then the host; one leaves; the other leaves.
void test_scenario() {
    static Tr t;
    sls::HashCheck hc;
    using U = sls::User;
    using V = sls::Verdict;
    const uint32_t tavern = 0xDE7B52AA;
    auto decide = [&](sls::Who provoker, uint32_t name_hash, uint32_t packet_hash) {
        sls::DeleteIn d;
        d.mode = sls::Mode::On;
        d.caller = sls::Caller::Unload2834;
        d.type_ok = true;
        d.field_host = true;
        d.provoker = provoker;
        d.name_ok = true;
        d.packet_hash_ok = true;
        hc.record({name_hash, 0x55555555}, packet_hash);
        d.variant = hc.usable();
        d.hash_matches = sls::HashCheck::pick(d.variant, {name_hash, 0x55555555}) == packet_hash;
        d.host = t.has(U::Host, packet_hash);
        d.user2 = t.has(U::User2, packet_hash);
        d.ready = sls::Has::Yes;  // A made it ready
        return sls::decide_delete(d);
    };
    t.apply(U::User2, frame(clean(k2598Tavern)));  // (a) the guest first
    t.apply(U::Host, frame(clean(k2598Tavern)));   // (b) the host arrives
    // (c) the guest leaves: its 2834 reaches OnRecvPacket first (its set loses it), then the handler's delete.
    t.apply(U::User2, frame(clean(k2834Tavern)));
    auto o = decide(sls::Who::User2, tavern, tavern);
    CHECK(o.verdict == V::Keep && o.holders == 1);
    // The guest comes back and the host leaves: kept for the guest.
    t.apply(U::User2, frame(clean(k2598Tavern)));
    t.apply(U::Host, unload(tavern));
    CHECK(decide(sls::Who::Host, tavern, tavern).verdict == V::Keep);
    // The guest loads elsewhere (2437 empties its set) and the host's late 2834 of the level comes: the native delete.
    t.apply(U::Host, load(tavern));
    t.apply(U::User2, frame(make(2437, {0})));
    t.apply(U::Host, unload(tavern));
    CHECK(decide(sls::Who::Host, tavern, tavern).verdict == V::Delete);
    // The last user's own 2834 deletes natively.
    t.apply(U::Host, load(tavern));
    t.apply(U::Host, unload(tavern));
    o = decide(sls::Who::Host, tavern, tavern);
    CHECK(o.verdict == V::Delete && o.why == sls::Why::NoHolder);
    // A name whose hash is not the packet's: the rule is out for the process; nothing is kept any more.
    t.apply(U::Host, load(tavern));
    t.apply(U::User2, load(tavern));
    t.apply(U::User2, unload(tavern));
    o = decide(sls::Who::User2, 0x12345678, tavern);
    CHECK(o.verdict == V::Delete);
    CHECK(decide(sls::Who::User2, tavern, tavern).verdict == V::Delete);
}

void test_kept() {
    sls::KeptBook k;
    k.kept(1, 10);
    k.kept(1, 20);
    k.kept(2, 30);
    CHECK(k.entries().size() == 2 && k.entries()[0].times == 2 && k.entries()[0].last_us == 20);
    CHECK(k.deleted(1) && !k.deleted(1) && k.entries().size() == 1);
    for (uint32_t i = 0; i < sls::kKeptMax + 3; ++i) k.kept(100 + i, i);
    CHECK(k.entries().size() == sls::kKeptMax && k.forgotten() == 4);
}

}  // namespace

int main() {
    test_mode();
    test_parsers();
    test_hashes();
    test_set();
    test_tracker();
    test_gate();
    test_delete();
    test_callers();
    test_scenario();
    test_kept();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("optd_scene_level_share: all passed\n");
    return 0;
}
