// cdmp: a copy of safetyhook v0.7.0 src/os.windows.cpp (Boost Software License 1.0,
// third_party/safetyhook-patch/LICENSE) with one change in the trap handler, built in place of
// the original by the project's CMakeLists.txt (which checks the original's SHA-256 first).
// Why (P8 review of 10-10, I4, docs/research/pendientes/p8-revision-noche.md): upstream keeps
// every page it ever patched as a "trap page" for good and answers EVERY access violation whose
// address falls on one with EXCEPTION_CONTINUE_EXECUTION. A read/write fault there (an integrity
// check of the protector, a re-protection) would retry forever: a thread spinning in the
// exception dispatcher, "no frames" without a crash. trap_threads only ever makes those pages
// non-executable (PAGE_READWRITE) while it writes, so the only faults it causes are EXECUTE
// faults. Now:
//   - a read/write fault is never retried here (EXCEPTION_CONTINUE_SEARCH, decided without the
//     lock; counted when the lock is free);
//   - an execute fault inside the patched bytes is redirected as upstream (fix_ip), unless that
//     would retry the same address on a page that is not executable while no patch is in progress;
//   - an execute fault elsewhere on a trap page is retried only while a trap_threads is in
//     progress or if the page is executable again (a fault raised during the patch, handled
//     after it); otherwise it is passed on.
//   - counters for the mod's diagnostics: cdmp_trap_stats().
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

#include "safetyhook/common.hpp"
#include "safetyhook/utility.hpp"

#if SAFETYHOOK_OS_WINDOWS

#define NOMINMAX
#if __has_include(<Windows.h>)
#include <Windows.h>
#elif __has_include(<windows.h>)
#include <windows.h>
#else
#error "Windows.h not found"
#endif

#include "safetyhook/os.hpp"

