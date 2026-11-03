#include "mod/net/puppet_minimap.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include <safetyhook.hpp>

#include "mod/game/offsets.h"
#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::net {
namespace {
namespace off = game::build_2976;

constexpr uint32_t kPuppetPlayer = 0xA010F001;

SafetyHookInline g_hook;
std::atomic<int> g_offset{0};

// What the hook saw, for the log (written on game threads).
std::atomic<uint64_t> g_calls{0};       // for the puppet
std::atomic<uint32_t> g_last_icon{0xFFFFFFFF};
std::atomic<uint32_t> g_diag_answer{0xFFFFFFFF};  // icon | sub << 16 (diagnostic)
std::atomic<uint32_t> g_diag_flags{0};            // a | b << 8
std::atomic<uint64_t> g_failed{0};      // icon table not readable

uint32_t actor_key_of(const void* comp) {
    __try {
        const auto actor = *reinterpret_cast<const uintptr_t*>(reinterpret_cast<uintptr_t>(comp) + 0x08);
        if (!actor) return 0;
        return *reinterpret_cast<const uint32_t*>(actor + off::kActor_Key);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// cfg = [[[[exe+0x6D69528]]+8]+0x270], a table of u16 UIMapIconKey.
uint16_t icon_from_table(int offset) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    for (uintptr_t root : {off::kMinimapIconRoot, off::kMinimapIconRootAlt}) {
        __try {
            uintptr_t p = *reinterpret_cast<const uintptr_t*>(base + root);
            if (!p) continue;
            p = *reinterpret_cast<const uintptr_t*>(p);
            if (!p) continue;
            p = *reinterpret_cast<const uintptr_t*>(p + 0x08);
            if (!p) continue;
            p = *reinterpret_cast<const uintptr_t*>(p + 0x270);
            if (!p) continue;
            return *reinterpret_cast<const uint16_t*>(p + offset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return 0xFFFF;
}

void __fastcall minimap_icon(void* comp, uint16_t* icon, uint16_t* sub, uint8_t* a, uint8_t* b) {
    const int offset = g_offset.load(std::memory_order_relaxed);
    if (offset == 0 || actor_key_of(comp) != kPuppetPlayer) {
        g_hook.call<void>(comp, icon, sub, a, b);
        return;
    }
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (offset < 0) {  // diagnostic: what does the game decide for it?
        g_hook.call<void>(comp, icon, sub, a, b);
        __try {
            g_diag_answer.store(*icon | static_cast<uint32_t>(*sub) << 16, std::memory_order_relaxed);
            g_diag_flags.store(*a | static_cast<uint32_t>(*b) << 8, std::memory_order_relaxed);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return;
    }
    const uint16_t key = icon_from_table(offset);
    if (key == 0xFFFF) {
        g_failed.fetch_add(1, std::memory_order_relaxed);
        g_hook.call<void>(comp, icon, sub, a, b);
        return;
    }
    // Answer without the original: it would also send HideFlagMinimapActor
    // for an actor it considers hidden.
    __try {
        *icon = key;
        *sub = 0xFFFF;
        *a = 0;
        *b = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_last_icon.store(key, std::memory_order_relaxed);
}

}  // namespace

bool puppet_minimap_install(int table_offset) {
    if (table_offset == 0) return false;
    if (g_hook) {
        puppet_minimap_set_offset(table_offset);
        return true;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    uint8_t head[sizeof off::kMinimapIconDecideHead];
    if (base != off::kImageBase || !game::safe_read(base + off::kMinimapIconDecide, head, sizeof head) ||
        std::memcmp(head, off::kMinimapIconDecideHead, sizeof head) != 0) {
        log::warn("Minimap: icon function not recognised; the other player gets no minimap icon");
        return false;
    }
    puppet_minimap_set_offset(table_offset);
    g_hook = safetyhook::create_inline(reinterpret_cast<void*>(base + off::kMinimapIconDecide),
                                       reinterpret_cast<void*>(minimap_icon));
    log::info("Minimap: icon hook {}", g_hook ? "installed" : "FAILED to install");
    return static_cast<bool>(g_hook);
}

void puppet_minimap_set_offset(int table_offset) {
    if (table_offset > 0 && (table_offset < 0xEA || table_offset > 0x16C || table_offset % 2)) return;
    g_offset = table_offset;
    if (table_offset > 0) {
        log::info("Minimap: other player's icon = table +0x{:X} (key {})", table_offset,
                  icon_from_table(table_offset));
    } else if (table_offset < 0) {
        log::info("Minimap: diagnostic mode (the game decides the other player's icon)");
    }
}

int puppet_minimap_offset() { return g_offset.load(); }

void puppet_minimap_tick() {
    static uint64_t logged_calls = 0, logged_failed = 0;
    static uint32_t logged_answer = 0xFFFFFFFE, logged_flags = 0xFFFFFFFF, logged_icon = 0xFFFFFFFE;
    const uint64_t calls = g_calls.load(std::memory_order_relaxed);
    const uint32_t answer = g_diag_answer.load(std::memory_order_relaxed);
    const uint32_t flags = g_diag_flags.load(std::memory_order_relaxed);
    const uint32_t icon = g_last_icon.load(std::memory_order_relaxed);
    if (calls && !logged_calls) log::info("Minimap: the game asked for the other player's icon");
    if (calls) logged_calls = calls;
    if (answer != 0xFFFFFFFF && (answer != logged_answer || flags != logged_flags)) {
        log::info("Minimap (diagnostic): the game gives the other player icon {} sub {} flags {} {}",
                  answer & 0xFFFF, answer >> 16, flags & 0xFF, flags >> 8);
        logged_answer = answer;
        logged_flags = flags;
    }
    if (icon != 0xFFFFFFFF && icon != logged_icon) {
        log::info("Minimap: other player shown with icon {}", icon);
        logged_icon = icon;
    }
    const uint64_t failed = g_failed.load(std::memory_order_relaxed);
    if (failed != logged_failed) {
        log::info("Minimap: icon table not readable ({} times)", failed);
        logged_failed = failed;
    }
}

}  // namespace cdmp::net
