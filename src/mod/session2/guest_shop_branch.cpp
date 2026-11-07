#include "mod/session2/guest_shop_branch.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <span>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <vector>

#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_seq_present.h"
#include "mod/session2/guest_seq_route.h"
#include "mod/session2/guest_shop_branch_logic.h"

namespace cdmp::session2 {
namespace {

// Live 2026-11-06 (scene 0x1B4): handing the host's own scene its branch on this client moves the camera but stops
// there: the guest's copy of a host scene has its player node unbound (guest_seq_foreign), and the shop's menu comes
// only after the scene sees ITS player. So the guest gets a scene of its own: a copy of the host scene's 2661 with
// this client's character as actor and owner and a local id, created in its own manager, and that copy is branched.
constexpr uint64_t kLocalTag = 0x1F000000ull;  // local ids: never one of the server's (small counters); fit 0x1FFFFFFF
constexpr uint64_t kPressWindowMs = 8000;
constexpr size_t kMaxPrepared = 1024;
constexpr int kMaxLines = 200;
constexpr uint16_t kOpDelete = 2810;
// The scene Reqs a client sends about its own scene (u64 scene @0): never sent for a local copy (the server has no
// such scene). observer_seq_copy_logic.h's list plus the 2276 / 2105.
constexpr uint16_t kSceneReqs[] = {2436, 2767, 2591, 2138, 2173, 2276, 2105};

std::atomic<bool> g_on{false}, g_npc_control{false}, g_proactive{false}, g_xmode{false};
// guest_shop_branch=5: as 1, and the copy's first branch-0 request after its shop branch gets the server's MENU answer
// (gsb::menu_answer), as the native owner's client does 2 s after the branch.
std::atomic<bool> g_menu{false};
std::set<uint64_t> g_menu_done;  // copies already given the menu step (g_mx)
std::set<uint64_t> g_left;       // copies that asked Leave (branch 3) since their last menu step (g_mx)
std::atomic<uint64_t> g_menu_given{0};
std::wstring g_ini;  // written once by guest_shop_branch_start, before any packet
uint32_t sell_node() {
    return g_ini.empty() ? 4 : GetPrivateProfileIntW(L"session2", L"guest_shop_sell_node", 4, g_ini.c_str());
}
// guest_shop_branch=4: the host scene itself (its copy X on this client) gets the shop branch, with its presentation
// and UI handlers let through while the shop is open (g_x_open_until, ms tick).
std::atomic<uint64_t> g_x_open_until{0}, g_x_scene{0};
std::atomic<uint32_t> g_x_owner{0};
std::map<uint64_t, uint64_t> g_want;       // host scene -> when a copy was asked (ms tick), until made
std::map<uint64_t, uint64_t> g_copy_host;  // copy -> its host scene (kept after the copy is gone)
std::mutex g_mx;
struct Press {
    uint32_t npc = 0;
    uint64_t stage = 0;
    uint64_t at = 0;
    uintptr_t actor = 0;  // the NPC's actor on this client (interact_gate's press target)
    uint32_t own_user = 0, npc_controller = 0;
};
Press g_press;
std::map<uint64_t, uint32_t> g_copies;  // copy -> its owner (this client's character)
// guest_shop_branch=2: this client controls the shopkeeper while its copy is in the shop branch.
constexpr uint32_t kActor_Key = 0x60, kActor_Controller = 0x90;
constexpr uint64_t kNpcControlMs = 12000, kNpcWithholdAfterMs = 3000;
struct NpcControl {
    uintptr_t actor = 0;
    uint32_t key = 0, own_user = 0, host_controller = 0;
    uint64_t copy = 0;
    uint64_t since = 0, released = 0;  // ms ticks; since 0 = armed, not swapped yet
    bool active = false;
};
NpcControl g_ctl;
std::atomic<uint32_t> g_ctl_key{0};  // the NPC whose Reqs are withheld (0 = none)
std::atomic<uint64_t> g_ctl_withheld{0};
std::map<uint64_t, std::vector<uint8_t>> g_prepared;  // host scene -> its 2661 as delivered (logical)
std::map<uint64_t, uint64_t> g_local;                 // host scene -> this client's copy
std::map<uint64_t, uint64_t> g_opened_at;             // copy -> when its shop branch was handed (ms tick)
// Experiment (live 2026-11-06): the copy asks branch 0 again every 2 s after the shop branch (its trigger volume never
// reports this client's character); those first requests are swallowed, unanswered, for this long.
constexpr uint64_t kHoldOpenMs = 10000;
std::vector<std::vector<uint8_t>> g_queue;
// The branch Acks of a copy just created: handed only once its stage has started and has its branch table
// (guest_seq_present_stage_ready); live 2026-11-06, 1.5 s was not enough and the branch crashed the client at
// exe+0xBA9DAA ([[stage+0x60]+0x38] still null). Dropped after kReadyTimeoutMs.
struct Delayed {
    uint64_t scene = 0;
    uint64_t since = 0;
    std::vector<std::vector<uint8_t>> acks;
};
std::vector<Delayed> g_delayed;
constexpr uint64_t kReadyTimeoutMs = 15000;
std::map<uint64_t, std::vector<uint8_t>> g_state;  // host scene -> its last 2981 (the copy starts from it)
std::atomic<int> g_lines{0}, g_open_n{0};
std::atomic<uint64_t> g_created{0}, g_opened{0}, g_answered{0}, g_withheld{0}, g_no_scene{0}, g_no_press{0};

bool may_log() { return g_lines.fetch_add(1, std::memory_order_relaxed) < kMaxLines; }

template <class T>
T rd(uintptr_t a, T fallback = T{}) noexcept {
    T v{};
    return a >= 0x10000 && game::safe_read(a, &v, sizeof v) ? v : fallback;
}
bool write_u32(uintptr_t a, uint32_t v) noexcept {
    if (a < 0x10000) return false;
    __try {
        *reinterpret_cast<volatile uint32_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
// g_mx held. Swap in (the copy's shop branch was just handed) or put back (time is up / the copy is gone).
void npc_control_swap_in(uint64_t now) {
    if (!g_ctl.actor || g_ctl.active || g_ctl.since) return;
    g_ctl.since = now;
    const uint32_t key = rd<uint32_t>(g_ctl.actor + kActor_Key), ctl = rd<uint32_t>(g_ctl.actor + kActor_Controller);
    if (key != g_ctl.key || ctl != g_ctl.host_controller || !write_u32(g_ctl.actor + kActor_Controller, g_ctl.own_user)) {
        if (may_log()) {
            log::warn("GuestShopBranch: shopkeeper 0x{:08X}: not swapped (key 0x{:08X}, controller 0x{:08X}, expected "
                      "0x{:08X})",
                      g_ctl.key, key, ctl, g_ctl.host_controller);
        }
        g_ctl = NpcControl{};
        return;
    }
    g_ctl.active = true;
    g_ctl_key.store(g_ctl.key, std::memory_order_relaxed);
    if (may_log()) {
        log::info("GuestShopBranch: shopkeeper 0x{:08X} is this client's for {} s (controller 0x{:08X} -> 0x{:08X}); its "
                  "Reqs for it are withheld",
                  g_ctl.key, kNpcControlMs / 1000, g_ctl.host_controller, g_ctl.own_user);
    }
}
void npc_control_tick(uint64_t now) {
    if (g_ctl.active && (now - g_ctl.since > kNpcControlMs || !g_copies.contains(g_ctl.copy))) {
        const bool ours = rd<uint32_t>(g_ctl.actor + kActor_Key) == g_ctl.key &&
                          rd<uint32_t>(g_ctl.actor + kActor_Controller) == g_ctl.own_user;
        const bool put = ours && write_u32(g_ctl.actor + kActor_Controller, g_ctl.host_controller);
        g_ctl.active = false;
        g_ctl.released = now;
        if (may_log()) {
            log::info("GuestShopBranch: shopkeeper 0x{:08X}: controller {} | {} of its Reqs withheld", g_ctl.key,
                      put ? std::format("put back to 0x{:08X}", g_ctl.host_controller)
                          : std::string("left as it is (the game changed it meanwhile)"),
                      g_ctl_withheld.load());
        }
    }
    if (!g_ctl.active && g_ctl.released && now - g_ctl.released > kNpcWithholdAfterMs) {
        g_ctl_key.store(0, std::memory_order_relaxed);
        g_ctl = NpcControl{};
    }
}

uint64_t local_id(uint64_t host_scene) { return kLocalTag | (host_scene & 0xFFFFFFull); }

// The host scene's 2661 with every 4-byte occurrence of the host's character key (owner @5) as this client's key and
// the scene id @9 as the copy's.
std::vector<uint8_t> copy_prepare(const std::vector<uint8_t>& p, uint32_t own, uint64_t copy) {
    std::vector<uint8_t> v = p;
    uint32_t host = 0;
    std::memcpy(&host, v.data() + gsb::kHeader + 5, 4);
    for (size_t i = gsb::kHeader; i + 4 <= v.size(); ++i) {
        uint32_t x = 0;
        std::memcpy(&x, v.data() + i, 4);
        if (x == host) {
            std::memcpy(v.data() + i, &own, 4);
            i += 3;
        }
    }
    std::memcpy(v.data() + gsb::kHeader + 9, &copy, 8);
    return v;
}

}  // namespace

void guest_shop_branch_start(const std::string& mode, bool guest, const std::wstring& ini_path) {
    g_ini = ini_path;
    if (mode.empty() || mode == "0") return;
    if (!guest) {
        log::info("GuestShopBranch: guest_shop_branch is a guest key; ignored on the host");
        return;
    }
    if (mode != "1" && mode != "2" && mode != "3" && mode != "4" && mode != "5") {
        log::warn("GuestShopBranch: guest_shop_branch '{}' is not 0 | 1 | 2 | 3 | 4 | 5; it stays off", mode);
        return;
    }
    g_menu.store(mode == "5");
    if (mode == "5") {
        log::info("GuestShopBranch (guest_shop_branch=5): as 1, and the copy's first branch-0 request after its shop branch "
                  "gets the server's MENU answer (2981 phase {} tag 0x{:08X}), as the native owner's client 2 s after the "
                  "branch",
                  gsb::kShopMenuPhase, gsb::kShopMenuTag);
    }
    g_on.store(true);
    g_npc_control.store(mode == "2");
    g_proactive.store(mode == "3");
    g_xmode.store(mode == "4");
    if (mode == "4") {
        log::info("GuestShopBranch (guest_shop_branch=4): after this client's own 'Shop', the HOST scene as this client has "
                  "it gets the shop branch here, its presentation un-muted and its UI handlers let through while open");
    }
    if (mode == "3") {
        log::info("GuestShopBranch (guest_shop_branch=3): a shopkeeper's scene gets this client's copy before the press; "
                  "C1 points its interaction entries at the copy once it runs, and the copy handles the press itself");
    }
    if (mode == "2") {
        log::info("GuestShopBranch (guest_shop_branch=2): while its copy is in the shop branch this client controls the "
                  "shopkeeper ({} s at most), as the native owner does",
                  kNpcControlMs / 1000);
    }
    log::info("GuestShopBranch ON: after this client's own 'Shop' (0x{:08X}) on a shopkeeper of the host's world, it gets "
              "a scene of its own (a copy of the host's shop scene with this client's character as its player, id "
              "0x1F000000 | scene) and that copy goes to the shop branch {} on this client only; its Reqs about the copy "
              "never reach the host (a branch request is answered locally)",
              gsb::kShopInteraction, gsb::kShopBranch);
}

void guest_shop_note_press(uint32_t npc, uint64_t stage, uintptr_t actor, uint32_t own_user,
                           uint32_t npc_controller) noexcept {
    if (!g_on.load(std::memory_order_relaxed) || !npc || !(stage & 0x1FFFFFFFull)) return;
    try {
        std::lock_guard lk(g_mx);
        g_press = Press{npc, stage, GetTickCount64(), actor, own_user, npc_controller};
    } catch (...) {
    }
}

void guest_shop_after_deliver(std::span<const uint8_t> p, std::vector<std::vector<uint8_t>>& out) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    try {
        const uint16_t op = gsb::op_of(p);
        if (op == gsb::kOpPrepare) {
            if (const auto pr = gsb::parse_prepare(p)) {
                std::lock_guard lk(g_mx);
                if (g_prepared.size() >= kMaxPrepared) g_prepared.erase(g_prepared.begin());
                g_prepared[pr->first] = std::vector<uint8_t>(p.begin(), p.end());
            }
            return;
        }
        if (op == gsb::kOpBranchState && p.size() >= gsb::kHeader + 13) {
            uint64_t scene = 0;
            std::memcpy(&scene, p.data() + gsb::kHeader + 5, 8);
            std::lock_guard lk(g_mx);
            if (g_state.size() >= kMaxPrepared) g_state.erase(g_state.begin());
            g_state[scene] = std::vector<uint8_t>(p.begin(), p.end());
            return;
        }
        if (op == kOpDelete && p.size() >= gsb::kHeader + 12) {
            // The host scene is gone: so is this client's copy of it (the same 2810, its actor and id).
            uint64_t scene = 0;
            std::memcpy(&scene, p.data() + gsb::kHeader + 4, 8);
            std::lock_guard lk(g_mx);
            g_prepared.erase(scene);
            const auto it = g_local.find(scene);
            if (it == g_local.end()) return;
            const auto c = g_copies.find(it->second);
            if (c != g_copies.end()) {
                std::vector<uint8_t> d(p.begin(), p.end());
                std::memcpy(d.data() + gsb::kHeader, &c->second, 4);
                std::memcpy(d.data() + gsb::kHeader + 4, &it->second, 8);
                out.push_back(std::move(d));
                g_copies.erase(c);
            }
            g_local.erase(it);
            g_open_n.store(static_cast<int>(g_copies.size()), std::memory_order_relaxed);
            return;
        }
        if (op != gsb::kOpDoStepOk) return;
        const auto d = gsb::parse_dostep(p);
        if (!d || d->interaction != gsb::kShopInteraction || d->requester != d->actor) return;
        const uint32_t own = d->requester;
        std::lock_guard lk(g_mx);
        const Press press = g_press;
        if (g_xmode.load(std::memory_order_relaxed)) {
            g_press = Press{};
            uint32_t owner = 0;
            if (const auto pr = g_prepared.find(press.stage); pr != g_prepared.end()) {
                if (const auto pp = gsb::parse_prepare(pr->second)) owner = pp->second;
            }
            if (!press.stage || !owner) {
                if (may_log()) log::warn("GuestShopBranch: X mode: the host scene 0x{:X} of 0x{:08X} is unknown", press.stage, d->target);
                return;
            }
            const bool unmuted = guest_seq_present_set_muted(press.stage, false);
            g_x_scene.store(press.stage);
            g_x_owner.store(owner);
            g_x_open_until.store(GetTickCount64() + 60000);
            for (auto& a : gsb::answer(owner, press.stage, gsb::kShopBranch, true)) out.push_back(std::move(a));
            if (may_log()) {
                log::info("GuestShopBranch: X mode: shopkeeper 0x{:08X}: host scene 0x{:X} (owner 0x{:08X}) {} | its UI let through "
                          "60 s | branch {} handed to this client",
                          d->target, press.stage, owner, unmuted ? "un-muted" : "NOT un-muted (not cut, or unknown)",
                          gsb::kShopBranch);
            }
            return;
        }
        if (g_proactive.load(std::memory_order_relaxed)) {
            g_press = Press{};
            if ((press.stage & 0xFF000000ull) == kLocalTag && g_copies.contains(press.stage)) {
                if (may_log()) {
                    log::info("GuestShopBranch: shopkeeper 0x{:08X}: 'Shop' succeeded on this client's copy 0x{:X}: the "
                              "copy asks its branch itself (answered here)",
                              d->target, press.stage);
                }
            } else if (press.stage && !g_local.contains(press.stage) && !g_want.contains(press.stage)) {
                g_want[press.stage] = GetTickCount64();
                if (may_log()) {
                    log::info("GuestShopBranch: shopkeeper 0x{:08X}: 'Shop' succeeded on the host scene 0x{:X} (no copy yet): "
                              "its copy is made now; the next press uses it",
                              d->target, press.stage);
                }
            }
            return;
        }
        if (press.npc != d->target || GetTickCount64() - press.at > kPressWindowMs) {
            g_no_press.fetch_add(1, std::memory_order_relaxed);
            if (may_log()) {
                log::warn("GuestShopBranch: a shop 2916 for 0x{:08X} with no matching press (last press 0x{:08X}); "
                          "nothing handed",
                          d->target, press.npc);
            }
            return;
        }
        g_press = Press{};
        uint64_t copy = 0;
        bool created = false;
        if (const auto it = g_local.find(press.stage); it != g_local.end()) {
            copy = it->second;
        } else {
            const auto pr = g_prepared.find(press.stage);
            if (pr == g_prepared.end()) {
                g_no_scene.fetch_add(1, std::memory_order_relaxed);
                if (may_log()) {
                    log::warn("GuestShopBranch: shopkeeper 0x{:08X}: the 2661 of its scene 0x{:X} was not seen; nothing "
                              "handed",
                              d->target, press.stage);
                }
                return;
            }
            copy = local_id(press.stage);
            out.push_back(copy_prepare(pr->second, own, copy));
            // Its current branch state as the host scene had it (the server sends one with every 2661).
            if (const auto st = g_state.find(press.stage); st != g_state.end()) {
                std::vector<uint8_t> s0 = st->second;
                std::memcpy(s0.data() + gsb::kHeader + 1, &own, 4);
                std::memcpy(s0.data() + gsb::kHeader + 5, &copy, 8);
                out.push_back(std::move(s0));
            }
            g_local[press.stage] = copy;
            g_copies[copy] = own;
            g_open_n.store(static_cast<int>(g_copies.size()), std::memory_order_relaxed);
            created = true;
            g_created.fetch_add(1, std::memory_order_relaxed);
        }
        if (g_npc_control.load(std::memory_order_relaxed) && press.actor && press.own_user && press.npc_controller &&
            !g_ctl.actor) {
            g_ctl = NpcControl{press.actor, press.npc, press.own_user, press.npc_controller, copy};
        }
        Delayed dl{copy, GetTickCount64(), gsb::answer(own, copy, gsb::kShopBranch, true)};
        if (!created && guest_seq_present_stage_ready(copy)) {
            for (auto& a : dl.acks) out.push_back(std::move(a));
            g_opened_at[copy] = GetTickCount64();
            g_menu_done.erase(copy);
            if (g_ctl.copy == copy) npc_control_swap_in(GetTickCount64());
        } else {
            g_delayed.push_back(std::move(dl));
        }
        g_opened.fetch_add(1, std::memory_order_relaxed);
        if (may_log()) {
            log::info("GuestShopBranch: shopkeeper 0x{:08X}: this client's 'Shop' succeeded | host scene 0x{:X} -> this "
                      "client's copy 0x{:X} ({}) -> branch {} (handed to this client only)",
                      d->target, press.stage, copy, created ? "created now from the host scene's 2661" : "reused",
                      gsb::kShopBranch);
        }
    } catch (...) {
    }
}

void guest_shop_drain(std::vector<std::vector<uint8_t>>& out) noexcept {
    if (!g_on.load(std::memory_order_relaxed)) return;
    try {
        std::lock_guard lk(g_mx);
        for (auto& q : g_queue) out.push_back(std::move(q));
        g_queue.clear();
        const uint64_t now = GetTickCount64();
        npc_control_tick(now);
        if (const uint64_t x = g_x_scene.load(); x && g_x_open_until.load() && now >= g_x_open_until.load()) {
            g_x_open_until.store(0);
            g_x_scene.store(0);
            const bool muted = guest_seq_present_set_muted(x, true);
            if (may_log()) log::info("GuestShopBranch: X mode: the shop of 0x{:X} timed out; {}", x, muted ? "muted again" : "not muted");
        }
        if (!g_want.empty()) {
            const uint32_t own = guest_seq_route_own_key();
            for (auto it = g_want.begin(); it != g_want.end();) {
                const uint64_t host = it->first;
                const auto pr = g_prepared.find(host);
                if (g_local.contains(host)) {
                    it = g_want.erase(it);
                } else if (pr != g_prepared.end() && own) {
                    const uint64_t copy = local_id(host);
                    out.push_back(copy_prepare(pr->second, own, copy));
                    if (const auto st = g_state.find(host); st != g_state.end()) {
                        std::vector<uint8_t> s0 = st->second;
                        std::memcpy(s0.data() + gsb::kHeader + 1, &own, 4);
                        std::memcpy(s0.data() + gsb::kHeader + 5, &copy, 8);
                        out.push_back(std::move(s0));
                    }
                    g_local[host] = copy;
                    g_copies[copy] = own;
                    g_copy_host[copy] = host;
                    g_open_n.store(static_cast<int>(g_copies.size()), std::memory_order_relaxed);
                    g_created.fetch_add(1, std::memory_order_relaxed);
                    if (may_log()) {
                        log::info("GuestShopBranch: host scene 0x{:X} -> this client's copy 0x{:X} made ahead of the press "
                                  "(owner 0x{:08X})",
                                  host, copy, own);
                    }
                    it = g_want.erase(it);
                } else if (now - it->second > 30000) {
                    if (may_log()) {
                        log::warn("GuestShopBranch: no copy of host scene 0x{:X} ({}): dropped", host,
                                  own ? "its 2661 was not seen" : "this client's character is not known");
                    }
                    it = g_want.erase(it);
                } else {
                    ++it;
                }
            }
        }
        if (g_delayed.empty()) return;
        for (auto it = g_delayed.begin(); it != g_delayed.end();) {
            if (guest_seq_present_stage_ready(it->scene)) {
                for (auto& a : it->acks) out.push_back(std::move(a));
                g_opened_at[it->scene] = now;
                g_menu_done.erase(it->scene);
                if (g_ctl.copy == it->scene) npc_control_swap_in(now);
                if (may_log()) log::info("GuestShopBranch: copy 0x{:X} is ready: branch {} handed", it->scene, gsb::kShopBranch);
                it = g_delayed.erase(it);
            } else if (now - it->since > kReadyTimeoutMs) {
                if (may_log()) {
                    log::warn("GuestShopBranch: copy 0x{:X} never became ready in {} s: its branch is NOT handed (no shop "
                              "window this time)",
                              it->scene, kReadyTimeoutMs / 1000);
                }
                it = g_delayed.erase(it);
            } else {
                ++it;
            }
        }
    } catch (...) {
    }
}

bool guest_shop_withhold(const uint8_t* packet, size_t size) noexcept {
    // guest_shop_branch=2: this client's Reqs for the shopkeeper it controls for now (its actor key first).
    if (const uint32_t npc = g_ctl_key.load(std::memory_order_relaxed); npc && packet && size >= gsb::kHeader + 4) {
        uint32_t first = 0;
        std::memcpy(&first, packet + gsb::kHeader, 4);
        if (first == npc) {
            if (g_ctl_withheld.fetch_add(1, std::memory_order_relaxed) < 12 && may_log()) {
                log::info("GuestShopBranch: withheld this client's {} for the shopkeeper 0x{:08X} ({} B)",
                          gsb::op_of(std::span<const uint8_t>(packet, size)), npc, size);
            }
            return true;
        }
    }
    // guest_shop_branch=4: this client's branch requests for the open host scene, answered here (the host drops them).
    if (g_xmode.load(std::memory_order_relaxed) && packet && size >= gsb::kHeader + 12 &&
        GetTickCount64() < g_x_open_until.load(std::memory_order_relaxed)) {
        const std::span<const uint8_t> p(packet, size);
        if (const auto b = gsb::parse_branch_req(p); b && b->first == g_x_scene.load()) {
            try {
                std::lock_guard lk(g_mx);
                const bool avoid = b->second == 0 || b->second == gsb::kShopBranch;
                for (auto& a : gsb::answer(g_x_owner.load(), b->first, b->second, avoid)) g_queue.push_back(std::move(a));
                if (may_log()) log::info("GuestShopBranch: X mode: this client asked branch {} of 0x{:X}: answered here", b->second, b->first);
                // Natively the scene goes back to branch 0 with the menu still open and handles the choice there:
                // kept open (60 s after this client's last request about it).
                g_x_open_until.store(GetTickCount64() + 60000);
            } catch (...) {
            }
            return true;
        }
    }
    if (!g_on.load(std::memory_order_relaxed) || !packet || size < gsb::kHeader + 8 ||
        g_open_n.load(std::memory_order_relaxed) == 0) {
        return false;
    }
    try {
        const std::span<const uint8_t> p(packet, size);
        const uint16_t op = gsb::op_of(p);
        bool scene_req = false;
        for (const uint16_t s : kSceneReqs) scene_req = scene_req || s == op;
        if (!scene_req) return false;
        uint64_t scene = 0;
        std::memcpy(&scene, packet + gsb::kHeader, 8);
        std::lock_guard lk(g_mx);
        const auto it = g_copies.find(scene);
        if (it == g_copies.end()) return false;
        g_withheld.fetch_add(1, std::memory_order_relaxed);
        if (const auto b = gsb::parse_branch_req(p)) {
            const auto at = g_opened_at.find(scene);
            if (g_menu.load(std::memory_order_relaxed)) {
                // guest_shop_branch=5: every branch request of the copy answered as the server answers the native owner.
                const bool shown = g_menu_done.count(scene) != 0, left = g_left.count(scene) != 0;
                const uint32_t sell = b->second == 1 && shown && !left ? sell_node() : 4;
                for (auto& a : gsb::native_answer(it->second, scene, b->second, shown, left, sell)) {
                    g_queue.push_back(std::move(a));
                }
                if (b->second == 0 && !shown) {
                    g_menu_done.insert(scene);
                    g_left.erase(scene);
                    g_menu_given.fetch_add(1, std::memory_order_relaxed);
                } else if (b->second == 3) {
                    g_left.insert(scene);
                } else if (b->second == 0 || (b->second == 1 && !left) || b->second == gsb::kShopBranch) {
                    // a window opened (Buy / Sell) or the shop again: its Esc asks branch 0, a menu step again
                    g_menu_done.erase(scene);
                }
                g_answered.fetch_add(1, std::memory_order_relaxed);
                if (may_log()) {
                    log::info("GuestShopBranch: copy 0x{:X} asked branch {} ({} ms after its shop branch): answered as the "
                              "server does{}",
                              scene, b->second, at != g_opened_at.end() ? GetTickCount64() - at->second : 0,
                              b->second == 0 && !shown          ? " -- the MENU step (2981 2 | 0 | 0x979C909D)"
                              : b->second == 0                  ? " -- Buy (2981 3 | 0)"
                              : b->second == 3                  ? " -- Leave (2981 7 | 3)"
                              : b->second == 1 && shown && !left ? " -- Sell (2981 <guest_shop_sell_node> | 1)"
                              : b->second == 1                  ? " -- idle after Leave (2981 0 | 1 | 0x979C909D)"
                                                                : "");
                }
                return true;
            }
            if (!g_proactive.load(std::memory_order_relaxed) && !g_menu.load(std::memory_order_relaxed) && b->second == 0 &&
                at != g_opened_at.end() &&
                GetTickCount64() - at->second < kHoldOpenMs) {
                if (may_log()) log::info("GuestShopBranch: branch 0 of copy 0x{:X} swallowed (held open)", scene);
                return true;
            }
            const bool avoid = b->second == 0 || (g_proactive.load(std::memory_order_relaxed) && b->second == gsb::kShopBranch);
            for (auto& a : gsb::answer(it->second, scene, b->second, avoid)) g_queue.push_back(std::move(a));
            g_answered.fetch_add(1, std::memory_order_relaxed);
            if (may_log()) {
                log::info("GuestShopBranch: this client asked branch {} of its copy 0x{:X}: answered locally{}", b->second,
                          scene, b->second == 0 ? " (the shop is closed)" : "");
            }
        }
        return true;  // the server has no such scene
    } catch (...) {
        return false;
    }
}

bool guest_shop_open_now() noexcept {
    return g_xmode.load(std::memory_order_relaxed) && GetTickCount64() < g_x_open_until.load(std::memory_order_relaxed);
}

bool guest_shop_proactive() noexcept { return g_proactive.load(std::memory_order_relaxed); }

void guest_shop_want_copy(uint64_t host_scene, const char* why) noexcept {
    if (!g_proactive.load(std::memory_order_relaxed) || !(host_scene & 0x1FFFFFFFull) ||
        (host_scene & 0xFF000000ull) == kLocalTag) {
        return;
    }
    try {
        std::lock_guard lk(g_mx);
        if (g_local.contains(host_scene) || g_want.contains(host_scene)) return;
        g_want[host_scene] = GetTickCount64();
        if (may_log()) log::info("GuestShopBranch: a copy of host scene 0x{:X} is wanted ({})", host_scene, why);
    } catch (...) {
    }
}

uint64_t guest_shop_ready_copy(uint64_t host_scene) noexcept {
    if (!g_proactive.load(std::memory_order_relaxed)) return 0;
    uint64_t copy = 0;
    try {
        std::lock_guard lk(g_mx);
        const auto it = g_local.find(host_scene);
        if (it == g_local.end()) return 0;
        copy = it->second;
    } catch (...) {
        return 0;
    }
    return copy;  // C1 checks that the native lookup finds it in M
}

uint64_t guest_shop_host_of(uint64_t copy) noexcept {
    try {
        std::lock_guard lk(g_mx);
        const auto it = g_copy_host.find(copy);
        return it == g_copy_host.end() ? 0 : it->second;
    } catch (...) {
        return 0;
    }
}

bool guest_shop_copy_alive(uint64_t copy) noexcept {
    try {
        std::lock_guard lk(g_mx);
        return g_copies.contains(copy);
    } catch (...) {
        return false;
    }
}

}  // namespace cdmp::session2
