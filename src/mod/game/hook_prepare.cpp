#include "mod/game/hook_prepare.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <utility>

#include "mod/game/safe_memory.h"
#include "mod/log.h"

namespace cdmp::game {
namespace {

constexpr uint64_t kTwoGb = 0x7FFFFFFFull;  // safetyhook's default max_distance
constexpr uint64_t kGranule = 0x10000;
constexpr size_t kNearWords = 6 + 48 * 5;

std::atomic<int> g_neighbourhood_lines{0};
std::atomic<bool> g_first_prepared{false};  // the map is logged before the first hook of the process (inline or mid)

uintptr_t exe_base() noexcept { return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)); }

uint64_t exe_size() noexcept {
    const uintptr_t base = exe_base();
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!base || !safe_read(base, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        !safe_read(base + dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    return nt.OptionalHeader.SizeOfImage;
}

std::string where(uintptr_t target) {
    const uintptr_t base = exe_base();
    const uint64_t size = exe_size();
    if (base && target >= base && target < base + size) return std::format("exe+0x{:X}", target - base);
    return std::format("0x{:X}", target);
}

const char* alloc_error_name(safetyhook::Allocator::Error e) {
    switch (e) {
        case safetyhook::Allocator::Error::BAD_VIRTUAL_ALLOC: return "VirtualAlloc failed";
        case safetyhook::Allocator::Error::NO_MEMORY_IN_RANGE: return "no memory in range";
    }
    return "?";
}

std::string signed_mb(uint64_t at, uint64_t from) {
    return at >= from ? std::format("+{} MB", (at - from) >> 20) : std::format("-{} MB", (from - at) >> 20);
}

// A 16-byte slot within +-2 GB of the target now (the same request a trampoline makes), freed at once. Its 64 KB block
// stays only while the global allocator lives, i.e. while some hook holds it (review of bf255bb (4)): before the
// first hook of the process it is released again.
std::string near_probe(uintptr_t target) {
    auto a = safetyhook::Allocator::global()->allocate_near({reinterpret_cast<uint8_t*>(target)}, 16);
    if (!a) return std::format("no slot within +-2 GB ({})", alloc_error_name(a.error()));
    return std::format("a slot at 0x{:X} ({})", a->address(), signed_mb(a->address(), target));
}

// What safetyhook prepared, which the caller's verified instructions do not match (review of bf255bb (4): not always
// its fallback; 14 bytes or more is what its 14-byte jump moves when no trampoline memory is within +-2 GB).
std::string prepared_text(const std::vector<uint8_t>& orig) {
    std::string hex;
    for (size_t i = 0; i < orig.size() && i < 24; ++i) hex += std::format("{}{:02X}", i ? " " : "", orig[i]);
    return std::format("safetyhook prepared a patch over {} bytes [{}] that the caller does not accept (not its "
                       "verified whole instructions){}",
                       orig.size(), hex,
                       orig.size() >= 14 ? "; 14 bytes or more: likely its 14-byte fallback (no trampoline memory "
                                           "within +-2 GB)"
                                         : "");
}

void log_neighbourhood(const char* tag, const char* why) {
    if (g_neighbourhood_lines.fetch_add(1, std::memory_order_relaxed) >= kNeighbourhoodLines) return;
    log::info("HookPrepare ({} {}): {}", tag, why, exe_neighbourhood_text());
}

template <class Hook, class Err, class Make, class ErrText>
std::expected<Hook, Err> prepare(uintptr_t target, Make make, ErrText err_text, const AcceptBytes& accept,
                                 const char* tag, const char* what) {
    if (!g_first_prepared.exchange(true)) log_neighbourhood(tag, "first hook prepared in this process");
    std::expected<Hook, Err> h = make();
    for (int n = 1;; ++n) {
        const bool prepared = h.has_value();
        const bool accepted = prepared && (!accept || accept(h->original_bytes()));
        if (prepared && accepted) {
            if (n > 1) log::info("{}: {} ({}): prepared on attempt {} of {}", tag, what, where(target), n, kHookTries);
            return h;
        }
        const std::string reason =
            prepared ? prepared_text(h->original_bytes()) : std::format("not prepared: {}", err_text(h.error()));
        const bool again = hook_try_again(n, prepared, accepted);
        const std::string last = near_search_last_failure_text();
        log::warn("{}: {} ({}): attempt {} of {}: {} | near memory now: {} | {} | {}", tag, what, where(target), n,
                  kHookTries, reason, near_probe(target),
                  last.empty() ? std::string("the allocator's near searches never failed") : last,
                  again ? std::format("again in {} ms (nothing was written)", kHookRetryMs)
                        : std::string("no more attempts (nothing was written)"));
        log_neighbourhood(tag, "after a refused attempt");
        if (!again) return h;
        {
            auto old = std::move(h);  // a disabled hook: freed before the next attempt
        }
        Sleep(kHookRetryMs);
        h = make();
    }
}

}  // namespace

std::string inline_error_text(const safetyhook::InlineHook::Error& e) {
    using E = safetyhook::InlineHook::Error;
    switch (e.type) {
        case E::BAD_ALLOCATION:
            return std::format("BAD_ALLOCATION ({})", alloc_error_name(e.allocator_error));
        case E::FAILED_TO_DECODE_INSTRUCTION:
            return std::format("FAILED_TO_DECODE_INSTRUCTION at {}", where(reinterpret_cast<uintptr_t>(e.ip)));
        case E::SHORT_JUMP_IN_TRAMPOLINE:
            return std::format("SHORT_JUMP_IN_TRAMPOLINE at {}", where(reinterpret_cast<uintptr_t>(e.ip)));
        case E::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE:
            return std::format("IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE at {} (the 14-byte fallback cannot move a "
                               "relative instruction: no trampoline memory within +-2 GB)",
                               where(reinterpret_cast<uintptr_t>(e.ip)));
        case E::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE:
            return std::format("UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE at {}", where(reinterpret_cast<uintptr_t>(e.ip)));
        case E::FAILED_TO_UNPROTECT:
            return std::format("FAILED_TO_UNPROTECT at {}", where(reinterpret_cast<uintptr_t>(e.ip)));
        case E::NOT_ENOUGH_SPACE:
            return std::format("NOT_ENOUGH_SPACE at {}", where(reinterpret_cast<uintptr_t>(e.ip)));
    }
    return std::format("error {}", static_cast<int>(e.type));
}

std::string mid_error_text(const safetyhook::MidHook::Error& e) {
    using E = safetyhook::MidHook::Error;
    switch (e.type) {
        case E::BAD_ALLOCATION: return std::format("BAD_ALLOCATION of the stub ({})", alloc_error_name(e.allocator_error));
        case E::BAD_INLINE_HOOK: return std::format("BAD_INLINE_HOOK: {}", inline_error_text(e.inline_hook_error));
    }
    return std::format("error {}", static_cast<int>(e.type));
}

std::expected<safetyhook::InlineHook, safetyhook::InlineHook::Error> prepare_inline(uintptr_t target, void* detour,
                                                                                    const AcceptBytes& accept,
                                                                                    const char* tag, const char* what) {
    return prepare<safetyhook::InlineHook, safetyhook::InlineHook::Error>(
        target,
        [&] {
            return safetyhook::InlineHook::create(reinterpret_cast<void*>(target), detour,
                                                  safetyhook::InlineHook::StartDisabled);
        },
        [](const safetyhook::InlineHook::Error& e) { return inline_error_text(e); }, accept, tag, what);
}

std::expected<safetyhook::MidHook, safetyhook::MidHook::Error> prepare_mid(uintptr_t target,
                                                                           safetyhook::MidHookFn fn,
                                                                           const AcceptBytes& accept, const char* tag,
                                                                           const char* what) {
    return prepare<safetyhook::MidHook, safetyhook::MidHook::Error>(
        target,
        [&] {
            return safetyhook::MidHook::create(reinterpret_cast<void*>(target), fn, safetyhook::MidHook::StartDisabled);
        },
        [](const safetyhook::MidHook::Error& e) { return mid_error_text(e); }, accept, tag, what);
}

uint64_t near_search_failures() noexcept {
    uint64_t w[1] = {};
    return safetyhook::cdmp_near_last_failure(w, 1) ? w[0] : 0;
}

std::string near_search_last_failure_text() {
    std::vector<uint64_t> w(kNearWords);
    const size_t n = safetyhook::cdmp_near_last_failure(w.data(), w.size());
    if (n < 6 || w[0] == 0) return {};
    std::string s = std::format("the allocator's last failed near search (of {}): target 0x{:X}, window 0x{:X}..0x{:X}, "
                                "{} KB, {} steps:",
                                w[0], w[1], w[2], w[3], w[4] >> 10, w[5]);
    static constexpr const char* kWhat[] = {"not free", "free, VirtualAlloc refused", "free, no 64 KB-aligned room"};
    for (size_t i = 6; i + 5 <= n; i += 5) {
        const uint64_t at = w[i], base = w[i + 1], size = w[i + 2], tried = w[i + 3];
        const auto dir = static_cast<uint8_t>(w[i + 4] >> 8), what = static_cast<uint8_t>(w[i + 4]);
        s += std::format(" [{} 0x{:X} +0x{:X}{} {}{}]", dir ? "above" : "below", at, size,
                         base ? std::format(" (allocation 0x{:X})", base) : std::string(), what < 3 ? kWhat[what] : "?",
                         tried ? std::format(" at 0x{:X}", tried) : std::string());
    }
    if (w[5] > (n - 6) / 5) s += std::format(" (+{} steps not kept)", w[5] - (n - 6) / 5);
    return s;
}

std::string exe_neighbourhood_text() {
    const uintptr_t base = exe_base();
    const uint64_t size = exe_size();
    if (!base || !size) return "the exe's image is unreadable";
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const uint64_t min_a = reinterpret_cast<uint64_t>(si.lpMinimumApplicationAddress);
    const uint64_t max_a = reinterpret_cast<uint64_t>(si.lpMaximumApplicationAddress);
    const uint64_t lo = base > min_a + kTwoGb ? base - kTwoGb : min_a;
    const uint64_t end = base + size;
    const uint64_t hi = std::min<uint64_t>(end + kTwoGb, max_a);
    struct Side {
        uint64_t regions = 0, free_runs = 0, free_bytes = 0, best = 0;
        std::string next;  // the regions right next to the image
        int listed = 0;
    } below, above;
    // One walk upward through [lo, hi): BaseAddress + RegionSize.
    std::vector<MEMORY_BASIC_INFORMATION> last_below;  // the last few regions below the image
    for (uint64_t p = lo; p < hi;) {
        MEMORY_BASIC_INFORMATION m{};
        if (!VirtualQuery(reinterpret_cast<void*>(p), &m, sizeof m)) break;
        const uint64_t rb = reinterpret_cast<uint64_t>(m.BaseAddress), re = rb + m.RegionSize;
        if (re <= p) break;
        const bool is_free = m.State == MEM_FREE;
        if (re <= base) {
            ++below.regions;
            if (is_free) {
                ++below.free_runs;
                below.free_bytes += re - rb;
                // the highest 64 KB-aligned slot of this run
                const uint64_t a = (re - kGranule) & ~(kGranule - 1);
                if (re - rb >= kGranule && a >= rb && a >= lo) below.best = std::max(below.best, a);
            }
            last_below.push_back(m);
            if (last_below.size() > 3) last_below.erase(last_below.begin());
        } else if (rb >= end) {
            ++above.regions;
            if (is_free) {
                ++above.free_runs;
                above.free_bytes += re - rb;
                const uint64_t a = (rb + kGranule - 1) & ~(kGranule - 1);
                if (!above.best && a + kGranule <= re && a < hi) above.best = a;
            }
            if (above.listed < 3) {
                above.next += std::format(" [0x{:X} +0x{:X} {}]", rb, m.RegionSize,
                                          is_free ? "free" : m.State == MEM_RESERVE ? "reserved" : "committed");
                ++above.listed;
            }
        }
        p = re;
    }
    for (const auto& m : last_below) {
        below.next += std::format(" [0x{:X} +0x{:X} {}]", reinterpret_cast<uint64_t>(m.BaseAddress), m.RegionSize,
                                  m.State == MEM_FREE ? "free" : m.State == MEM_RESERVE ? "reserved" : "committed");
    }
    auto side = [&](const Side& s, uint64_t from) {
        return std::format("{} regions, {} free runs ({} MB), nearest free 64 KB slot {}, next to the image:{}",
                           s.regions, s.free_runs, s.free_bytes >> 20,
                           s.best ? std::format("0x{:X} ({})", s.best, signed_mb(s.best, from)) : std::string("NONE"),
                           s.next.empty() ? std::string(" none") : s.next);
    };
    return std::format("address space around the exe 0x{:X}..0x{:X} within +-2 GB: below: {} | above (the image ends "
                       "at 0x{:X}, {}on a 64 KB boundary): {} | near searches failed so far {}",
                       base, end, side(below, base), end, end % kGranule ? "NOT " : "", side(above, end),
                       near_search_failures());
}

}  // namespace cdmp::game
