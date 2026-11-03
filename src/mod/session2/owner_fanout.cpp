#include "mod/session2/owner_fanout.h"

#include <windows.h>

#include <intrin.h>
#include <safetyhook.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "mod/game/hook_prepare.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest_actor_diag_logic.h"
#include "mod/session2/observer_seq_copy.h"
#include "mod/session2/owner_fanout_logic.h"
#include "mod/session2/p8_probes.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

// ToTarget::vf1 (pa::ToTargetSendTargetFinder, vtable exe+0x5B25438 slot 1; the same function is
// ToControlUser's vf1), build 1.0.0.2976. Its whole body, 0x2A757F0..0x2A7584E (ret), 95 bytes:
//   mov [rsp+8],rbx ; push rdi ; sub rsp,0x20 ; cmp qword [rcx+0x10],0 ; mov rbx,rdx ; mov rdi,rcx ;
//   je +0x3D ; ... grow the array with 0x35E3F0 if full ... ; [data + count*8] = [rcx+0x10] ; inc count ; ret
constexpr uintptr_t kVf1 = 0x2A757F0;
constexpr uint8_t kVf1Bytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x79, 0x10, 0x00, 0x48, 0x8B, 0xDA, 0x48,
    0x8B, 0xF9, 0x74, 0x3D, 0x8B, 0x4A, 0x08, 0x8B, 0x42, 0x0C, 0x3B, 0xC1, 0x77, 0x22, 0x8D, 0x14, 0x45, 0x01, 0x00,
    0x00, 0x00, 0x03, 0xD0, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xD1, 0xEA, 0x3B, 0xD0, 0x0F, 0x42, 0xD0, 0x3B, 0xCA, 0x0F,
    0x47, 0xD1, 0x48, 0x8B, 0xCB, 0xE8, 0xBD, 0x8B, 0x8E, 0xFD, 0x8B, 0x53, 0x08, 0x48, 0x8B, 0x47, 0x10, 0x48, 0x8B,
    0x0B, 0x48, 0x89, 0x04, 0xD1, 0xFF, 0x43, 0x08, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3};
static_assert(sizeof kVf1Bytes == 95);
// The patch safetyhook may write: a 5-byte jmp (the first instruction) or a 14-byte one (the first
// four: 5 + 1 + 4 + 5 = 15). None of them is RIP-relative and no branch of the body lands inside.
constexpr uint32_t kBoundaries[] = {5, 15};
// The vtable (data): slots 0-2 as observer_milestone checks them.
constexpr uintptr_t kVtToTarget = 0x5B25438;
constexpr uintptr_t kVf0 = 0x2A75740, kVf2 = 0x2A75850;
constexpr uint32_t kFinder_Session = 0x10;  // the session vf1 pushes (the ctor: actor->vfE8())
constexpr uint32_t kArray_Count = 0x08;     // {T* data; u32 count; u32 cap}
// ToControlUser's vtable (exe+0x5B252F8): the only other vtable whose slot 1 is vf1 (scan of the PE). Its
// writers (2339, 2979, 3018, 2954, 2652, 3097, 2184) are not in the lists of §3.4; counted apart by target.
constexpr uintptr_t kVtToControlUser = 0x5B252F8;
constexpr uint64_t kSummaryMs = 60000;
constexpr size_t kTop = 12;

std::atomic<ofo::Mode> g_mode{ofo::Mode::Off};
std::atomic<bool> g_active{false};
uintptr_t g_base = 0;
uintptr_t g_vt = 0;          // exe + kVtToTarget, set before the hook goes in
uintptr_t g_vt_control = 0;  // exe + kVtToControlUser (0 if its slot 1 is not vf1: that bucket stays empty)
SafetyHookInline g_hook;

// The patch, for owner_fanout_read_unpatched (written before the hook is enabled, then read only).
std::atomic<bool> g_patch_known{false};
uintptr_t g_patch_at = 0;
size_t g_patch_len = 0;
uint8_t g_patch_orig[16] = {};
uintptr_t g_patch_detour = 0;                    // where our jump must lead
uintptr_t g_tramp_lo = 0, g_tramp_hi = 0;        // safetyhook's trampoline (the E9 form's stub is inside)