namespace safetyhook {
std::expected<uint8_t*, OsError> vm_allocate(uint8_t* address, size_t size, VmAccess access) {
    DWORD protect = 0;

    if (access == VM_ACCESS_R) {
        protect = PAGE_READONLY;
    } else if (access == VM_ACCESS_RW) {
        protect = PAGE_READWRITE;
    } else if (access == VM_ACCESS_RX) {
        protect = PAGE_EXECUTE_READ;
    } else if (access == VM_ACCESS_RWX) {
        protect = PAGE_EXECUTE_READWRITE;
    } else {
        return std::unexpected{OsError::FAILED_TO_ALLOCATE};
    }

    auto* result = VirtualAlloc(address, size, MEM_COMMIT | MEM_RESERVE, protect);

    if (result == nullptr) {
        return std::unexpected{OsError::FAILED_TO_ALLOCATE};
    }

    return static_cast<uint8_t*>(result);
}

void vm_free(uint8_t* address) {
    VirtualFree(address, 0, MEM_RELEASE);
}

std::expected<uint32_t, OsError> vm_protect(uint8_t* address, size_t size, VmAccess access) {
    DWORD protect = 0;

    if (access == VM_ACCESS_R) {
        protect = PAGE_READONLY;
    } else if (access == VM_ACCESS_RW) {
        protect = PAGE_READWRITE;
    } else if (access == VM_ACCESS_RX) {
        protect = PAGE_EXECUTE_READ;
    } else if (access == VM_ACCESS_RWX) {
        protect = PAGE_EXECUTE_READWRITE;
    } else {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    return vm_protect(address, size, protect);
}

std::expected<uint32_t, OsError> vm_protect(uint8_t* address, size_t size, uint32_t protect) {
    DWORD old_protect = 0;

    if (VirtualProtect(address, size, protect, &old_protect) == FALSE) {
        return std::unexpected{OsError::FAILED_TO_PROTECT};
    }

    return old_protect;
}

std::expected<VmBasicInfo, OsError> vm_query(uint8_t* address) {
    MEMORY_BASIC_INFORMATION mbi{};
    auto result = VirtualQuery(address, &mbi, sizeof(mbi));

    if (result == 0) {
        return std::unexpected{OsError::FAILED_TO_QUERY};
    }

    VmAccess access{};
    access.read = (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
    access.write = (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
    access.execute = (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;

    VmBasicInfo info{};
    info.address = static_cast<uint8_t*>(mbi.AllocationBase);
    info.size = mbi.RegionSize;
    info.access = access;
    info.is_free = mbi.State == MEM_FREE;

    return info;
}

bool vm_is_readable(uint8_t* address, size_t size) {
    return IsBadReadPtr(address, size) == FALSE;
}

bool vm_is_writable(uint8_t* address, size_t size) {
    return IsBadWritePtr(address, size) == FALSE;
}

bool vm_is_executable(uint8_t* address) {
    // Check if the address is in a valid module allowing us to potentially skip a heavier memory query.
    HMODULE image{};
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPTSTR>(address), &image) ||
        image == nullptr) {
        return vm_query(address).value_or(VmBasicInfo{}).access.execute;
    }

    // Just check if the section is executable.
    const auto* image_base = reinterpret_cast<uint8_t*>(image);
    const auto* dos_hdr = reinterpret_cast<const IMAGE_DOS_HEADER*>(image_base);

    if (dos_hdr->e_magic != IMAGE_DOS_SIGNATURE) {
        return vm_query(address).value_or(VmBasicInfo{}).access.execute;
    }

    const auto* nt_hdr = reinterpret_cast<const IMAGE_NT_HEADERS*>(image_base + dos_hdr->e_lfanew);

    if (nt_hdr->Signature != IMAGE_NT_SIGNATURE) {
        return vm_query(address).value_or(VmBasicInfo{}).access.execute;
    }

    const auto* section = IMAGE_FIRST_SECTION(nt_hdr);

    for (auto i = 0; i < nt_hdr->FileHeader.NumberOfSections; ++i, ++section) {
        if (address >= image_base + section->VirtualAddress &&
            address < image_base + section->VirtualAddress + section->Misc.VirtualSize) {
            return (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
        }
    }

    return vm_query(address).value_or(VmBasicInfo{}).access.execute;
}

SystemInfo system_info() {
    SystemInfo info{};

    SYSTEM_INFO si{};
    GetSystemInfo(&si);

    info.page_size = si.dwPageSize;
    info.allocation_granularity = si.dwAllocationGranularity;
    info.min_address = static_cast<uint8_t*>(si.lpMinimumApplicationAddress);
    info.max_address = static_cast<uint8_t*>(si.lpMaximumApplicationAddress);

    return info;
}

// cdmp: patches in progress (trap_threads between its two VirtualProtect calls) and what the
// handler did.
static std::atomic<int> g_trapping{0};
static std::atomic<uint64_t> g_trap_redirected{0}, g_trap_retried{0}, g_trap_passed_exec{0}, g_trap_passed_data{0};

static bool executable_now(uint8_t* address) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        return false;
    }
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

struct TrapInfo {
    uint8_t* from_page_start;
    uint8_t* from_page_end;
    uint8_t* from;
    uint8_t* to_page_start;
    uint8_t* to_page_end;
    uint8_t* to;
    size_t len;
};

class TrapManager final {
public:
    static std::mutex mutex;
    static std::unique_ptr<TrapManager> instance;
    static bool is_destructed;

    TrapManager() { m_trap_veh = AddVectoredExceptionHandler(1, trap_handler); }
    ~TrapManager() {
        if (m_trap_veh != nullptr) {
            RemoveVectoredExceptionHandler(m_trap_veh);
        }
        is_destructed = true;
    }

    TrapInfo* find_trap(uint8_t* address) {
        auto search = std::find_if(m_traps.begin(), m_traps.end(), [address](auto& trap) {
            return address >= trap.second.from && address < trap.second.from + trap.second.len;
        });

        if (search == m_traps.end()) {
            return nullptr;
        }

        return &search->second;
    }

    TrapInfo* find_trap_page(uint8_t* address) {
        auto search = std::find_if(m_traps.begin(), m_traps.end(), [address](auto& trap) {
            return address >= trap.second.from_page_start && address < trap.second.from_page_end;
        });

        if (search != m_traps.end()) {
            return &search->second;
        }

        search = std::find_if(m_traps.begin(), m_traps.end(), [address](auto& trap) {
            return address >= trap.second.to_page_start && address < trap.second.to_page_end;
        });

        if (search != m_traps.end()) {
            return &search->second;
        }

        return nullptr;
    }

    void add_trap(uint8_t* from, uint8_t* to, size_t len) {
        TrapInfo info{};
        info.from_page_start = align_down(from, 0x1000);
        info.from_page_end = align_up(from + len, 0x1000);
        info.from = from;
        info.to_page_start = align_down(to, 0x1000);
        info.to_page_end = align_up(to + len, 0x1000);
        info.to = to;
        info.len = len;

        m_traps.insert_or_assign(from, std::move(info));
    }

private:
    std::map<uint8_t*, TrapInfo> m_traps;
    PVOID m_trap_veh{};

    static LONG CALLBACK trap_handler(PEXCEPTION_POINTERS exp) {
        auto exception_code = exp->ExceptionRecord->ExceptionCode;

        if (exception_code != EXCEPTION_ACCESS_VIOLATION) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const auto* rec = exp->ExceptionRecord;
        if (rec->NumberParameters < 2) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        auto* faulting_address = reinterpret_cast<uint8_t*>(rec->ExceptionInformation[1]);

        // cdmp: not an execute fault: never ours (see the top of the file).
        if (rec->ExceptionInformation[0] != EXCEPTION_EXECUTE_FAULT) {
            std::unique_lock lock{mutex, std::try_to_lock};
            if (lock.owns_lock() && instance != nullptr && instance->find_trap_page(faulting_address) != nullptr) {
                g_trap_passed_data.fetch_add(1, std::memory_order_relaxed);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }

        std::scoped_lock lock{mutex};
        if (instance == nullptr) {
            return EXCEPTION_CONTINUE_SEARCH;
        }
        const bool patching = g_trapping.load(std::memory_order_acquire) > 0;
        auto* trap = instance->find_trap(faulting_address);

        if (trap == nullptr) {
            if (instance->find_trap_page(faulting_address) != nullptr) {
                if (patching || executable_now(faulting_address)) {
                    g_trap_retried.fetch_add(1, std::memory_order_relaxed);
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
                g_trap_passed_exec.fetch_add(1, std::memory_order_relaxed);
                return EXCEPTION_CONTINUE_SEARCH;
            } else {
                return EXCEPTION_CONTINUE_SEARCH;
            }
        }

        auto* ctx = exp->ContextRecord;
        const auto ip_before = ctx->Rip;

        for (size_t i = 0; i < trap->len; i++) {
            fix_ip(ctx, trap->from + i, trap->to + i);
        }

        if (ctx->Rip == ip_before && !patching && !executable_now(faulting_address)) {
            g_trap_passed_exec.fetch_add(1, std::memory_order_relaxed);
            return EXCEPTION_CONTINUE_SEARCH;  // the same address again would fault again: not a patch of ours
        }
        g_trap_redirected.fetch_add(1, std::memory_order_relaxed);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
};

std::mutex TrapManager::mutex;
std::unique_ptr<TrapManager> TrapManager::instance;
bool TrapManager::is_destructed = false;

void find_me() {
}

static std::mutex virtual_protect_mutex;

void trap_threads(uint8_t* from, uint8_t* to, size_t len, const std::function<void()>& run_fn) {
    MEMORY_BASIC_INFORMATION find_me_mbi{};
    MEMORY_BASIC_INFORMATION from_mbi{};
    MEMORY_BASIC_INFORMATION to_mbi{};

    VirtualQuery(reinterpret_cast<void*>(find_me), &find_me_mbi, sizeof(find_me_mbi));
    VirtualQuery(from, &from_mbi, sizeof(from_mbi));
    VirtualQuery(to, &to_mbi, sizeof(to_mbi));

    if (to_mbi.State != MEM_COMMIT) {
        return;
    }

    auto new_protect = PAGE_READWRITE;

    if (from_mbi.AllocationBase == find_me_mbi.AllocationBase || to_mbi.AllocationBase == find_me_mbi.AllocationBase) {
        new_protect = PAGE_EXECUTE_READWRITE;
    }

    auto si = system_info();
    auto* from_page_start = align_down(from, si.page_size);
    auto* from_page_end = align_up(from + len, si.page_size);
    auto* vp_start = reinterpret_cast<uint8_t*>(&VirtualProtect);
    auto* vp_end = vp_start + 0x20;

    if (!(from_page_end < vp_start || vp_end < from_page_start)) {
        new_protect = PAGE_EXECUTE_READWRITE;
    }

    if (!TrapManager::is_destructed) {
        std::scoped_lock lock{TrapManager::mutex};

        if (TrapManager::instance == nullptr) {
            TrapManager::instance = std::make_unique<TrapManager>();
        }

        TrapManager::instance->add_trap(from, to, len);
    }

    // Make sure we aren't working on a different address in the same memory page on a different thread.
    std::scoped_lock vp_lock{virtual_protect_mutex};

    // cdmp: a patch is in progress until both protections are back.
    struct Trapping {
        Trapping() { g_trapping.fetch_add(1, std::memory_order_acq_rel); }
        ~Trapping() { g_trapping.fetch_sub(1, std::memory_order_acq_rel); }
    } trapping;

    DWORD from_protect;
    DWORD to_protect;

    VirtualProtect(from, len, new_protect, &from_protect);
    VirtualProtect(to, len, new_protect, &to_protect);

    if (run_fn) {
        run_fn();
    }

    VirtualProtect(to, len, to_protect, &to_protect);
    VirtualProtect(from, len, from_protect, &from_protect);
}

// cdmp: the handler's counters for the mod's diagnostics: out[0] execute faults redirected into a
// trampoline (or retried at a patched address), out[1] execute faults retried on a trap page,
// out[2] execute faults passed on (page not executable, no patch in progress), out[3] read/write
// faults on a trap page passed on (counted when the handler's lock was free), out[4] patches in
// progress now.
void cdmp_trap_stats(uint64_t* out5) noexcept {
    out5[0] = g_trap_redirected.load(std::memory_order_relaxed);
    out5[1] = g_trap_retried.load(std::memory_order_relaxed);
    out5[2] = g_trap_passed_exec.load(std::memory_order_relaxed);
    out5[3] = g_trap_passed_data.load(std::memory_order_relaxed);
    out5[4] = static_cast<uint64_t>(g_trapping.load(std::memory_order_relaxed));
}

void fix_ip(ThreadContext thread_ctx, uint8_t* old_ip, uint8_t* new_ip) {
    auto* ctx = reinterpret_cast<CONTEXT*>(thread_ctx);

#if SAFETYHOOK_ARCH_X86_64
    auto ip = ctx->Rip;
#elif SAFETYHOOK_ARCH_X86_32
    auto ip = ctx->Eip;
#endif

    if (ip == reinterpret_cast<uintptr_t>(old_ip)) {
        ip = reinterpret_cast<uintptr_t>(new_ip);
    }

#if SAFETYHOOK_ARCH_X86_64
    ctx->Rip = ip;
#elif SAFETYHOOK_ARCH_X86_32
    ctx->Eip = ip;
#endif
}

} // namespace safetyhook

#endif
