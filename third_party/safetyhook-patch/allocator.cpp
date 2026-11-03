// cdmp: a copy of safetyhook v0.7.0 src/allocator.cpp (Boost Software License 1.0,
// third_party/safetyhook-patch/LICENSE) with one change in Allocator::allocate_nearby_memory, built in place of
// the original by the project's CMakeLists.txt (which checks the original's SHA-256 first; the diff is
// allocator.cpp.patch next to this file).
// Why (P8 session 26, docs/research/s25/montura-regresion.md sections 5 and 7.1): on the host the first inline hook in the
// game's exe (MountSync's 0x2228260) twice found no trampoline memory within +-2 GB and got the 14-byte fallback,
// so the module refused it and the host never published its mounts. Upstream's search near the target:
//   - forward: asked VirtualAlloc only for the START of each free run; VirtualAlloc rounds a reservation's address
//     down to the allocation granularity, so a run that does not start on a 64 KB boundary (the one right after an
//     image whose size is not a multiple of 64 KB, always: the game's ends at 0x1573AB000) overlapped the
//     reservation below and was skipped whole;
//   - backward: after a refused attempt on a FREE region it went on from that region's AllocationBase, which is 0
//     for free memory, so the search stopped at the first refusal (a race, or a free gap smaller than the size).
// Now both directions try the allocation-granularity boundaries INSIDE each free run, the nearest first:
//   - forward: at most 16 refused VirtualAlloc calls per free run, then the next run;
//   - backward: granule by granule down to the end of the window, with no other bound (2 GB / 64 KB = 32768
//     granules; the review of bf255bb measured a worst case of 32,770 steps in 6.3 ms).
// Neither stops before the +-2 GB window ends. Review of bf255bb (3): a block is taken only when ALL of it is within
// reach of every desired address (its start and its end; upstream checked the start only, so a block starting just
// inside the window could end 64 KB past it and the trampoline's jumps would wrap by 4 GB); the same for a free-list
// reuse. Nothing else changes: the same window, the same order (below the target first), the same blocks and free
// lists. When a search finds nothing, what it examined is kept for the mod's log (cdmp_near_last_failure,
// src/mod/game/hook_prepare.h).
#include <algorithm>
#include <functional>
#include <limits>
#include <cstdint>
#include <mutex>

#include "safetyhook/os.hpp"
#include "safetyhook/utility.hpp"

#include "safetyhook/utility.hpp"

#include "safetyhook/allocator.hpp"