ofo::Counters g_c;
u2f::OpCounts g_client;  // S>C opcodes the host's client got during a trip (sniffer)
u2f::OpCounts g_extra;   // option A's copies queued for user 2 during a trip (server_push_extra)
std::atomic<uint64_t> g_extra_refused{0};
std::atomic<bool> g_sniffer_fed{false};  // the sniffer's tap reached this module at least once

// The trip: the session 2 it began with (0 = none) and the owner the server wrote.
std::atomic<uintptr_t> g_trip_s2{0};
std::atomic<uint32_t> g_trip_owner{0};
std::atomic<uint64_t> g_trip_since_ms{0}, g_observed_ms{0}, g_trips{0};
// The last MoveFieldAck seen (for the tick's line): a sequence and what it said.
std::atomic<uint64_t> g_move_seq{0};
std::atomic<uint32_t> g_move_owner{0}, g_move_user{0};
std::atomic<bool> g_move_observer{false};
std::atomic<uint64_t> g_trip_ended_gone{0};

uint32_t rva_of(uintptr_t a) {
    return a >= g_base && a - g_base < 0x20000000u ? static_cast<uint32_t>(a - g_base) : 0u;
}

void end_trip(uint64_t now) {
    if (g_trip_s2.exchange(0, std::memory_order_acq_rel)) {
        const uint64_t since = g_trip_since_ms.load(std::memory_order_relaxed);
        if (now > since) g_observed_ms.fetch_add(now - since, std::memory_order_relaxed);
    }
}

// ofo::scan_array over the game's array: in, not in, unreadable, or too long to scan (unknown).
int array_has(uintptr_t arr, uintptr_t s2) noexcept {
    uintptr_t data = 0;
    uint32_t count = 0;
    const bool header = arr && game::fast_read(arr, &data, sizeof data) &&
                        game::fast_read(arr + kArray_Count, &count, sizeof count);
    return ofo::scan_array(header, data == 0, count, s2, [data](uint32_t i, uintptr_t& s) {
        return game::fast_read(data + 8ull * i, &s, sizeof s);
    });
}

// After the original vf1, on whatever server thread called it. Relaxed atomics and guarded reads only.
void after_vf1(uintptr_t finder, uintptr_t arr, uintptr_t ret) noexcept {
    g_c.calls.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t trip_s2 = g_trip_s2.load(std::memory_order_acquire);
    if (!trip_s2) return;
    g_c.observing.fetch_add(1, std::memory_order_relaxed);
    ofo::CallIn in;
    in.observing = true;
    in.trip_s2 = trip_s2;
    in.to_target_vt = g_vt;
    in.to_control_vt = g_vt_control;
    in.finder_read = game::fast_read(finder, &in.vtable, sizeof in.vtable) &&
                     game::fast_read(finder + kFinder_Session, &in.target, sizeof in.target);
    const bool known_finder = in.vtable == in.to_target_vt || (in.to_control_vt && in.vtable == in.to_control_vt);
    if (in.finder_read && known_finder && in.target) {
        in.s2_live = server_live_session2();
        in.logout = server_logout_requested();
        in.s1 = server_host_session();
        if (in.s2_live && !in.logout && in.s2_live == trip_s2 && in.target == in.s1) {
            in.s2_in_array = array_has(arr, in.s2_live);
        }
    }
    const ofo::Verdict v = ofo::judge(in);
    if (v == ofo::Verdict::HostTarget) {
        g_c.count_host(rva_of(ret));
    } else if (v == ofo::Verdict::ControlUserHost) {
        g_c.count_control_host(rva_of(ret));
    } else {
        g_c.count(v);
    }
}

uintptr_t __fastcall to_target_vf1(void* finder, void* out) {
    const uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    // unsafe_call, as relay_fix: no hook mutex around the game's code.
    const uintptr_t r = g_hook.unsafe_call<uintptr_t>(finder, out);
    after_vf1(reinterpret_cast<uintptr_t>(finder), reinterpret_cast<uintptr_t>(out), ret);
    return r;
}

bool code_matches(uintptr_t rva, const uint8_t* expected, size_t n) {
    std::vector<uint8_t> actual(n);
    return owner_fanout_read_unpatched(g_base + rva, actual.data(), n) && std::memcmp(actual.data(), expected, n) == 0;
}

// ---------------------------------------------------------------- lines (network thread)

uint64_t g_last_summary_ms = 0;
uint64_t g_logged_move_seq = 0;
ofo::Snapshot g_prev;

