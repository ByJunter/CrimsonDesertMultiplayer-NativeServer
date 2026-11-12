#include "mod/session2/housing_redraw.h"
#include "mod/game/build_profile.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <safetyhook.hpp>
#include <windows.h>

namespace cdmp::session2 {
namespace {

// Build 3576 (PE 0x6AED2E85), 7-nov, housing_trace on both copies:
// - 0x4578F0(manager, flags u8, stage key u32, records): the data of a SyncHousingPlacedSceneObjectListAck part (the
//   2733 handler 0xCBF0B0 calls it); flags 1 = first of several, 3 = last, 4 = only part.
// - 0x461A60(manager, stage index): rebuilds the active stage (its bounds from the active list +0xA8, the entry's object
//   count +0x70 to 0, 0x465E20, then 0x45F430 = Mgr32, which places every record with 0x460AA0). The client calls it
//   from 0xA600C0. Approaching the house runs 0x45EB60 -> 0x45F430 the same way.
// - [manager+0xA0]: the active stage index, -1 when none.
constexpr uintptr_t kStore = 0x4578F0, kRebuild = 0x461A60, kWorldSlot = 0x71C0100;
constexpr uintptr_t kActiveStage = 0xA0;

using RebuildFn = void(__fastcall*)(uintptr_t manager, uint32_t stage);

// housing_gimmick_guard: pa::VHousingManager slot 35, 0x462DD0(manager, gimmick actor), the owner's update of a gimmick
// placed by its house (called from 0x9F1181 for every gimmick with a housing sync id, [+0x41C] != -1). With the host's
// house re-keyed to the guest and its gimmick records kept, the guest's client is the owner of gimmicks it never
// spawned (its own spawn requests 2492/2446 are dropped): their elements in the stage entry's list (+0x68) lack what
// the owner's spawn fills in, and slot 35 read them as null (0x46313D [+0x28], 0x4632EA [+0x68]; 7-nov sessions 2 and 4,
// when the host opened the construction mode). The guest skips it: those gimmicks are the host's.
constexpr uintptr_t kMgr35 = 0x462DD0;
constexpr uint8_t kMgr35Head[] = {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x50, 0x10, 0x55, 0x56, 0x57};
SafetyHookInline g_mgr35;
std::atomic<uint64_t> g_mgr35_skipped{0};

uintptr_t __fastcall mgr35_detour(uintptr_t manager, uintptr_t gimmick, uintptr_t, uintptr_t) {
    const uint64_t n = g_mgr35_skipped.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 3 || n % 100 == 0) {
        log::info("HousingGimmickGuard: the owner's update of a housing gimmick (slot 35) skipped on the guest "
                  "(gimmick 0x{:X}, manager 0x{:X}) (#{})", gimmick, manager, n);
    }
    return 0;
}

uintptr_t g_base = 0;
SafetyHookInline g_store;
std::atomic<bool> g_live{true};
std::atomic<uint64_t> g_rebuilds{0}, g_faults{0};

bool rebuild(uintptr_t manager, uint32_t stage) noexcept {
    __try {
        reinterpret_cast<RebuildFn>(g_base + kRebuild)(manager, stage);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t __fastcall store_detour(uintptr_t manager, uint32_t flags, uint32_t stage_key, uintptr_t records) {
    const uintptr_t r = g_store.call<uintptr_t>(manager, flags, stage_key, records);
    const uint8_t f = static_cast<uint8_t>(flags);
    if ((f != 3 && f != 4) || !g_live.load(std::memory_order_relaxed)) return r;
    int32_t active = -1;
    if (!game::fast_read(manager + kActiveStage, &active, sizeof active) || active < 0) return r;
    if (rebuild(manager, static_cast<uint32_t>(active))) {
        const uint64_t n = g_rebuilds.fetch_add(1) + 1;
        log::info("HousingRedraw: house stage {} rebuilt after a 2733 (stage key 0x{:X}, flags {}) (#{})", active,
                  stage_key, f, n);
    } else {
        g_faults.fetch_add(1);
        g_live.store(false);
        log::error("HousingRedraw: the rebuild of stage {} faulted; housing_redraw is off for this session", active);
    }
    return r;
}

bool g_want_redraw = false, g_want_guard = false;

void install_guard() {
    uint8_t head[sizeof kMgr35Head] = {};
    if (!game::safe_read(g_base + kMgr35, head, sizeof head) || std::memcmp(head, kMgr35Head, sizeof head) != 0) {
        log::warn("HousingGimmickGuard: exe+0x{:X} is not the expected code; off", kMgr35);
        return;
    }
    g_mgr35 = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kMgr35), reinterpret_cast<void*>(&mgr35_detour));
    if (!g_mgr35) {
        log::warn("HousingGimmickGuard: cannot hook exe+0x{:X}; off", kMgr35);
        return;
    }
    log::info("HousingGimmickGuard ON (housing_gimmick_guard=1, guest): the owner's update of the house's gimmicks "
              "(slot 35) is skipped here: with the host's house as this client's own, those gimmicks are the host's");
}

DWORD WINAPI install_thread(LPVOID) {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uintptr_t world = 0;
    while (!game::safe_read(g_base + kWorldSlot, &world, sizeof world) || !world) Sleep(500);
    if (g_want_guard) install_guard();
    if (!g_want_redraw) return 0;
    for (const uintptr_t at : {kStore, kRebuild}) {
        uint8_t b[2] = {};
        if (!game::safe_read(g_base + at - 1, b, 2) || b[0] != 0xCC || b[1] == 0xCC) {
            log::warn("HousingRedraw: exe+0x{:X} is not the expected function start; off", at);
            return 0;
        }
    }
    g_store = safetyhook::create_inline(reinterpret_cast<void*>(g_base + kStore), reinterpret_cast<void*>(&store_detour));
    if (!g_store) {
        log::warn("HousingRedraw: cannot hook exe+0x{:X}; off", kStore);
        return 0;
    }
    log::info("HousingRedraw ON (housing_redraw=1, experimental): after the last part of a house list (2733) the active "
              "house stage is rebuilt, so the host's edits show without joining again");
    return 0;
}

}  // namespace

void housing_redraw_start(bool redraw, bool gimmick_guard) {
    if (!redraw && !gimmick_guard) return;
    if (!game::is_3576()) {
        log::warn("HousingRedraw: housing_redraw and housing_gimmick_guard are for build 3576 only; off");
        return;
    }
    g_want_redraw = redraw;
    g_want_guard = gimmick_guard;
    if (HANDLE t = CreateThread(nullptr, 0, install_thread, nullptr, 0, nullptr)) CloseHandle(t);
}

}  // namespace cdmp::session2
