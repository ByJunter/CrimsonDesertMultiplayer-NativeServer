// P8 session 26 (docs/research/s25/montura-regresion.md sections 5 and 7.1): safetyhook's search for trampoline
// memory near a target, as the project builds it (third_party/safetyhook-patch/allocator.cpp), against upstream
// v0.7.0's own logic (copied below, Boost Software License 1.0, third_party/safetyhook-patch/LICENSE), on synthetic
// address-space layouts made in THIS process with MEM_RESERVE (address space only, nothing committed but the
// allocations the searches make, all released). Never the game.
//   L0 a free granule right below the "image": both take it (no change of behaviour in the normal case);
//   L1 the 2 GB below reserved, the "image" ending off a 64 KB boundary, free above it (the host's exe ends at
//      0x1573AB000): upstream finds nothing, the patch takes the first 64 KB boundary inside the free run above;
//   L2 above reserved; below, a free 64 KB gap too small for a 128 KB block, then a reservation, then a free run:
//      upstream stops at the gap, the patch goes on down to the free run;
//   L3 everything within +-2 GB reserved: both find nothing, and the patch keeps what it examined
//      (cdmp_near_last_failure);
//   L4 (review of bf255bb (3)) the only free slot starts 4 bytes inside +2 GB of the target: upstream takes it (its
//      end is out of reach: the trampoline's jumps would wrap), the patch refuses it.

#include <windows.h>

#include <safetyhook.hpp>
#include <safetyhook/os.hpp>
#include <safetyhook/utility.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "mod/game/hook_prepare.h"

namespace {

int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

constexpr uint64_t kG = 0x10000;  // allocation granularity on x64 Windows (checked in main)
constexpr uint64_t kMB = 1ull << 20;
constexpr uint64_t k2GB = 1ull << 31;

// Upstream v0.7.0 Allocator::allocate_nearby_memory (src/allocator.cpp), the near branch, verbatim but for the names.
uint8_t* upstream_nearby(const std::vector<uint8_t*>& desired_addresses, size_t size,
                         size_t max_distance = 0x7FFF'FFFF) {
    using namespace safetyhook;
    auto in_range = [&](uint8_t* address) {
        return std::all_of(desired_addresses.begin(), desired_addresses.end(), [&](const auto& desired_address) {
            const size_t delta = (address > desired_address) ? address - desired_address : desired_address - address;
            return delta <= max_distance;
        });
    };
    auto attempt_allocation = [&](uint8_t* p) -> uint8_t* {
        if (!in_range(p)) return nullptr;
        if (auto result = vm_allocate(p, size, VM_ACCESS_RWX)) return result.value();
        return nullptr;
    };
    auto si = system_info();
    auto desired_address = desired_addresses[0];
    auto search_start = si.min_address;
    auto search_end = si.max_address;
    if (static_cast<size_t>(desired_address - search_start) > max_distance) search_start = desired_address - max_distance;
    if (static_cast<size_t>(search_end - desired_address) > max_distance) search_end = desired_address + max_distance;
    search_start = std::max(search_start, si.min_address);
    search_end = std::min(search_end, si.max_address);
    desired_address = align_up(desired_address, si.allocation_granularity);
    VmBasicInfo mbi{};
    for (auto p = desired_address; p > search_start && in_range(p);
         p = align_down(mbi.address - 1, si.allocation_granularity)) {
        auto result = vm_query(p);
        if (!result) break;
        mbi = result.value();
        if (!mbi.is_free) continue;
        if (auto allocation_address = attempt_allocation(p); allocation_address != nullptr) return allocation_address;
    }
    for (auto p = desired_address; p < search_end && in_range(p); p += mbi.size) {
        auto result = vm_query(p);
        if (!result) break;
        mbi = result.value();
        if (!mbi.is_free) continue;
        if (auto allocation_address = attempt_allocation(p); allocation_address != nullptr) return allocation_address;
    }
    return nullptr;
}

// A free run of at least `size` high in this process' address space, 64 KB aligned.
uint64_t free_span(uint64_t size) {
    for (uint64_t p = 0x0000100000000000ull; p < 0x00007FF000000000ull;) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof m)) break;
        const uint64_t b = reinterpret_cast<uint64_t>(m.BaseAddress), e = b + m.RegionSize;
        const uint64_t a = (b + kG - 1) & ~(kG - 1);
        if (m.State == MEM_FREE && e > a && e - a >= size) return a;
        p = e;
    }
    return 0;
}

struct Reservations {
    std::vector<void*> r;
    bool add(uint64_t at, uint64_t size) {
        void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_RESERVE, PAGE_NOACCESS);
        if (p != reinterpret_cast<void*>(at)) {
            std::printf("  could not reserve 0x%llX +0x%llX (got %p, error %lu)\n", static_cast<unsigned long long>(at),
                        static_cast<unsigned long long>(size), p, GetLastError());
            if (p) VirtualFree(p, 0, MEM_RELEASE);
            return false;
        }
        r.push_back(p);
        return true;
    }
    ~Reservations() {
        for (void* p : r) VirtualFree(p, 0, MEM_RELEASE);
    }
};