uint64_t observed_ms_now(uint64_t now) {
    uint64_t ms = g_observed_ms.load(std::memory_order_relaxed);
    if (g_trip_s2.load(std::memory_order_acquire)) {
        const uint64_t since = g_trip_since_ms.load(std::memory_order_relaxed);
        if (now > since) ms += now - since;
    }
    return ms;
}

void summary(uint64_t now) {
    const ofo::Snapshot s = ofo::snapshot(g_c);
    const auto fam = ofo::family_totals(s.by_writer);
    const auto& v = s.verdicts;
    auto V = [&](ofo::Verdict x) { return v[static_cast<size_t>(x)]; };
    auto F = [&](ofo::Family f) { return fam[static_cast<size_t>(f)]; };
    const uint64_t would = F(ofo::Family::Sequencer) + F(ofo::Family::Schedule) + F(ofo::Family::Faction);
    const auto unknown = g_c.unknown.rows();
    uint64_t unknown_n = g_c.unknown.overflow();
    for (const auto& [rva, n] : unknown) unknown_n += n;
    log::info("OwnerFanout summary ({}) | ToTarget::vf1 calls {} (+{} in 60 s) | while user 2 observes the host's "
              "field: {} call(s) (+{}), {:.1f} s in {} trip(s), now {} | the host's session {} (ToControlUser to the "
              "host {}), user 2's own {}, another session {}, other finder {}, empty finder {}, no live user 2 {}, stale "
              "trip {}, session 2 already there {}, array too long (unknown) {}, unreadable {} | WOULD ADD user 2: {} "
              "(sequencers {}, schedules {}, faction {}) | not listed: doubtful (missions) {}, excluded {}, other writers "
              "{}, unknown return {}",
              ofo::mode_name(g_mode.load()), s.calls, s.calls - g_prev.calls, s.observing, s.observing - g_prev.observing,
              static_cast<double>(observed_ms_now(now)) / 1000.0, g_trips.load(),
              g_trip_s2.load() ? std::format("OBSERVER (owner 0x{:08X})", g_trip_owner.load()) : "not observing",
              V(ofo::Verdict::HostTarget), V(ofo::Verdict::ControlUserHost), V(ofo::Verdict::ToUser2),
              V(ofo::Verdict::OtherTarget), V(ofo::Verdict::OtherFinder), V(ofo::Verdict::EmptyFinder),
              V(ofo::Verdict::NoUser2), V(ofo::Verdict::StaleTrip), V(ofo::Verdict::AlreadyThere),
              V(ofo::Verdict::ArrayTooLong), V(ofo::Verdict::Unreadable), would, F(ofo::Family::Sequencer),
              F(ofo::Family::Schedule),
              F(ofo::Family::Faction), F(ofo::Family::Doubtful), F(ofo::Family::Excluded), F(ofo::Family::Other),
              unknown_n);
    const auto top = ofo::top_writers(s.by_writer, g_prev.by_writer, kTop);
    if (!top.empty()) {
        std::string t;
        for (const ofo::TopRow& r : top) {
            const ofo::WriterRow& w = ofo::kWriters[r.index];
            t += std::format(" | {} {} [{}] {} (+{})", w.opcode, w.name, ofo::family_name(ofo::family_of(w.opcode)),
                             r.total, r.delta);
        }
        log::info("OwnerFanout top writers to the host's session while user 2 observes (total, +60 s):{}", t);
    }
    if (!unknown.empty()) {
        std::string t;
        for (size_t i = 0; i < unknown.size() && i < 6; ++i) {
            t += std::format(" exe+0x{:X} x{}", unknown[i].first, unknown[i].second);
        }
        log::info("OwnerFanout returns not in the writer table (a writer the scan missed):{}{}", t,
                  g_c.unknown.overflow() ? std::format(" (table full: {} more)", g_c.unknown.overflow()) : "");
    }
    // ToControlUser to the host (the host controls the actor): apart, by writer. None of its known writers is
    // listed in §3.4; a listed opcode here would mean the scan missed a ToControlUser path.
    if (V(ofo::Verdict::ControlUserHost)) {
        std::string t;
        for (const ofo::TopRow& r : ofo::top_writers(s.by_writer_control, g_prev.by_writer_control, kTop)) {
            const ofo::WriterRow& w = ofo::kWriters[r.index];
            t += std::format(" | {} {} [{}] {} (+{})", w.opcode, w.name, ofo::family_name(ofo::family_of(w.opcode)),
                             r.total, r.delta);
        }
        for (const auto& [rva, n] : g_c.unknown_control.rows()) t += std::format(" | return exe+0x{:X} x{}", rva, n);
        log::info("OwnerFanout ToControlUser Acks to the host's session while user 2 observes (total, +60 s):{}", t);
    }
    // The comparison: the hook, the host's client (sniffer), option A's copies.
    const bool sniffer = g_sniffer_fed.load(std::memory_order_relaxed);
    const bool a_on = observer_seq_copy_sends();
    std::string cmp;
    for (const uint16_t op : ofo::compared_opcodes()) {
        const uint64_t hook = ofo::count_of_opcode(s.by_writer, op);
        const uint64_t client = g_client.at(op);
        const bool a = ofo::copied_by_a(op);
        if (!a && !hook && !client) continue;
        cmp += std::format(" | {} hook {} / host client {}{} {}", op, hook, sniffer ? std::to_string(client) : "-",
                           a ? std::format(" / A queued {}", g_extra.at(op)) : "",
                           sniffer ? ofo::agreement_name(ofo::agree(hook, client, 1)) : "(sniffer off)");
    }
    log::info("OwnerFanout compare while observing (hook = Acks for the host's session; host client = what the host's "
              "client got, sniffer; A = observer_seq_copy's copies queued for user 2, snapshot included{}, refused {}):{}",
              a_on ? "" : ", A is not sending", g_extra_refused.load(), cmp.empty() ? " nothing yet" : cmp);
    g_prev = s;
}

}  // namespace