namespace safetyhook {
Allocation::Allocation(Allocation&& other) noexcept {
    *this = std::move(other);
}

Allocation& Allocation::operator=(Allocation&& other) noexcept {
    if (this != &other) {
        free();

        m_allocator = std::move(other.m_allocator);
        m_address = other.m_address;
        m_size = other.m_size;

        other.m_address = nullptr;
        other.m_size = 0;
    }

    return *this;
}

Allocation::~Allocation() {
    free();
}

void Allocation::free() {
    if (m_allocator && m_address != nullptr && m_size != 0) {
        m_allocator->free(m_address, m_size);
        m_address = nullptr;
        m_size = 0;
        m_allocator.reset();
    }
}

Allocation::Allocation(std::shared_ptr<Allocator> allocator, uint8_t* address, size_t size) noexcept
    : m_allocator{std::move(allocator)}, m_address{address}, m_size{size} {
}

std::shared_ptr<Allocator> Allocator::global() {
    static std::weak_ptr<Allocator> global_allocator{};
    static std::mutex global_allocator_mutex{};

    std::scoped_lock lock{global_allocator_mutex};

    if (auto allocator = global_allocator.lock()) {
        return allocator;
    }

    auto allocator = Allocator::create();

    global_allocator = allocator;

    return allocator;
}

std::shared_ptr<Allocator> Allocator::create() {
    return std::shared_ptr<Allocator>{new Allocator{}};
}

std::expected<Allocation, Allocator::Error> Allocator::allocate(size_t size) {
    return allocate_near({}, size, std::numeric_limits<size_t>::max());
}

std::expected<Allocation, Allocator::Error> Allocator::allocate_near(
    const std::vector<uint8_t*>& desired_addresses, size_t size, size_t max_distance) {
    std::scoped_lock lock{m_mutex};
    return internal_allocate_near(desired_addresses, size, max_distance);
}

void Allocator::free(uint8_t* address, size_t size) {
    std::scoped_lock lock{m_mutex};
    return internal_free(address, size);
}

std::expected<Allocation, Allocator::Error> Allocator::internal_allocate_near(
    const std::vector<uint8_t*>& desired_addresses, size_t size, size_t max_distance) {
    // Align to 2 bytes to pass MFP virtual method check
    // See https://itanium-cxx-abi.github.io/cxx-abi/abi.html#member-function-pointers
    size_t aligned_size = align_up(size, 2);

    // First search through our list of allocations for a free block that is large
    // enough.
    for (const auto& allocation : m_memory) {
        if (allocation->size < aligned_size) {
            continue;
        }

        for (auto node = allocation->freelist.get(); node != nullptr; node = node->next.get()) {
            // Enough room?
            if (static_cast<size_t>(node->end - node->start) < aligned_size) {
                continue;
            }

            const auto address = node->start;

            // Close enough? cdmp (review of bf255bb (3)): the whole slot, not only its start.
            if (!in_range(address, desired_addresses, max_distance) ||
                !in_range(address + aligned_size, desired_addresses, max_distance)) {
                continue;
            }

            node->start += aligned_size;

            return Allocation{shared_from_this(), address, size};
        }
    }

    // If we didn't find a free block, we need to allocate a new one.
    auto allocation_size = align_up(aligned_size, system_info().allocation_granularity);
    auto allocation_address = allocate_nearby_memory(desired_addresses, allocation_size, max_distance);

    if (!allocation_address) {
        return std::unexpected{allocation_address.error()};
    }

    auto& allocation = m_memory.emplace_back(new Memory);

    allocation->address = *allocation_address;
    allocation->size = allocation_size;
    allocation->freelist = std::make_unique<FreeNode>();
    allocation->freelist->start = *allocation_address + aligned_size;
    allocation->freelist->end = *allocation_address + allocation_size;

    return Allocation{shared_from_this(), *allocation_address, size};
}

void Allocator::internal_free(uint8_t* address, size_t size) {
    // See internal_allocate_near
    size = align_up(size, 2);

    for (const auto& allocation : m_memory) {
        if (allocation->address > address || allocation->address + allocation->size < address) {
            continue;
        }

        // Find the right place for our new freenode.
        FreeNode* prev{};

        for (auto node = allocation->freelist.get(); node != nullptr; prev = node, node = node->next.get()) {
            if (node->start > address) {
                break;
            }
        }

        // Add new freenode.
        auto free_node = std::make_unique<FreeNode>();

        free_node->start = address;
        free_node->end = address + size;

        if (prev == nullptr) {
            free_node->next.swap(allocation->freelist);
            allocation->freelist.swap(free_node);
        } else {
            free_node->next.swap(prev->next);
            prev->next.swap(free_node);
        }

        combine_adjacent_freenodes(*allocation);
        break;
    }
}

void Allocator::combine_adjacent_freenodes(Memory& memory) {
    for (auto prev = memory.freelist.get(), node = prev; node != nullptr; node = node->next.get()) {
        if (prev->end == node->start) {
            prev->end = node->end;
            prev->next.swap(node->next);
            node->next.reset();
            node = prev;
        } else {
            prev = node;
        }
    }
}

// cdmp: the last near search that found nothing (for the mod's log; cdmp_near_last_failure below). Written under its
// own lock only when a search fails; a search is always called under its Allocator's lock.
namespace {
constexpr size_t kCdmpNearSteps = 48;
struct CdmpNearTrace {
    uint64_t desired = 0, lo = 0, hi = 0, size = 0;
    uint32_t steps = 0;
    uint64_t step[kCdmpNearSteps][5] = {};
    void add(uint8_t dir, uint8_t what, uint8_t* at, const VmBasicInfo& mbi, uint8_t* tried) noexcept {
        if (steps < kCdmpNearSteps) {
            uint64_t* s = step[steps];
            s[0] = reinterpret_cast<uint64_t>(at);
            s[1] = mbi.is_free ? 0 : reinterpret_cast<uint64_t>(mbi.address);
            s[2] = mbi.size;
            s[3] = reinterpret_cast<uint64_t>(tried);
            s[4] = (static_cast<uint64_t>(dir) << 8) | what;
        }
        ++steps;
    }
};
std::mutex g_cdmp_near_mutex;
uint64_t g_cdmp_near_failures = 0;
CdmpNearTrace g_cdmp_near_last;
// Outcomes of one step.
constexpr uint8_t kCdmpNotFree = 0;    // reserved / committed: the search jumped past it
constexpr uint8_t kCdmpRefused = 1;    // free: VirtualAlloc refused the address in `tried`
constexpr uint8_t kCdmpNoRoom = 2;     // free: no 64 KB-aligned room of the size inside it (and in range)
// Failed VirtualAlloc calls tried inside one free run before the search moves on (upstream: one, then it stopped).
constexpr int kCdmpTriesPerRun = 16;
} // namespace

std::expected<uint8_t*, Allocator::Error> Allocator::allocate_nearby_memory(
    const std::vector<uint8_t*>& desired_addresses, size_t size, size_t max_distance) {
    if (desired_addresses.empty()) {
        if (auto result = vm_allocate(nullptr, size, VM_ACCESS_RWX)) {
            return result.value();
        }

        return std::unexpected{Error::BAD_VIRTUAL_ALLOC};
    }

    auto attempt_allocation = [&](uint8_t* p) -> uint8_t* {
        // cdmp (review of bf255bb (3)): the whole block within reach, its end as well as its start.
        if (!in_range(p, desired_addresses, max_distance) || !in_range(p + size, desired_addresses, max_distance)) {
            return nullptr;
        }

        if (auto result = vm_allocate(p, size, VM_ACCESS_RWX)) {
            return result.value();
        }

        return nullptr;
    };

    auto si = system_info();
    auto desired_address = desired_addresses[0];
    auto search_start = si.min_address;
    auto search_end = si.max_address;

    if (static_cast<size_t>(desired_address - search_start) > max_distance) {
        search_start = desired_address - max_distance;
    }

    if (static_cast<size_t>(search_end - desired_address) > max_distance) {
        search_end = desired_address + max_distance;
    }

    search_start = std::max(search_start, si.min_address);
    search_end = std::min(search_end, si.max_address);
    desired_address = align_up(desired_address, si.allocation_granularity);
    VmBasicInfo mbi{};
    const size_t granularity = si.allocation_granularity;
    CdmpNearTrace trace;
    trace.desired = reinterpret_cast<uint64_t>(desired_addresses[0]);
    trace.lo = reinterpret_cast<uint64_t>(search_start);
    trace.hi = reinterpret_cast<uint64_t>(search_end);
    trace.size = size;

    // Search backwards from the desired_address. p is always on an allocation-granularity boundary.
    // cdmp: upstream went on from the region's AllocationBase also after a failed attempt on a FREE region, whose
    // AllocationBase is 0: the search stopped there. Now a free granule that is too small for `size`, or that
    // VirtualAlloc refuses, is followed by the granule below it.
    for (auto p = desired_address; p > search_start && in_range(p, desired_addresses, max_distance);) {
        auto result = vm_query(p);

        if (!result) {
            break;
        }

        mbi = result.value();

        if (!mbi.is_free) {
            trace.add(0, kCdmpNotFree, p, mbi, nullptr);
            auto next = align_down(mbi.address - 1, granularity);
            if (mbi.address == nullptr || next >= p) {
                break;
            }
            p = next;
            continue;
        }

        // mbi.size counts from p upward (the free run above p).
        if (mbi.size >= size) {
            if (auto allocation_address = attempt_allocation(p); allocation_address != nullptr) {
                return allocation_address;
            }
            trace.add(0, kCdmpRefused, p, mbi, p);
        } else {
            trace.add(0, kCdmpNoRoom, p, mbi, nullptr);
        }

        if (reinterpret_cast<uintptr_t>(p) < granularity) {
            break;
        }
        p -= granularity;
    }

    // Search forwards from the desired_address.
    // cdmp: upstream asked VirtualAlloc only for the START of each free run, which it rounds down to the
    // allocation granularity: a run that does not start on a 64 KB boundary (the one right after an image whose
    // size is not a multiple of 64 KB, always) overlapped the reservation below and was skipped whole. Now the
    // first granule boundary inside the run (and the next ones, while VirtualAlloc refuses, a bounded number of
    // times) is tried.
    for (auto p = desired_address; p < search_end && in_range(p, desired_addresses, max_distance);) {
        auto result = vm_query(p);

        if (!result) {
            break;
        }

        mbi = result.value();
        auto run_end = p + mbi.size;  // RegionSize counts from p's page: the end of this run

        if (mbi.is_free) {
            int tries = 0;
            for (auto a = align_up(p, granularity);
                 a >= p && a + size <= run_end && a < search_end && in_range(a, desired_addresses, max_distance) &&
                 in_range(a + size, desired_addresses, max_distance);
                 a += granularity) {
                if (auto allocation_address = attempt_allocation(a); allocation_address != nullptr) {
                    return allocation_address;
                }
                trace.add(1, kCdmpRefused, p, mbi, a);
                if (++tries >= kCdmpTriesPerRun) {
                    break;
                }
            }
            if (tries == 0) {
                trace.add(1, kCdmpNoRoom, p, mbi, nullptr);
            }
        } else {
            trace.add(1, kCdmpNotFree, p, mbi, nullptr);
        }

        if (run_end <= p) {
            break;
        }
        p = run_end;
    }

    {
        std::scoped_lock lock{g_cdmp_near_mutex};
        ++g_cdmp_near_failures;
        g_cdmp_near_last = trace;
    }
    return std::unexpected{Error::NO_MEMORY_IN_RANGE};
}

// cdmp: the last near search that found nothing (src/mod/game/hook_prepare.h). out[0] failed searches so far,
// out[1] the first desired address of the last one, out[2] / out[3] its window, out[4] the size it asked for, out[5]
// the steps it took (only the first 48 kept), then 5 words per kept step: the address queried, the region's
// AllocationBase (0 = free), RegionSize from that address, the address VirtualAlloc was asked for (0 = none),
// (direction << 8) | outcome (direction 0 below the target, 1 above; outcome 0 not free, 1 VirtualAlloc refused,
// 2 free but no 64 KB-aligned room of the size in range). Returns the words written (at most n).
size_t cdmp_near_last_failure(uint64_t* out, size_t n) noexcept {
    if (out == nullptr || n == 0) {
        return 0;
    }
    std::scoped_lock lock{g_cdmp_near_mutex};
    const CdmpNearTrace& t = g_cdmp_near_last;
    size_t w = 0;
    auto put = [&](uint64_t v) {
        if (w < n) {
            out[w++] = v;
        }
    };
    put(g_cdmp_near_failures);
    put(t.desired);
    put(t.lo);
    put(t.hi);
    put(t.size);
    put(t.steps);
    const uint32_t kept = t.steps < kCdmpNearSteps ? t.steps : static_cast<uint32_t>(kCdmpNearSteps);
    for (uint32_t i = 0; i < kept; ++i) {
        for (int k = 0; k < 5; ++k) {
            put(t.step[i][k]);
        }
    }
    return w;
}

bool Allocator::in_range(uint8_t* address, const std::vector<uint8_t*>& desired_addresses, size_t max_distance) {
    return std::all_of(desired_addresses.begin(), desired_addresses.end(), [&](const auto& desired_address) {
        const size_t delta = (address > desired_address) ? address - desired_address : desired_address - address;
        return delta <= max_distance;
    });
}

Allocator::Memory::~Memory() {
    vm_free(address);
}
} // namespace safetyhook