// Independent native3527 byte fixtures: offsets are from sequential stream
// reader evidence, intentionally not computed through movement::Layout.
#include "common/movement_layout.h"
#include "common/troc_frame.h"
#include "mod/session2/attach_follow_logic.h"
#include "mod/session2/horse_follow_logic.h"
#include "mod/session2/ride_filter_logic.h"
#include "mod/session2/travel_probe_logic.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace cdmp;
namespace afl = session2::afl;
namespace hfl = session2::hfl;
namespace rfl = session2::rfl;
namespace tpl = session2::tpl;
constexpr auto current = movement::Build::v3527;
// 3576 keeps 3527's wire payloads (the Troc catalogue did not change).
static_assert(movement::layout(movement::Build::v3576) == &movement::k3527);
static_assert(movement::payload_size(movement::Build::v3576, 2556) == 518);
int failures = 0;
#define CHECK(v) do { if (!(v)) { std::printf("FAIL line %d: %s\n", __LINE__, #v); ++failures; } } while (false)
template<class T> void put(std::vector<uint8_t>& p, size_t at, T value) { std::memcpy(p.data()+at, &value, sizeof value); }
template<class T> T get(const std::vector<uint8_t>& p, size_t at) { T v{}; std::memcpy(&v,p.data()+at,sizeof v); return v; }

std::vector<uint8_t> fixture_move(size_t n) {
    std::vector<uint8_t> p(n, 0x5A);
    put<uint32_t>(p,0,0xA0101001);
    put<uint64_t>(p,68,0xB0102002);
    put<float>(p,76,100.f); put<float>(p,80,200.f); put<float>(p,84,300.f);
    put<float>(p,88,0.f); put<float>(p,92,0.f); put<float>(p,96,0.f); put<float>(p,100,1.f);
    put<float>(p,180,0.f); put<float>(p,184,0.f); put<float>(p,188,0.f); put<float>(p,192,1.f);
    put<float>(p,196,10.f); put<float>(p,200,20.f); put<float>(p,204,30.f);
    put<uint64_t>(p,212,0x0102030405060708ULL);
    put<uint32_t>(p,220,0xDEADBEEF);
    p[224]=0x81; p[225]=0x43;
    return p;
}

void movement_copies() {
    auto native = fixture_move(239);
    auto packet = troc::make(2697,-1,native);
    const auto m = hfl::read_move(native,hfl::kAckSize,current);
    CHECK(m.shape==hfl::Shape::Ridden && m.parent==0xB0102002 && m.ts==0x0102030405060708ULL);
    CHECK(hfl::read_move(native,hfl::kAckSize).shape==hfl::Shape::NotMove);
    std::vector<uint8_t> horse;
    CHECK(hfl::make_horse_ack(packet,horse,current));
    CHECK(horse.size()==244 && get<uint16_t>(horse,3)==239);
    CHECK(get<uint32_t>(horse,5)==0xB0102002);
    CHECK(get<float>(horse,201)==100.f && get<float>(horse,9)==100.f);
    CHECK(get<uint64_t>(horse,73)==~uint64_t{0});
    CHECK(get<uint32_t>(horse,225)==0xDEADBEEF);
    CHECK(std::equal(packet.begin()+217,packet.end(),horse.begin()+217)); // timestamp, opaqueu32, flags, tail
    CHECK(std::equal(packet.begin()+145,packet.begin()+185,horse.begin()+145)); // secondaryTransform unchanged
    std::vector<uint8_t> untouched(244,0xCD);
    const auto before=untouched;
    CHECK(!hfl::make_horse_ack_into(packet,untouched.data(),243,current));
    CHECK(untouched==before);
    auto req=fixture_move(518);
    CHECK(hfl::make_horse_req(req,horse,current));
    CHECK(horse.size()==518 && get<uint32_t>(horse,220)==0xDEADBEEF);
    CHECK(std::equal(req.begin()+212,req.begin()+238,horse.begin()+212));
    CHECK(std::all_of(horse.begin()+238,horse.end(),[](uint8_t b){return b==0;}));

    const auto am=afl::read_move(native,afl::kMoveAckSize,current);
    CHECK(am.ok && am.pose.p.x==10.f && am.pose.p.y==20.f && am.pose.p.z==30.f);
    const afl::Pose pose{{7.f,8.f,9.f},{0.f,0.f,0.f,1.f}};
    CHECK(afl::make_child_ack(packet,0xB0103003,pose,horse,current));
    CHECK(get<float>(horse,201)==7.f && get<float>(horse,9)==7.f);
    CHECK(get<uint32_t>(horse,225)==0xDEADBEEF);
    CHECK(std::equal(packet.begin()+217,packet.end(),horse.begin()+217));
}