void owner_fanout_configure(const std::string& raw) {
    const ofo::ParsedMode p = ofo::parse_mode(raw);
    if (p.reserved_send) {
        log::warn("OwnerFanout: owner_fanout={} is reserved for step 3 (adding user 2's session) and not implemented "
                  "yet: off; use owner_fanout=probe",
                  raw);
    } else if (p.unknown) {
        log::warn("OwnerFanout: owner_fanout={} is not 0 or probe: off", raw);
    }
    g_mode.store(p.mode);
}

bool owner_fanout_wanted() { return g_mode.load(std::memory_order_relaxed) != ofo::Mode::Off; }
bool owner_fanout_active() { return g_active.load(std::memory_order_relaxed); }

bool owner_fanout_read_unpatched(uintptr_t addr, void* out, size_t n) {
    if (!game::safe_read(addr, out, n)) return false;
    if (!g_patch_known.load(std::memory_order_acquire)) return true;
    const uintptr_t lo = std::max(addr, g_patch_at), hi = std::min(addr + n, g_patch_at + g_patch_len);
    if (lo >= hi) return true;
    // The originals only if the patch range holds exactly OUR jump (ofo::patch_is_ours); anything else (a
    // foreign or corrupt write, a half-written patch) is returned as it is, so the caller's check fails.
    uint8_t live[16] = {}, stub[14] = {};
    if (!game::safe_read(g_patch_at, live, g_patch_len)) return true;
    const std::span<const uint8_t> live_span(live, g_patch_len);
    std::span<const uint8_t> stub_span;
    if (const uint64_t dst = ofo::e9_destination(live_span, g_patch_at);
        dst && dst >= g_tramp_lo && dst + sizeof stub <= g_tramp_hi && game::safe_read(dst, stub, sizeof stub)) {
        stub_span = std::span<const uint8_t>(stub, sizeof stub);
    }
    if (ofo::patch_is_ours(live_span, g_patch_at, g_patch_detour, stub_span, g_tramp_lo, g_tramp_hi)) {
        std::memcpy(static_cast<uint8_t*>(out) + (lo - addr), g_patch_orig + (lo - g_patch_at), hi - lo);
    }
    return true;
}