void release(uint8_t* p) {
    if (p) VirtualFree(p, 0, MEM_RELEASE);
}

uint64_t near_failures() {
    uint64_t w[1] = {};
    return safetyhook::cdmp_near_last_failure(w, 1) ? w[0] : 0;
}

// The project's allocator (a fresh one, freed with its blocks at the end of the call).
uint64_t patched_near(uint64_t desired, size_t size) {
    auto alloc = safetyhook::Allocator::create();
    auto a = alloc->allocate_near({reinterpret_cast<uint8_t*>(desired)}, size);
    return a ? a->address() : 0;
}

void test_l0_normal(uint64_t base) {
    std::printf("L0: a free granule right below the image\n");
    Reservations rs;
    const uint64_t i0 = base + 1024 * kMB;
    if (!rs.add(i0, 16 * kMB)) {
        ++g_failures;
        return;
    }
    const uint64_t desired = i0 + 0x8000;
    uint8_t* up = upstream_nearby({reinterpret_cast<uint8_t*>(desired)}, kG);
    CHECK(reinterpret_cast<uint64_t>(up) == i0 - kG);
    release(up);
    CHECK(patched_near(desired, 32) == i0 - kG);
}

void test_l1_image_end_unaligned(uint64_t base) {
    std::printf("L1: the 2 GB below reserved, the image ending off a 64 KB boundary\n");
    Reservations rs;
    const uint64_t s = base + kG;
    const uint64_t i0 = s + k2GB + 2 * kG;
    const uint64_t i1 = i0 + 16 * kMB + 0x3000;  // like 0x140000000 + 0x173AB000
    if (!rs.add(s, i0 - s) || !rs.add(i0, i1 - i0)) {
        ++g_failures;
        return;
    }
    MEMORY_BASIC_INFORMATION m{};
    CHECK(VirtualQuery(reinterpret_cast<void*>(i1), &m, sizeof m) && m.State == MEM_FREE);
    const uint64_t desired = i0 + 0x8000;
    uint8_t* up = upstream_nearby({reinterpret_cast<uint8_t*>(desired)}, kG);
    CHECK(up == nullptr);  // upstream: VirtualAlloc(0x...3000) rounds down into the image; the run is skipped whole
    release(up);
    const uint64_t want = (i1 + kG - 1) & ~(kG - 1);
    const uint64_t got = patched_near(desired, 32);
    std::printf("  upstream %p, patched 0x%llX (want 0x%llX)\n", static_cast<void*>(up),
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(want));
    CHECK(got == want);
    // The hook's own case: two desired addresses (a jmp rel32 thunk and its target), both in the image.
    auto alloc = safetyhook::Allocator::create();
    auto two = alloc->allocate_near({reinterpret_cast<uint8_t*>(i0 + 0x1000), reinterpret_cast<uint8_t*>(i0 + 0xF00000)},
                                    40);
    CHECK(two && two->address() == want);
}

void test_l2_small_gap_below(uint64_t base) {
    std::printf("L2: above reserved; below a 64 KB gap, a reservation, then a free run (a 128 KB block)\n");
    Reservations rs;
    const uint64_t i0 = base + k2GB + 4 * kMB;
    const uint64_t g = i0 - kG;            // G = [g, i0) free, 64 KB
    const uint64_t r2 = g - kMB;           // R2 = [r2, g) reserved
    const uint64_t f = r2 - 256 * 1024;    // F = [f, r2) free, 256 KB
    if (!rs.add(base + kG, f - (base + kG)) || !rs.add(r2, g - r2) || !rs.add(i0, 16 * kMB) ||
        !rs.add(i0 + 16 * kMB, k2GB + 2 * kG)) {
        ++g_failures;
        return;
    }
    const uint64_t desired = i0 + 0x8000;
    const size_t block = 2 * kG;  // allocate_near(100 KB) asks allocate_nearby_memory for 128 KB
    uint8_t* up = upstream_nearby({reinterpret_cast<uint8_t*>(desired)}, block);
    CHECK(up == nullptr);  // upstream: refused at G, then went on from G's AllocationBase (0) and stopped
    release(up);
    const uint64_t got = patched_near(desired, 100 * 1024);
    std::printf("  upstream %p, patched 0x%llX (want 0x%llX)\n", static_cast<void*>(up),
                static_cast<unsigned long long>(got), static_cast<unsigned long long>(r2 - block));
    CHECK(got == r2 - block);
}