void actions() {
    auto move=fixture_move(239);
    std::vector<uint8_t> action(729,0xA7);
    put<uint32_t>(action,0,0xA0101001);
    put<uint32_t>(action,8,0x91605616); put<uint32_t>(action,77,0);
    // Ack block B starts143; MoveData starts atMoveActor+4 and has222bytes.
    std::copy(move.begin()+4,move.begin()+226,action.begin()+143);
    put<uint32_t>(action,369,0xB0109999);
    const auto a=rfl::read_ack(action.data(),action.size(),current);
    CHECK(a.ok && a.parent==0xB0102002 && a.target==0xB0109999);
    CHECK(!rfl::read_ack(action.data(),action.size()).ok);
    auto whole=troc::make(2991,-1,action), before=whole;
    const auto out=rfl::filter_packet(rfl::Mode::On,whole.data(),whole.size(),0xA0101001,0,current);
    CHECK(out.start_action && out.remote && out.rewritten);
    CHECK(std::equal(before.begin()+147,before.end(),whole.begin()+147)); // flag+entireB remains native
    CHECK(get<uint32_t>(whole,364)==0xDEADBEEF);
    const auto parsed=afl::read_action(action,true,current);
    CHECK(parsed.ok && parsed.world.p.x==10.f && parsed.ts==0x0102030405060708ULL);
    CHECK(parsed.parent==0xB0102002 && parsed.target==0xB0109999);
    // Req shares A blocks, but B precedes its final flag.
    std::vector<uint8_t> req=action;
    std::copy(action.begin()+143,action.end(),req.begin()+142);
    req[728]=0xCF;
    const auto ra=afl::read_action(req,false,current);
    CHECK(ra.ok && ra.world.p.x==10.f && ra.ts==parsed.ts && ra.target==parsed.target);
    CHECK(movement::position_offset(current,2223)==334 && movement::position_offset(current,2991)==335);
}

void field_owner() {
    for (uint16_t op : {uint16_t{2174},uint16_t{2643}}) {
        const size_t head=op==2174?17:20;
        std::vector<uint8_t> payload(head+56*2,0x6B);
        put<uint32_t>(payload,9,0x90000001);
        auto packet=troc::make(op,-1,payload), before=packet;
        uint32_t old=0;
        CHECK(tpl::alias_owner(packet,5,op,0x90000002,true,&old,current)==tpl::AliasOut::Rewrite);
        CHECK(old==0x90000001 && get<uint32_t>(packet,14)==0x90000002);
        CHECK(std::equal(before.begin(),before.begin()+14,packet.begin()));
        CHECK(std::equal(before.begin()+18,before.end(),packet.begin()+18)); // unknown record tails unchanged
        if(op==2643) {
            const auto v=tpl::parse_loading_field_data(payload,payload.size(),current);
            CHECK(v && v->records==2 && v->owner==0x90000001);
        }
        packet.pop_back(); const auto malformed=packet;
        CHECK(tpl::alias_owner(packet,5,op,0x90000002,true,nullptr,current)==tpl::AliasOut::BadLayout);
        CHECK(packet==malformed);
        CHECK(!tpl::owner_layout_ok(op,head+48,current));
    }
}
int main() {
    movement_copies(); actions(); field_owner();
    std::puts(failures?"Native movement tests FAILED":"Native movement tests passed");
    return failures?1:0;
}