bool owner_fanout_install(uintptr_t base) {
    if (!owner_fanout_wanted() || g_active.load()) return g_active.load();
    g_base = base;
    std::vector<gad::Section> secs;
    if (!p8_exe_sections(base, secs)) {
        log::error("OwnerFanout: the game's section table is unreadable; REFUSED, owner_fanout off");
        g_mode.store(ofo::Mode::Off);
        return false;
    }
    if (!gad::in_real_code(secs, static_cast<uint32_t>(kVf1), static_cast<uint32_t>(sizeof kVf1Bytes))) {
        log::error("OwnerFanout: ToTarget::vf1 (exe+0x{:X}) is not inside .rsrc; REFUSED, owner_fanout off", kVf1);
        g_mode.store(ofo::Mode::Off);
        return false;
    }
    if (!code_matches(kVf1, kVf1Bytes, sizeof kVf1Bytes)) {
        log::error("OwnerFanout: ToTarget::vf1 (exe+0x{:X}, 95 bytes) is not build 2976's code; REFUSED, owner_fanout "
                   "off",
                   kVf1);
        g_mode.store(ofo::Mode::Off);
        return false;
    }
    const auto s0 = game::read<uintptr_t>(base + kVtToTarget), s1 = game::read<uintptr_t>(base + kVtToTarget + 8),
               s2 = game::read<uintptr_t>(base + kVtToTarget + 16);
    if (!s0 || *s0 != base + kVf0 || !s1 || *s1 != base + kVf1 || !s2 || *s2 != base + kVf2) {
        log::error("OwnerFanout: the ToTarget vtable (exe+0x{:X}) is not build 2976's; REFUSED, owner_fanout off",
                   kVtToTarget);
        g_mode.store(ofo::Mode::Off);
        return false;
    }
    g_vt = base + kVtToTarget;
    if (const auto c1 = game::read<uintptr_t>(base + kVtToControlUser + 8); c1 && *c1 == base + kVf1) {
        g_vt_control = base + kVtToControlUser;
    } else {
        log::warn("OwnerFanout: ToControlUser's vtable (exe+0x{:X}) slot 1 is not vf1: its Acks are not told apart "
                  "(counted as 'other finder')",
                  kVtToControlUser);
    }
    auto whole = [](const std::vector<uint8_t>& o) {
        return std::find(std::begin(kBoundaries), std::end(kBoundaries), o.size()) != std::end(kBoundaries) &&
               o.size() <= sizeof g_patch_orig && std::memcmp(o.data(), kVf1Bytes, o.size()) == 0;
    };
    // P8 S26 (montura-regresion.md section 7.1): refused attempts logged and tried again (bounded), nothing written.
    auto h = game::prepare_inline(base + kVf1, reinterpret_cast<void*>(&to_target_vf1), whole, "OwnerFanout",
                                  "ToTarget::vf1");
    if (!h) {
        log::error("OwnerFanout: the hook on ToTarget::vf1 could not be prepared ({}); REFUSED",
                   game::inline_error_text(h.error()));
        g_mode.store(ofo::Mode::Off);
        return false;
    }
    const auto& orig = h->original_bytes();
    if (!whole(orig)) {
        log::error("OwnerFanout: the hook on ToTarget::vf1 would patch {} bytes, not a whole number of the verified "
                   "instructions; REFUSED, nothing written",
                   orig.size());
        g_mode.store(ofo::Mode::Off);
        return false;  // destroyed disabled: nothing was written
    }
    // What the other modules' byte checks must see, known before anything is written.
    g_patch_at = base + kVf1;
    g_patch_len = orig.size();
    std::memcpy(g_patch_orig, orig.data(), orig.size());
    g_patch_detour = reinterpret_cast<uintptr_t>(h->destination());
    g_tramp_lo = h->trampoline().address();
    g_tramp_hi = g_tramp_lo + h->trampoline().size();
    g_patch_known.store(true, std::memory_order_release);
    g_hook = std::move(*h);  // the detour calls through it: in place before enabling
    if (!g_hook.enable()) {
        log::error("OwnerFanout: the hook on ToTarget::vf1 could not be enabled; REFUSED");
        g_patch_known.store(false, std::memory_order_release);  // nothing of ours there: the live bytes only
        g_hook = {};
        g_mode.store(ofo::Mode::Off);
        return false;
    }
    g_active.store(true);
    log::info("OwnerFanout ON (probe, owner_fanout=probe): inline hook at ToTarget::vf1 exe+0x{:X} (95 bytes verified, "
              "{} patched; vtable exe+0x{:X}); {} writers in the table (return of finder->vf1 -> writer -> opcode) | while "
              "user 2 is on an OBSERVER trip in the host's field (its MoveFieldAck, the server's owner), every Ack for the "
              "host's session is counted by writer: the listed ones (sequencers, schedules, faction) are what step 3 "
              "would add user 2 to | nothing is changed or sent | summary every 60 s",
              kVf1, orig.size(), kVtToTarget, ofo::kWriterCount);
    return true;
}

