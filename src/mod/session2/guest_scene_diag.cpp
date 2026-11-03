#include "mod/session2/guest_scene_diag.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/guest_stage_diag_logic.h"
#include "mod/session2/observer_stage_logic.h"
#include "mod/session2/offsets_s2.h"
#include "mod/session2/p8_probe_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/scene_diag_logic.h"
#include "mod/session2/scene_points.h"

namespace cdmp::session2 {
namespace {

namespace p = p8p;

// G3: the client's actor lookup 0x8AE1A0(mgr = [[WorldSystem]+0x30], out, key) -> ClientActorContainer::Find
// 0x464290 on [mgr+8] (gad::container_find replays it), then [actor+0x5E] & 0x10 (a character).
constexpr uint32_t kWorld_ActorMgr = 0x30, kActorMgr_Container = 0x08, kActor_Flags5E = 0x5E;
constexpr uint32_t kComps_Seq = 0xA8, kSeqComp_Mgr = 0x18;
constexpr uint32_t kMgr_Stages = 0x148, kStages_Data = 0x18, kStages_Count = 0x20, kMgr_Count = 0x174,
                   kMgr_Ready = 0x231;
constexpr uintptr_t kVtStageManager = 0x55C9720;  // pa::ClientSequencerStageManager (RTTI)
constexpr uint32_t kHostCharacter = 0xA0100001;   // the host's first character on its server (every session so far)

// ---------------------------------------------------------------- events

enum Kind : uint16_t { kKG1 = 0, kKDelivered, kKReq, kKindCount };
constexpr const char* kKindName[kKindCount] = {"G1 AI block 0x206DE6B", "delivered", "G4 C>S"};

constexpr uint64_t kWindowUs = 5'000'000;
constexpr uint32_t kPerWindow = 1024;
constexpr uint32_t kFirstPerNpc = 2;
constexpr uint64_t kMaxLinesPerKind = 3000;

struct KindStats {
    std::atomic<uint64_t> hits{0}, queued{0}, limited{0}, dropped{0}, over_budget{0};
};

uintptr_t g_base = 0;
std::vector<gad::Section> g_secs;
std::atomic<bool> g_started{false}, g_on{false};
p::EventRing<4096> g_ring;
KindStats g_stats[kKindCount];
p::WindowLimiter g_lim[kKindCount] = {{kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}, {kPerWindow, kWindowUs}};
std::atomic<uint64_t> g_lines[kKindCount] = {};
std::atomic<uint32_t> g_hooked{0}, g_wanted{0};
SafetyHookMid g_hooks[2];
p::KeyCounter<8192> g_keys_g1;
// G4
std::atomic<uint64_t> g_req_counts[scd::kSceneReqCount] = {};
std::atomic<uint64_t> g_req_2223_npc{0}, g_req_2223_own{0};
scd::CountTable<2048, 1> g_req_pairs;  // (opcode << 48 | scene or actor) -> count

struct PendingAi {
    uintptr_t x1 = 0;
    uint32_t resource = 0, node = 0, index = 0;
    uint64_t h0 = 0;
    uintptr_t file = 0;
};
thread_local PendingAi t_ai;

// ---------------------------------------------------------------- hot path (no CRT, no allocation)

template <class T>
bool rd(uintptr_t a, T& v) noexcept {
    if (a < 0x10000) return false;
    __try {
        v = *reinterpret_cast<const volatile T*>(a);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <class T>
T rdv(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return rd(a, v) ? v : fallback;
}

uintptr_t rdp(uintptr_t a) noexcept { return rdv<uintptr_t>(a); }

uint32_t key_of(uintptr_t actor) noexcept { return actor ? rdv<uint32_t>(actor + scp::kActor_Key) : 0; }

void emit(Kind k, p::Event& e) noexcept {
    e.kind = k;
    if (!e.qpc_us) e.qpc_us = p8_now_us();
    e.tid = GetCurrentThreadId();
    if (!g_lim[k].take(e.qpc_us)) {
        g_stats[k].limited.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (g_ring.push(e)) {
        g_stats[k].queued.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_stats[k].dropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void on_ai_read(safetyhook::Context& ctx) noexcept {
    PendingAi a;
    a.x1 = ctx.r14;
    rd(ctx.rbp + 0x50, a.resource);
    rd(ctx.rbp + 0x54, a.node);
    rd(ctx.rax, a.h0);
    rd(ctx.rax + scp::kHandle_Index, a.index);
    rd(ctx.rax + scp::kHandle_File, a.file);
    t_ai = a;
}

void on_ai_done(safetyhook::Context& ctx) noexcept {
    g_stats[kKG1].hits.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t ai = ctx.rdi, actor = rdp(ai + scp::kComp_Actor), x1 = rdp(ai + scp::kAi_Scene);
    const uint32_t npc = key_of(actor);
    const PendingAi a = t_ai;
    t_ai = PendingAi{};
    const bool pending = a.x1 && a.x1 == x1;
    const uint32_t nth = g_keys_g1.bump(npc ? npc : 1u);
    if (!p::within_key_budget(nth, kFirstPerNpc)) {
        g_stats[kKG1].over_budget.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uintptr_t h = rdp(x1 + scp::kScene_Handle);
    const uintptr_t comps = actor ? rdp(actor + scp::kActor_Comps) : 0;
    const uintptr_t pd0 = comps ? rdp(comps + scp::kComps_PlayData) : 0;
    const uintptr_t pd = pd0 && rdp(pd0) == g_base + scp::kVtPlayData ? pd0 : 0;
    p::Event e;
    e.v[0] = npc | static_cast<uint64_t>(pd ? rdv<uint32_t>(pd + scp::kPlayData_Owner) : 0) << 32;
    e.v[1] = rdv<uint64_t>(x1);
    e.v[2] = pending ? (a.resource | static_cast<uint64_t>(a.node) << 32) : 0;
    e.v[3] = h ? rdv<uint64_t>(h) : 0;
    e.v[4] = (h ? rdv<uint32_t>(h + scp::kHandle_Index, 0xFFFFFFFFu) : 0xFFFFFFFFu) |
             static_cast<uint64_t>(ctx.rax & 0xFF) << 32 | static_cast<uint64_t>(pending) << 40 |
             static_cast<uint64_t>(pd != 0) << 41 | static_cast<uint64_t>(rdv<uint8_t>(x1 + scp::kScene_Flag)) << 48;
    e.v[5] = h ? rdp(h + scp::kHandle_File) : 0;
    e.v[6] = pd ? rdv<uint64_t>(pd + scp::kPlayData_Key) : 0;
    e.v[7] = actor;
    e.v[8] = nth;
    emit(kKG1, e);
}

// ---------------------------------------------------------------- drain (the guest's diagnostics thread)

scd::NpcBook g_book;                              // drain only
scd::SceneLedger g_received;                      // what reached this client of the scene family (drain only)
std::map<uint32_t, uint64_t> g_npc_delivered;     // NPC key -> its first 2005 handed to the game (drain only)
std::set<uint32_t> g_characters;                  // G3 candidates (drain only)
std::map<uint32_t, scd::Lookup> g_g2_last;        // resource -> last G2 outcome (drain only)
std::map<uint32_t, scd::XSnapshot> g_g3_last;     // character -> last X snapshot (drain only)
uint64_t g_ticks = 0;                             // drain only
uint64_t g_delivered_2005_npc = 0, g_delivered_scene = 0;  // drain only

void write_line(Kind k, const std::string& s) {
    if (g_lines[k].fetch_add(1) + 1 > kMaxLinesPerKind) return;
    try {
        log::info("{}", s);
    } catch (...) {
    }
}

std::string head(Kind k, const p::Event& e) {
    return std::format("GuestScene [{}] {} T{} |", kKindName[k], p8_when(e.qpc_us), e.tid);
}

void on_g1(const p::Event& e) {
    const uint32_t npc = static_cast<uint32_t>(e.v[0]), owner = static_cast<uint32_t>(e.v[0] >> 32);
    const uint64_t scene = e.v[1];
    const bool pending = (e.v[4] >> 40) & 1, pd = (e.v[4] >> 41) & 1;
    const uint32_t res = pending ? static_cast<uint32_t>(e.v[2]) : static_cast<uint32_t>(e.v[3]);
    const uint32_t node = pending ? static_cast<uint32_t>(e.v[2] >> 32) : static_cast<uint32_t>(e.v[3] >> 32);
    const uint32_t index = static_cast<uint32_t>(e.v[4]);
    const uintptr_t file = e.v[5];
    const bool valid = static_cast<uint32_t>(e.v[3]) != 0 && static_cast<uint32_t>(e.v[3] >> 32) != 0xFFFFFFFFu &&
                       index != 0xFFFFFFFFu;
    if (scd::NpcTimeline* t = g_book.get(npc)) {
        ++t->reads;
        if (!t->t_2005) t->t_2005 = e.qpc_us;
        if (res) t->resource = res;
        t->node = node;
        t->scene = scene;
        t->handle_valid = valid;
        t->file_loaded = file != 0;
        if (owner) t->owner = owner;
    }
    if (scd::is_character_key(owner)) g_characters.insert(owner);
    const auto d = g_npc_delivered.find(npc);
    const scd::SceneLedger::Hit h = g_received.find(scene, res);
    write_line(kKG1,
               std::format("{} NPC 0x{:08X} (#{}) | {} | AI block: resource 0x{:08X} node 0x{:08X} scene key 0x{:X} "
                           "(u8 +0x61 {}){} | handle 0x{:016X} index {} file 0x{:X} -> {} | this read came {} | its "
                           "scene on this client: {} | play-data component: {}",
                           head(kKG1, e), npc, e.v[8], gsd::corr_token(scd::kOpActorLoad, npc, scene, res), res, node,
                           scene, (e.v[4] >> 48) & 0xFF, pending ? "" : " (resource/node from the handle only)", e.v[3],
                           static_cast<int32_t>(index), file, scd::handle_text(e.v[3], index, file),
                           d != g_npc_delivered.end()
                               ? scd::rel_text(e.qpc_us, d->second) + " after its 2005 was handed to the game"
                               : std::string("(its 2005 not seen by the delivery tap)"),
                           h.kind == 1   ? std::format("a 2335 of it reached this client ({} before this read)",
                                                       scd::rel_text(e.qpc_us, h.t_us))
                           : h.kind == 2 ? std::format("a 2661 of it reached this client ({} before this read)",
                                                       scd::rel_text(e.qpc_us, h.t_us))
                                         : std::string("NO 2335/2661 of it reached this client"),
                           pd ? std::format("owner comp+0x2D8 0x{:08X} comp+0x2E0 0x{:X}", owner, e.v[6])
                              : std::string("not readable yet")));
}

void on_delivered(const p::Event& e) {
    const uint16_t op = static_cast<uint16_t>(e.v[0]);
    const uint32_t a = static_cast<uint32_t>(e.v[1]), b = static_cast<uint32_t>(e.v[1] >> 32);
    switch (op) {
        case scd::kOpActorLoad:
            g_npc_delivered.try_emplace(a, e.qpc_us);  // a = the first NPC key, b = NPC records
            g_delivered_2005_npc += b;
            return;
        case scd::kOpMoveFieldAck:
            g_received.new_field(b, e.qpc_us);
            return;
        case scd::kOpScheduleCreate:
            g_received.schedule_create({e.v[2], a, b, e.qpc_us});
            break;
        case scd::kOpScheduleDelete:
            g_received.schedule_delete(e.v[2]);
            break;
        case scd::kOpScheduleClosed:
            g_received.schedule_closed(b);
            break;
        case scd::kOpPrepare:
            g_received.prepare({e.v[2], a, b, static_cast<uint32_t>(e.v[3]), static_cast<uint32_t>(e.v[3] >> 32),
                                e.qpc_us});
            break;
        case scd::kOpDeleteSequencer:
            g_received.remove_sequencer(e.v[2]);
            break;
        case scd::kOpInitSchedule:
            g_received.init({b, a, static_cast<uint32_t>(e.v[2]), static_cast<uint32_t>(e.v[2] >> 32), e.v[3], e.qpc_us});
            break;
        default:
            break;
    }
    ++g_delivered_scene;
    if (scd::is_character_key(a)) g_characters.insert(a);
    write_line(kKDelivered, std::format("{} {} to this client: actor 0x{:08X} | {} | received ledger: schedules {} "
                                        "sequencers {} inits {}",
                                        head(kKDelivered, e), op, a,
                                        gsd::corr_token(op, a, op == scd::kOpInitSchedule ? e.v[3] : e.v[2],
                                                        op == scd::kOpScheduleCreate  ? b
                                                        : op == scd::kOpPrepare       ? static_cast<uint32_t>(e.v[3] >> 32)
                                                        : op == scd::kOpInitSchedule || op == scd::kOpScheduleClosed ||
                                                                  op == scd::kOpDeleteSequencer
                                                            ? b
                                                            : 0),
                                        g_received.schedules().size(), g_received.sequencers().size(),
                                        g_received.inits().size()));
}

void on_req(const p::Event& e) {
    const uint16_t op = static_cast<uint16_t>(e.v[0]);
    const bool has_scene = (e.v[0] >> 16) & 1, has_actor = (e.v[0] >> 17) & 1;
    write_line(kKReq, std::format("{} FIRST {} of this client for {}{} (payload {} bytes) | from now on only counted",
                                  head(kKReq, e), op,
                                  has_scene ? std::format("scene 0x{:X}", e.v[1]) : std::string("-"),
                                  has_actor ? std::format(" actor 0x{:08X}", static_cast<uint32_t>(e.v[2])) : "",
                                  (e.v[0] >> 32) & 0xFFFF));
}

struct GameMem {
    bool u8(uintptr_t a, uint8_t& v) const noexcept { return rd(a, v); }
    bool u32(uintptr_t a, uint32_t& v) const noexcept { return rd(a, v); }
    bool ptr(uintptr_t a, uintptr_t& v) const noexcept { return rd(a, v); }
};

uintptr_t schedule_container() {
    using L = scd::ScheduleLayout;
    const uintptr_t mgr = rdp(g_base + L::kManagerSlot);
    return mgr ? rdp(mgr + L::kManager_Container) : 0;
}

// G2: every resource the NPCs named (most NPCs first, at most 64 per pass), with one NPC's node.
void g2_pass(bool summary_line) {
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> res;  // resource -> (NPCs, a node)
    for (const auto& [k, t] : g_book.all()) {
        if (!t.resource) continue;
        auto& r = res[t.resource];
        ++r.first;
        r.second = t.node;
    }
    std::vector<std::pair<uint32_t, std::pair<uint32_t, uint32_t>>> v(res.begin(), res.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.first > b.second.first; });
    const uintptr_t c = schedule_container();
    uint32_t counts[7] = {};
    size_t n = 0;
    for (const auto& [r, info] : v) {
        if (++n > 64) break;
        const scd::LookupResult lr = scd::schedule_lookup(GameMem{}, c, r, info.second);
        ++counts[static_cast<int>(lr.what)];
        auto [it, fresh] = g_g2_last.try_emplace(r, lr.what);
        if (!fresh && it->second == lr.what) continue;
        const scd::Lookup before = it->second;
        it->second = lr.what;
        write_line(kKG1, std::format("GuestScene [G2] {} | resource 0x{:08X} ({} NPC(s), node 0x{:08X}) in "
                                     "NPCScheduleManager's container 0x{:X} (buckets {}, used {}): {}{} (file 0x{:X} "
                                     "index {} nodes {})",
                                     p8_when(p8_now_us()), r, info.first, info.second, c, lr.buckets, lr.used,
                                     scd::lookup_text(lr.what),
                                     fresh ? "" : std::format(" (was: {})", scd::lookup_text(before)), lr.file,
                                     static_cast<int32_t>(lr.index), lr.nodes));
    }
    if (summary_line) {
        log::info("GuestScene [G2 summary] {} | container 0x{:X} | resources named by NPC AI blocks {} (looked up {}): "
                  "found {} | FILE NOT LOADED {} | unloading {} | node not found {} | empty container {} | no container "
                  "{} | unreadable {}",
                  p8_when(p8_now_us()), c, v.size(), std::min<size_t>(v.size(), 64),
                  counts[static_cast<int>(scd::Lookup::Found)], counts[static_cast<int>(scd::Lookup::FileNotLoaded)],
                  counts[static_cast<int>(scd::Lookup::FileUnloading)],
                  counts[static_cast<int>(scd::Lookup::NodeNotFound)], counts[static_cast<int>(scd::Lookup::Empty)],
                  counts[static_cast<int>(scd::Lookup::NoContainer)], counts[static_cast<int>(scd::Lookup::Unreadable)]);
    }
}

scd::XSnapshot read_x(uint32_t owner) {
    scd::XSnapshot s;
    s.owner = owner;
    const uintptr_t world = rdp(g_base + off::kWorldSystemSlot);
    const uintptr_t amgr = world ? rdp(world + kWorld_ActorMgr) : 0;
    const uintptr_t c = amgr ? rdp(amgr + kActorMgr_Container) : 0;
    s.actor = c ? gad::container_find(GameMem{}, c, owner) : 0;
    if (!s.actor) return s;
    const uintptr_t comps = rdp(s.actor + scp::kActor_Comps);
    const uintptr_t seq = comps ? rdp(comps + kComps_Seq) : 0;
    s.manager = seq ? rdp(seq + kSeqComp_Mgr) : 0;
    if (!s.manager) return s;
    s.manager_vt = rdp(s.manager);
    s.stages = rdp(s.manager + kMgr_Stages);
    if (s.stages) {
        s.stages_vt = rdp(s.stages);
        s.stage_count = rdv<uint32_t>(s.stages + kStages_Count);
        const uintptr_t data = rdp(s.stages + kStages_Data);
        for (uint32_t i = 0; data && i < 8 && i < s.stage_count; ++i) s.first[i] = rdp(data + 8 * i);
    }
    uint32_t i174 = 0;
    uint8_t ready = 0;
    if (rd(s.manager + kMgr_Count, i174)) s.i174 = static_cast<int32_t>(i174);
    if (rd(s.manager + kMgr_Ready, ready)) s.ready = ready;
    return s;
}

std::string rva_text(uintptr_t a) {
    return a >= g_base && a - g_base < 0x10000000 ? std::format("exe+0x{:X}", a - g_base) : std::format("0x{:X}", a);
}

void g3_pass(bool always) {
    std::vector<uint32_t> owners(g_characters.begin(), g_characters.end());
    if (std::find(owners.begin(), owners.end(), kHostCharacter) == owners.end()) owners.insert(owners.begin(), kHostCharacter);
    size_t n = 0;
    for (uint32_t o : owners) {
        if (++n > 8) break;
        const scd::XSnapshot s = read_x(o);
        auto [it, fresh] = g_g3_last.try_emplace(o, s);
        if (!fresh && it->second == s && !always) continue;
        it->second = s;
        std::string first;
        for (uint32_t i = 0; i < 8 && i < s.stage_count; ++i) first += std::format(" 0x{:X}", s.first[i]);
        write_line(kKG1,
                   std::format("GuestScene [G3 X] {} | character 0x{:08X}: actor 0x{:X} | X = its stage manager 0x{:X} "
                               "({}) | X+0x148 0x{:X} (vt {}) holds {} schedule stage(s){}{} | X+0x174 {} | X+0x231 {}{}",
                               p8_when(p8_now_us()), o, s.actor, s.manager,
                               !s.manager                               ? "none"
                               : s.manager_vt == g_base + kVtStageManager ? "ClientSequencerStageManager"
                                                                        : rva_text(s.manager_vt),
                               s.stages, rva_text(s.stages_vt), s.stage_count, first.empty() ? "" : ":", first, s.i174,
                               s.ready, fresh ? " (first)" : always ? " (periodic)" : " (CHANGED)"));
    }
}

void g4_summary() {
    std::string c;
    for (size_t i = 0; i < scd::kSceneReqCount; ++i) {
        if (const uint64_t v = g_req_counts[i].load()) c += std::format(" {} x{}", scd::kSceneReqs[i], v);
    }
    auto rows = g_req_pairs.rows();
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.c[0] > b.c[0]; });
    std::string top;
    for (size_t i = 0; i < rows.size() && i < 24; ++i) {
        top += std::format(" {}:0x{:X} x{}", static_cast<uint16_t>(rows[i].key >> 48),
                           rows[i].key & 0xFFFFFFFFFFFFull, rows[i].c[0]);
    }
    log::info("GuestScene [G4 summary] {} | this client's C>S scene/sequencer Reqs sent to the host:{} | 2223 for NPC keys "
              "{} (the server refuses them at its first gate: controller != session user) own/other {} | per (opcode, "
              "scene id or actor), top 24:{}",
              p8_when(p8_now_us()), c.empty() ? std::string(" none") : c, g_req_2223_npc.load(), g_req_2223_own.load(),
              top.empty() ? std::string(" none") : top);
}

void summary() {
    const scd::NpcBook::Summary s = g_book.summary();
    std::string st;
    for (int k = 0; k < kKindCount; ++k) {
        const KindStats& x = g_stats[k];
        st += std::format(" | {}: hits {} queued {} limited {} over budget {} dropped {} lines {}", kKindName[k],
                          x.hits.load(), x.queued.load(), x.limited.load(), x.over_budget.load(), x.dropped.load(),
                          g_lines[k].load());
    }
    log::info("GuestScene [G1 summary] {} | NPC AI blocks read {} (with a scene {}): handle valid {}, FILE NOT LOADED {}, "
              "file loaded but node not found {} | NPC records in the 2005 handed to the game {} | scene packets handed "
              "to the game {} (received ledger: schedules {} sequencers {} inits {}) | hooked {}/{} | ring dropped {}{}",
              p8_when(p8_now_us()), s.npcs, s.with_scene, s.valid, s.file_missing, s.node_missing,
              g_delivered_2005_npc, g_delivered_scene, g_received.schedules().size(), g_received.sequencers().size(),
              g_received.inits().size(), g_hooked.load(), g_wanted.load(), g_ring.dropped(), st);
}

// ---------------------------------------------------------------- install

template <class T>
bool peek(uintptr_t a, T& v) {
    return a && game::safe_read(a, &v, sizeof v);
}

bool read_sections() {
    g_secs.clear();
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!peek(g_base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || !peek(g_base + dos.e_lfanew, nt) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    const uintptr_t first = g_base + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) +
                            nt.FileHeader.SizeOfOptionalHeader;
    for (WORD i = 0; i < nt.FileHeader.NumberOfSections && i < 64; ++i) {
        IMAGE_SECTION_HEADER h{};
        if (!peek(first + i * sizeof h, h)) return false;
        gad::Section s;
        std::copy(std::begin(h.Name), std::end(h.Name), s.name);
        s.va = h.VirtualAddress;
        s.size = h.Misc.VirtualSize;
        g_secs.push_back(s);
    }
    return !g_secs.empty();
}

bool bytes_at(uintptr_t rva, std::span<const uint8_t> expected) {
    std::vector<uint8_t> actual(expected.size());
    return game::safe_read(g_base + rva, actual.data(), actual.size()) &&
           std::equal(actual.begin(), actual.end(), expected.begin());
}

struct Ctx {
    uintptr_t rva;
    std::span<const uint8_t> bytes;
};
struct PointDef {
    const char* name;
    uintptr_t rva;
    uint32_t patch;
    std::span<const uint8_t> bytes;
    std::vector<Ctx> ctx;
    safetyhook::MidHookFn fn;
};

std::vector<PointDef> points() {
    using namespace scp;
    return {
        {"G1 inner 0x1EB8946", kAiRead, kAiReadPatch, kAiReadBytes,
         {{kAiReadFrame, kAiReadFrameBytes}, {kAiReadArg, kAiReadArgBytes}, {kAiReadInit, kAiReadInitBytes},
          {kAiReadCall, kAiReadCallBytes}},
         on_ai_read},
        {"G1 0x206DE6B", kAiDone, kAiDonePatch, kAiDoneBytes,
         {{kAiDoneThis, kAiDoneThisBytes}, {kAiDoneCall, kAiDoneCallBytes}}, on_ai_done},
    };
}

bool all_bytes(const PointDef& d) {
    if (!bytes_at(d.rva, d.bytes)) return false;
    return std::all_of(d.ctx.begin(), d.ctx.end(), [](const Ctx& c) { return bytes_at(c.rva, c.bytes); });
}

DWORD WINAPI install_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (g_base != off::kImageBase || !peek(g_base, dos) || !peek(g_base + dos.e_lfanew, nt) ||
        nt.FileHeader.TimeDateStamp != off::kPeTimestamp) {
        log::error("GuestScene: unsupported game build; guest_scene_diag stays off");
        return 0;
    }
    if (!read_sections()) {
        log::error("GuestScene: the game's section table is unreadable; nothing is hooked");
        return 0;
    }
    const std::vector<PointDef> defs = points();
    g_wanted.store(static_cast<uint32_t>(defs.size()));
    for (int i = 0; i < 1200; ++i) {  // WorldSystem, as the P8 probes (up to 10 minutes)
        uintptr_t world = 0;
        if (peek(g_base + off::kWorldSystemSlot, world) && world) break;
        Sleep(500);
    }
    for (int i = 0; i < 24000; ++i) {  // the code (up to 2 minutes)
        if (std::all_of(defs.begin(), defs.end(), all_bytes)) break;
        Sleep(5);
    }
    std::string on, refused;
    for (size_t i = 0; i < defs.size(); ++i) {
        const PointDef& d = defs[i];
        bool ok = true;
        for (const Ctx& c : d.ctx) {
            if (!gad::in_real_code(g_secs, static_cast<uint32_t>(c.rva), static_cast<uint32_t>(c.bytes.size())) ||
                !bytes_at(c.rva, c.bytes)) {
                log::error("GuestScene: {} REFUSED: the code it relies on at exe+0x{:X} ({} bytes) is not the expected "
                           ".rsrc code of build 2976",
                           d.name, c.rva, c.bytes.size());
                ok = false;
                break;
            }
        }
        ok = ok && p8_install_mid(g_base, g_secs, d.rva, d.patch, d.bytes, d.fn, g_hooks[i], "GuestScene", d.name);
        if (ok) g_hooked.fetch_add(1);
        (ok ? on : refused) += std::format(" {}", d.name);
    }
    g_on.store(true);  // G2/G3/G4 need no hook
    log::info("GuestScene ON (guest_scene_diag, READ ONLY): hooked:{} | REFUSED:{} | nothing is written to the game, no "
              "game function is called, nothing is sent; lines 'GuestScene [...]' every 5 s from the diagnostics thread: "
              "G1 (each NPC's AI block and its handle, first {} per NPC), G2 (NPCScheduleManager replica per resource, on "
              "change), G3 (the host character's manager X, on change), G4 (first C>S scene Req per opcode and id); "
              "summaries every 60 s",
              on.empty() ? " none" : on, refused.empty() ? " none" : refused, kFirstPerNpc);
    return 0;
}

}  // namespace

void guest_scene_diag_start(bool on) {
    if (!on) return;
    if (g_started.exchange(true)) return;
    CloseHandle(CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr));
}

bool guest_scene_diag_wanted() { return g_started.load(); }

void guest_scene_diag_tick() noexcept {
    if (!g_on.load()) return;
    try {
        p::Event e;
        while (g_ring.pop(e)) {
            switch (e.kind) {
                case kKG1: on_g1(e); break;
                case kKDelivered: on_delivered(e); break;
                case kKReq: on_req(e); break;
                default: break;
            }
        }
        const bool minute = ++g_ticks % 12 == 0;
        g2_pass(minute);
        g3_pass(minute);
        if (minute) {
            summary();
            g4_summary();
        }
    } catch (...) {
        // allocation failure while formatting: these events are lost, the next run goes on
    }
}

std::string guest_scene_diag_describe() {
    if (!g_started.load()) return {};
    return std::format(" | scene diag {} hooked {}/{} (G1 {}, C>S scene Reqs {})", g_on.load() ? "on" : "starting",
                       g_hooked.load(), g_wanted.load(), g_stats[kKG1].hits.load(), g_stats[kKReq].hits.load());
}

void guest_scene_diag_on_out(std::span<const uint8_t> packet) noexcept {
    if (!g_started.load(std::memory_order_relaxed) || packet.size() < scd::kHeader) return;
    const uint16_t op = scd::le16(packet.data());
    const int i = scd::scene_req_index(op);
    if (i < 0) return;
    g_stats[kKReq].hits.fetch_add(1, std::memory_order_relaxed);
    g_req_counts[i].fetch_add(1, std::memory_order_relaxed);
    const scd::SceneReq r = scd::parse_scene_req(op, packet.subspan(scd::kHeader));
    if (op == scd::kOpStartActionReq) {
        (r.has_actor && scd::is_npc_key(r.actor) ? g_req_2223_npc : g_req_2223_own).fetch_add(1, std::memory_order_relaxed);
        return;  // its own actions: counted only
    }
    const uint64_t id = r.has_scene ? r.scene : r.has_actor ? r.actor : 0;
    bool fresh = false;
    g_req_pairs.add(static_cast<uint64_t>(op) << 48 | (id & 0xFFFFFFFFFFFFull), 0, 1, &fresh);
    if (!fresh) return;
    p::Event e;
    e.v[0] = op | static_cast<uint64_t>(r.has_scene) << 16 | static_cast<uint64_t>(r.has_actor) << 17 |
             static_cast<uint64_t>((packet.size() - scd::kHeader) & 0xFFFF) << 32;
    e.v[1] = r.scene;
    e.v[2] = r.actor;
    emit(kKReq, e);
}

void guest_scene_diag_on_delivered(std::span<const uint8_t> packet) noexcept {
    if (!g_started.load(std::memory_order_relaxed) || packet.size() < scd::kHeader) return;
    const uint16_t op = scd::le16(packet.data());
    const std::span<const uint8_t> pl = packet.subspan(scd::kHeader);
    p::Event e;
    e.v[0] = op;
    switch (op) {
        case scd::kOpActorLoad: {
            const scd::Scan2005 s = scd::scan_2005(pl.data(), pl.size());
            if (!s.npcs) return;
            e.v[1] = s.first_npc | static_cast<uint64_t>(s.npcs) << 32;
            break;
        }
        case scd::kOpMoveFieldAck:
            if (pl.size() < 13) return;
            e.v[1] = scd::le32(&pl[1]) | static_cast<uint64_t>(scd::le32(&pl[9])) << 32;
            break;
        case scd::kOpScheduleCreate: {
            const auto s = scd::parse_schedule_create(pl);
            if (!s) return;
            e.v[1] = s->actor | static_cast<uint64_t>(s->resource) << 32;
            e.v[2] = s->scene;
            break;
        }
        case scd::kOpScheduleDelete: {
            const auto s = scd::parse_schedule_delete(pl);
            if (!s) return;
            e.v[1] = s->actor;
            e.v[2] = s->scene;
            break;
        }
        case scd::kOpScheduleClosed: {
            const auto s = scd::parse_schedule_closed(pl);
            if (!s) return;
            e.v[1] = s->actor | static_cast<uint64_t>(s->id) << 32;
            break;
        }
        case scd::kOpPrepare: {
            const auto res = scd::prepare_resource(pl);
            if (!res || (pl.size() - obs::kPreparePrefix) % obs::kBindingWire != 0) return;
            e.v[1] = scd::le32(&pl[1]) | static_cast<uint64_t>(scd::le32(&pl[5])) << 32;
            e.v[2] = scd::le64(&pl[9]);
            e.v[3] = *res | static_cast<uint64_t>((pl.size() - obs::kPreparePrefix) / obs::kBindingWire) << 32;
            break;
        }
        case scd::kOpDeleteSequencer: {
            const auto d = obs::parse_delete_payload(pl);
            if (!d) return;
            e.v[1] = d->actor | static_cast<uint64_t>(d->extra) << 32;
            e.v[2] = d->seq;
            break;
        }
        case scd::kOpInitSchedule: {
            const auto s = scd::parse_init_schedule(pl);
            if (!s) return;
            e.v[1] = s->actor | static_cast<uint64_t>(s->npc) << 32;
            e.v[2] = s->resource | static_cast<uint64_t>(s->node) << 32;
            e.v[3] = s->scene;
            break;
        }
        default:
            return;
    }
    g_stats[kKDelivered].hits.fetch_add(1, std::memory_order_relaxed);
    emit(kKDelivered, e);
}

}  // namespace cdmp::session2
