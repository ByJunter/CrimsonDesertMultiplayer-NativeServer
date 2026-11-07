#include "mod/session2/seq_marker.h"

#include <windows.h>

#include <safetyhook.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

#include "mod/game/build_profile.h"
#include "mod/game/hook_prepare.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include "mod/session2/guest.h"
#include "mod/session2/seq_marker_logic.h"
#include "mod/session2/server.h"

namespace cdmp::session2 {
namespace {

constexpr DWORD kLookMs = 50;
constexpr int kDetailedRestores = 20;
constexpr uint64_t kSummaryMs = 60'000;
// Build 1.0.0.3527: pa::ClientSequencerStageManager's vtable and the fields read here (guest.cpp read_preload_facts).
constexpr uintptr_t kVtManager3527 = 0x59572E0;
// Build 1.0.0.3576 (static, 2026-11-06; not yet run): the same class's vtable by RTTI (vtable_map_3549_3576.json, 41
// slots as on 3527). The marker is still +0x259: the load machine writes 0 / 1 / 2 there at 0x856DB7 / 0x85741E /
// 0xC9CFBB and 0xBA340B compares it with 2 (3527's sites + 0x250 / + 0x250 / + 0x420 / in 0xBA2F70 + 0x420), and the
// machine's P+0x15C still takes 0x11 (0x8562D3) and 0x18 (0x8565ED, 0x859754). World slot 0x71C0100.
constexpr uintptr_t kVtManager3576 = 0x5959438;
constexpr uintptr_t kWorldSlot3576 = 0x71C0100;
constexpr uintptr_t kMarker = 0x259;

std::atomic<bool> g_started{false};
bool g_guest = false;
uintptr_t g_vt_manager = kVtManager3527;  // set by marker_thread before the first read

// ---------------------------------------------------------------- seq_marker_trace=1: who calls the manager's vf+0x20
//
// Reached only through the `jmp` at 0xB9FD80, slot 4 of the manager's vtable (the same thunk on both builds). It stores
// the field in +0x20 and leaves the marker at 1.
// 3576 (static): slot 4 of 0x5959438 is the thunk 0xBA01A0 (3527's 0xB9FD80 + 0x420), jmp 0x9AC7F90 in the protected
// section; there the same 15-byte head, `mov [rbp+0x20],rbx` and `mov byte [rbp+0x259],1` (0x9AC7FD0 / 0x9AC8034).
constexpr uintptr_t kBind3527 = 0x9A7F860, kBind3549 = 0x99BADC0, kBind3576 = 0x9AC7F90;
constexpr uint8_t kBindHead[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20};
constexpr int kTraceCalls = 40;
safetyhook::InlineHook g_bind_hook;
std::atomic<int> g_bind_calls{0};
bool g_trace = false;
uintptr_t g_base = 0;

void* __fastcall on_bind(uintptr_t manager, void* result, uintptr_t field) {
    const int n = g_bind_calls.fetch_add(1, std::memory_order_relaxed);
    uint8_t before = 0xFF;
    game::safe_read(manager + kMarker, &before, sizeof before);
    if (n < kTraceCalls) {
        void* frames[14]{};
        const USHORT count = RtlCaptureStackBackTrace(1, 14, frames, nullptr);
        std::string stack;
        for (USHORT i = 0; i < count; ++i) {
            const auto a = reinterpret_cast<uintptr_t>(frames[i]);
            stack += a >= g_base && a < g_base + 0x20000000 ? std::format(" exe+0x{:X}", a - g_base)
                                                            : std::format(" 0x{:X}", a);
        }
        log::info("SeqMarker trace: bind #{} manager 0x{:X} field 0x{:X} marker {} thread {} |{}", n + 1, manager, field,
                  before, GetCurrentThreadId(), stack);
    }
    return g_bind_hook.fastcall<void*>(manager, result, field);
}

void install_trace(uintptr_t base) {
    g_base = base;
    const uintptr_t bind = game::is_3576() ? kBind3576 : game::is_3549() ? kBind3549 : kBind3527;
    uint8_t head[sizeof kBindHead]{};
    if (!game::safe_read(base + bind, head, sizeof head) || std::memcmp(head, kBindHead, sizeof head) != 0) {
        log::warn("SeqMarker trace: exe+0x{:X} is not the expected code; no trace", bind);
        return;
    }
    auto h = game::prepare_inline(base + bind, reinterpret_cast<void*>(&on_bind), {}, "SeqMarker", "manager bind");
    if (!h) {
        log::warn("SeqMarker trace: no hook: {}", game::inline_error_text(h.error()));
        return;
    }
    g_bind_hook = std::move(*h);
    if (!g_bind_hook.enable()) {
        log::warn("SeqMarker trace: the hook could not be enabled");
        return;
    }
    log::info("SeqMarker trace ON at exe+0x{:X}", bind);
}

template <class T>
bool peek(uintptr_t address, T& out) noexcept {
    return game::safe_read(address, &out, sizeof out);
}

bool store_byte(uintptr_t address, uint8_t value) noexcept {
    __try {
        *reinterpret_cast<volatile uint8_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

smk::Facts read_facts(uintptr_t base, uintptr_t world_system) noexcept {
    smk::Facts f;
    uintptr_t world = 0, loader = 0, manager = 0, actor = 0, components = 0, component = 0, sequencer = 0, vt = 0;
    if (!peek(base + world_system, world) || !world) return f;
    if (!peek(world + 0x60, loader) || !loader || !peek(loader + 0x15C, f.state)) return f;
    if (!peek(world + 0x30, manager) || !manager) return f;
    if (!peek(manager + 0x50, actor) || !actor) return f;
    if (!peek(actor + 0x68, components) || !components) return f;
    if (!peek(components + 0xA8, component) || !component) return f;
    if (!peek(component + 0x18, sequencer) || !sequencer) return f;
    if (!peek(sequencer, vt) || vt != base + g_vt_manager) return f;
    if (!peek(sequencer + kMarker, f.marker)) return f;
    f.manager = sequencer;
    f.readable = true;
    return f;
}

DWORD WINAPI marker_thread(void*) {
    const bool b3576 = game::current_build() == game::Build::v3576;
    const auto* profile = game::reader_profile();
    if (!b3576 && (!profile || game::current_build() != game::Build::v3527)) {
        log::info("SeqMarker: only for builds 3527, 3549 and 3576, off");
        return 0;
    }
    // 3576: its own vtable and world slot (reader_profile() may have no 3576 entry yet).
    const uintptr_t world_slot = b3576 ? kWorldSlot3576 : profile->world_system;
    g_vt_manager = b3576 ? kVtManager3576 : kVtManager3527;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    log::info("SeqMarker ON ({}): the own sequencer manager's load marker is put back if it falls with no load running",
              g_guest ? "guest" : "host");
    if (g_trace) install_trace(base);
    smk::Guard guard;
    uint64_t restores = 0, failed = 0, summarized = 0, next_summary = GetTickCount64() + kSummaryMs;
    for (;;) {
        Sleep(kLookMs);
        smk::Facts f = read_facts(base, world_slot);
        f.live = g_guest ? guest_remote_live() : server_user2_present();
        const smk::Decision d = guard.look(f);
        if (d.restore) {
            // Again, right before the write: the trip may have moved on since the look.
            const smk::Facts now = read_facts(base, world_slot);
            const bool still = now.readable && now.manager == f.manager && now.marker == smk::kMarkerFallen &&
                               (now.state == smk::kIdleState || now.state == smk::kWaitState);
            if (still) {
                const bool ok = store_byte(now.manager + kMarker, d.value);
                ok ? ++restores : ++failed;
                if (restores + failed <= kDetailedRestores) {
                    log::warn("SeqMarker: the own manager 0x{:X} fell to marker {} in load state 0x{:X} (another player "
                              "{}): put back to {}{}",
                              now.manager, now.marker, now.state, f.live ? "connected" : "gone", d.value,
                              ok ? "" : " FAULTED");
                }
            }
        }
        const uint64_t tick = GetTickCount64();
        if (tick >= next_summary) {
            next_summary = tick + kSummaryMs;
            if (restores != summarized) {
                summarized = restores;
                log::info("SeqMarker: {} put back so far, {} faulted", restores, failed);
            }
        }
    }
}

}  // namespace

void seq_marker_start(bool enabled, bool guest, bool trace) {
    if (!enabled || g_started.exchange(true)) return;
    g_guest = guest;
    g_trace = trace;
    CloseHandle(CreateThread(nullptr, 0, marker_thread, nullptr, 0, nullptr));
}

}  // namespace cdmp::session2
