#pragma once

#include <span>
#include "mod/game/build_profile.h"

namespace cdmp::game::character_profile {

#include "mod/game/character_guard_signatures.inc"

// Build 1.0.0.3576 (static, 2026-11-06; not yet run): the six functions moved whole with the same 32-byte heads
// (kLayer3527.. match byte for byte, each a .pdata function start, each unique near old + shift): layer advance +0xC40,
// layers advance and pending +0xDD0, update / post-update / restore +0x370. The 3527 call chain holds: 0x9453A2 ->
// 0x9496C0, 0x949986 -> layers 0x251AC20, 0x251ADC0 -> layer 0x214F8D0, 0x94A2F5 -> pending 0x251B420; restore reads
// `49 8B 45 40 44 38 70 30` at 0x949F1F; the layers function compares the chart at layer+0x118 (0x251AC92).
inline uintptr_t rva3576(uintptr_t old) noexcept {
    switch (old) {
        case 0x1F63F70: return 0x214F8D0;
        case 0x2329700: return 0x251AC20;
        case 0x85FBA0: return 0x9451E0;
        case 0x860D30: return 0x9463D0;
        case 0x8646D0: return 0x949EB0;
        case 0x2329D70: return 0x251B420;
        default: return 0;
    }
}

inline uintptr_t rva(uintptr_t old) noexcept {
    if (current_build() == Build::v2976) return old;
    if (current_build() == Build::v3576) return rva3576(old);
    if (current_build() != Build::v3527) return 0;
    switch (old) {
        case 0x1F63F70: return 0x214EC90;
        case 0x2329700: return 0x2519E50;
        case 0x85FBA0: return 0x944E70;
        case 0x860D30: return 0x946060;
        case 0x8646D0: return 0x949B40;
        case 0x2329D70: return 0x251A650;
        default: return 0;
    }
}

// 3576's heads are 3527's (see rva3576).
inline std::span<const uint8_t> signature(uintptr_t old, std::span<const uint8_t> previous) noexcept {
    if (current_build() == Build::v2976) return previous;
    if (current_build() != Build::v3527 && current_build() != Build::v3576) return {};
    switch (old) {
        case 0x1F63F70: return kLayer3527;
        case 0x2329700: return kLayers3527;
        case 0x85FBA0: return kUpdate3527;
        case 0x860D30: return kPost3527;
        case 0x8646D0: return kRestore3527;
        case 0x2329D70: return kPending3527;
        default: return {};
    }
}

// 3527 and 3576 (`cmp [rax+0x118],r13` at 0x2519EC2 / 0x251AC92); 2976 0x110.
inline uint32_t layer_chart_offset() noexcept {
    return layout_3527() ? 0x118 : build_2976::kLayer_Chart;
}

inline bool matches(uintptr_t base, uintptr_t old, std::span<const uint8_t> previous) noexcept {
    const auto addr = rva(old);
    const auto bytes = signature(old, previous);
    std::array<uint8_t, 32> actual{};
    return addr && !bytes.empty() && bytes.size() <= actual.size() &&
           safe_read(base + addr, actual.data(), bytes.size()) &&
           std::equal(bytes.begin(), bytes.end(), actual.begin());
}

} // namespace cdmp::game::character_profile