void owner_fanout_on_move_field(uintptr_t self, bool pushed, uint32_t server_owner) noexcept {
    if (!g_active.load(std::memory_order_relaxed) || !pushed) return;
    const uintptr_t s2 = server_live_session2();
    if (!s2 || self != s2) return;
    const uint32_t user = server_user2_user_key();
    const bool observer = ofo::observer_trip(server_owner, user);
    const uint64_t now = GetTickCount64();
    end_trip(now);
    if (observer) {
        g_trip_owner.store(server_owner, std::memory_order_relaxed);
        g_trip_since_ms.store(now, std::memory_order_relaxed);
        g_trips.fetch_add(1, std::memory_order_relaxed);
        g_trip_s2.store(s2, std::memory_order_release);
    }
    g_move_owner.store(server_owner, std::memory_order_relaxed);
    g_move_user.store(user, std::memory_order_relaxed);
    g_move_observer.store(observer, std::memory_order_relaxed);
    g_move_seq.fetch_add(1, std::memory_order_release);
}

void owner_fanout_on_extra(std::span<const uint8_t> packet, bool queued) noexcept {
    if (!g_active.load(std::memory_order_relaxed) || packet.size() < 2) return;
    if (!g_trip_s2.load(std::memory_order_acquire)) return;
    if (!queued) {
        g_extra_refused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_extra.add(static_cast<uint16_t>(packet[0] | packet[1] << 8));
}

void owner_fanout_sniffed(const uint8_t* packet, size_t size, bool server_to_client) noexcept {
    if (!g_active.load(std::memory_order_relaxed)) return;
    if (!g_sniffer_fed.load(std::memory_order_relaxed)) g_sniffer_fed.store(true, std::memory_order_relaxed);
    if (!server_to_client || size < 2) return;
    if (!g_trip_s2.load(std::memory_order_acquire)) return;
    const uint16_t op = static_cast<uint16_t>(packet[0] | packet[1] << 8);
    const ofo::Family f = ofo::family_of(op);
    if (ofo::listed(f) || f == ofo::Family::Doubtful) g_client.add(op);
}

void owner_fanout_tick() noexcept {
    if (!g_active.load(std::memory_order_relaxed)) return;
    try {
        const uint64_t now = GetTickCount64();
        // User 2 gone (or its goodbye asked for) ends the trip, as observer_seq_copy's tick does.
        if (g_trip_s2.load(std::memory_order_acquire)) {
            uintptr_t user2 = 0;
            if (!server_user2(user2) || !user2 || server_logout_requested()) {
                end_trip(now);
                g_trip_ended_gone.fetch_add(1, std::memory_order_relaxed);
                log::info("OwnerFanout [trip] user 2 gone or leaving: the trip ends (counting stops)");
            }
        }
        if (const uint64_t seq = g_move_seq.load(std::memory_order_acquire); seq != g_logged_move_seq) {
            g_logged_move_seq = seq;
            const bool obs = g_move_observer.load();
            log::info("OwnerFanout [trip] user 2's MoveFieldAck #{}: owner 0x{:08X} as the server wrote it, user 2 "
                      "0x{:08X} -> {}",
                      seq, g_move_owner.load(), g_move_user.load(),
                      obs ? "OBSERVER trip in the host's field: counting the host's Acks"
                          : "its own field (or a key unknown): not counting");
        }
        if (!g_last_summary_ms) g_last_summary_ms = now;
        if (now - g_last_summary_ms >= kSummaryMs) {
            g_last_summary_ms = now;
            summary(now);
        }
    } catch (...) {
    }
}

std::string owner_fanout_describe() {
    if (!g_active.load()) return {};
    const ofo::Snapshot s = ofo::snapshot(g_c);
    const auto fam = ofo::family_totals(s.by_writer);
    return std::format("owner fanout probe: vf1 calls {}, observing {}, host's session {} (would add {}), trips {}, now {}",
                       s.calls, s.observing, s.verdicts[static_cast<size_t>(ofo::Verdict::HostTarget)],
                       fam[0] + fam[1] + fam[2], g_trips.load(), g_trip_s2.load() ? "observer" : "none");
}

}  // namespace cdmp::session2