void test_l3_nothing(uint64_t base) {
    std::printf("L3: everything within +-2 GB reserved\n");
    Reservations rs;
    const uint64_t i0 = base + kG + k2GB + 2 * kG;
    if (!rs.add(base + kG, i0 - (base + kG)) || !rs.add(i0, 16 * kMB) || !rs.add(i0 + 16 * kMB, k2GB + 2 * kG)) {
        ++g_failures;
        return;
    }
    const uint64_t desired = i0 + 0x8000;
    uint8_t* up = upstream_nearby({reinterpret_cast<uint8_t*>(desired)}, kG);
    CHECK(up == nullptr);
    release(up);
    const uint64_t before = near_failures();
    CHECK(patched_near(desired, 32) == 0);
    CHECK(near_failures() == before + 1);
    std::vector<uint64_t> w(6 + 48 * 5);
    const size_t n = safetyhook::cdmp_near_last_failure(w.data(), w.size());
    CHECK(n >= 6 + 5 && w[1] == desired && w[5] >= 2 && w[4] == kG);
    // Its steps: the image and the reservations around it, all "not free".
    bool below = false, above = false;
    for (size_t i = 6; i + 5 <= n; i += 5) {
        const auto dir = static_cast<uint8_t>(w[i + 4] >> 8), what = static_cast<uint8_t>(w[i + 4]);
        CHECK(what == 0);
        below = below || dir == 0;
        above = above || dir == 1;
    }
    CHECK(below && above);
    const std::string text = cdmp::game::near_search_last_failure_text();
    std::printf("  %s\n", text.c_str());
    CHECK(text.find("not free") != std::string::npos);
}

void test_l4_block_end_out_of_reach(uint64_t base) {
    std::printf("L4: the only free slot starts just inside +2 GB and ends past it (review of bf255bb (3))\n");
    // The target is 4 bytes past a 64 KB boundary; the only free 64 KB slot is S = T + 0x7FFFFFFC: its start is in
    // reach (in_range), its end 64 KB past it. A trampoline there would make the E9 jumps wrap by 4 GB.
    Reservations rs;
    const uint64_t i0 = base + kG + k2GB + 2 * kG;
    const uint64_t target = i0 + 4;
    const uint64_t s = target + 0x7FFFFFFCull;  // == i0 + 2 GB, 64 KB aligned
    if (!rs.add(base + kG, i0 - (base + kG)) || !rs.add(i0, 16 * kMB) || !rs.add(i0 + 16 * kMB, s - (i0 + 16 * kMB)) ||
        !rs.add(s + 2 * kG, 4 * kG)) {
        ++g_failures;
        return;
    }
    CHECK(s % kG == 0);
    uint8_t* up = upstream_nearby({reinterpret_cast<uint8_t*>(target)}, kG);
    std::printf("  upstream %p (S = 0x%llX: upstream checks the start only)\n", static_cast<void*>(up),
                static_cast<unsigned long long>(s));
    CHECK(reinterpret_cast<uint64_t>(up) == s);
    release(up);
    const uint64_t got = patched_near(target, 32);
    std::printf("  patched 0x%llX (want none)\n", static_cast<unsigned long long>(got));
    CHECK(got == 0);
    // A target 64 KB higher: the whole of S is within reach now, and the patch takes it.
    const uint64_t higher = s + kG - 0x7FFFFFFFull;
    const uint64_t got2 = patched_near(higher, 32);
    std::printf("  patched for a target 64 KB higher 0x%llX (want S)\n", static_cast<unsigned long long>(got2));
    CHECK(got2 == s);
}

void test_retry_rule() {
    using cdmp::game::hook_try_again;
    using cdmp::game::kHookTries;
    static_assert(kHookTries == 3);
    CHECK(!hook_try_again(1, true, true));   // prepared and accepted: kept
    CHECK(hook_try_again(1, true, false));   // the 14-byte fallback: again
    CHECK(hook_try_again(2, false, false));  // not prepared: again
    CHECK(!hook_try_again(3, false, false)); // bounded
    CHECK(!hook_try_again(3, true, false));
}

}  // namespace

int main() {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    if (si.dwAllocationGranularity != kG) {
        std::printf("allocation granularity %lu, not 64 KB: the layouts below assume it\n", si.dwAllocationGranularity);
        return 1;
    }
    test_retry_rule();
    const uint64_t base = free_span(3 * k2GB);  // 6 GB of free address space
    if (!base) {
        std::printf("no 6 GB free run found in this process\n");
        return 1;
    }
    std::printf("free run at 0x%llX\n", static_cast<unsigned long long>(base));
    test_l0_normal(base);
    test_l1_image_end_unaligned(base);
    test_l2_small_gap_below(base);
    test_l3_nothing(base);
    test_l4_block_end_out_of_reach(base);
    if (g_failures) {
        std::printf("hook near tests: %d FAILED\n", g_failures);
        return 1;
    }
    std::printf("hook near tests: all passed\n");
    return 0;
}
