// Synthetic argument guards plus optional replay of the installed 3527 serializer.
// No game process is opened. Native code and unwind bytes are read from --exe at
// runtime, executed only in this test process, and never written to an artifact.
#include <windows.h>
#include <safetyhook.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mod/session2/interaction_condition.h"

namespace {
using cdmp::session2::interaction_condition_argument;
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

template <class T, size_t N>
void put(std::array<uint8_t, N>& bytes, size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof value);
}

struct Fixture {
    char empty = 0;
    struct String { const char* chars; uint32_t size; uint32_t reserved; } string{&empty, 0, 0};
    std::array<uint8_t, 0x38> entry{};
    Fixture() { put(entry, 0, reinterpret_cast<uintptr_t>(&string)); }
    uintptr_t address() const { return reinterpret_cast<uintptr_t>(entry.data()); }
    uintptr_t argument() const { return interaction_condition_argument(0, address()); }
};

void synthetic_tests() {
    Fixture f;
    const auto before = f.entry;
    const uintptr_t replacement = f.argument();
    CHECK(replacement != 0);
    if (replacement) {
        const std::array<uint64_t, 2> zeros{};
        CHECK(std::memcmp(reinterpret_cast<const void*>(replacement), zeros.data(), sizeof zeros) == 0);
        CHECK(f.argument() == replacement);
    }
    CHECK(f.entry == before);
    CHECK(interaction_condition_argument(0, 0) == 0);
    CHECK(interaction_condition_argument(0, 1) == 0);
    CHECK(interaction_condition_argument(1, 0) == 1);  // Nonnull is passed through without dereference.
    CHECK(interaction_condition_argument(0x12345678, 1) == 0x12345678);
    for (uint32_t bit = 0; bit < 29; ++bit) {
        put(f.entry, 0x20, uint32_t{1} << bit);
        CHECK(f.argument() == 0);
    }
    put(f.entry, 0x20, uint32_t{0xE0000000});
    CHECK(f.argument() == replacement);  // Only the lower 29 bits carry the stage id.
    put(f.entry, 0x20, uint32_t{0});
    put(f.entry, 0x2E, uint16_t{0xFFFF});
    CHECK(f.argument() == 0);
    put(f.entry, 0x2E, uint16_t{0xFFFE});
    CHECK(f.argument() == replacement);  // A resource index, not a vector index.
    put(f.entry, 0x2E, uint16_t{0});
    put(f.entry, 8, uintptr_t{1});
    CHECK(f.argument() == 0);  // Mismatched incoming holder and entry are not repaired.
    put(f.entry, 8, uintptr_t{0});
    f.string.size = 1;
    CHECK(f.argument() == 0);
    f.string.size = 0;
    f.empty = 'x';
    CHECK(f.argument() == 0);
    f.empty = 0;
    f.string.chars = nullptr;
    CHECK(f.argument() == 0);
    f.string.chars = reinterpret_cast<const char*>(1);
    CHECK(f.argument() == 0);
    f.string.chars = &f.empty;
    put(f.entry, 0, uintptr_t{0});
    CHECK(f.argument() == 0);
    put(f.entry, 0, uintptr_t{1});
    CHECK(f.argument() == 0);
    put(f.entry, 0, reinterpret_cast<uintptr_t>(&f.string));
    CHECK(f.entry == before);

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    auto* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(memory != nullptr);
    if (!memory) return;
    std::memcpy(memory, f.entry.data(), f.entry.size());
    // A complete valid-looking prefix at a page boundary must not permit a
    // partial 0x38-byte entry read into a PAGE_NOACCESS page.
    std::memcpy(memory + page - 0x30, f.entry.data(), 0x30);
    DWORD prior = 0;
    const bool protected_ok = VirtualProtect(memory + page, page, PAGE_NOACCESS, &prior) != FALSE;
    CHECK(protected_ok);
    if (protected_ok) {
        CHECK(interaction_condition_argument(0, reinterpret_cast<uintptr_t>(memory + page - 0x30)) == 0);
        CHECK(interaction_condition_argument(0, reinterpret_cast<uintptr_t>(memory + page)) == 0);
    }
    const bool read_only = VirtualProtect(memory, page, PAGE_READONLY, &prior) != FALSE;
    CHECK(read_only);
    if (read_only) {
        CHECK(interaction_condition_argument(0, reinterpret_cast<uintptr_t>(memory)) == replacement);
        CHECK(std::memcmp(memory, before.data(), before.size()) == 0);
    }
    VirtualFree(memory, 0, MEM_RELEASE);
}

// Read only the requested PE ranges; do not map/load the executable or run its
// initializers. This also keeps replay memory use independent of the exe size.
class PeReader {
    std::ifstream input_;
    std::vector<IMAGE_SECTION_HEADER> sections_;
    IMAGE_DATA_DIRECTORY exceptions_{};
    std::vector<uint8_t> at(uint64_t offset, size_t size) {
        std::vector<uint8_t> out(size);
        input_.seekg(static_cast<std::streamoff>(offset));
        input_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (!input_) throw std::runtime_error("short PE read");
        return out;
    }
    template <class T> T object(uint64_t offset) {
        const auto bytes = at(offset, sizeof(T));
        T out{};
        std::memcpy(&out, bytes.data(), sizeof out);
        return out;
    }
public:
    explicit PeReader(const std::filesystem::path& path) : input_(path, std::ios::binary) {
        if (!input_) throw std::runtime_error("cannot open --exe");
        const auto dos = object<IMAGE_DOS_HEADER>(0);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0x40 || dos.e_lfanew > 0x100000)
            throw std::runtime_error("invalid DOS header");
        const auto nt = object<IMAGE_NT_HEADERS64>(static_cast<uint64_t>(dos.e_lfanew));
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            nt.FileHeader.TimeDateStamp != 0x6AEA07A6 || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.OptionalHeader.ImageBase != 0x140000000ull || nt.OptionalHeader.SizeOfImage != 0x13EB2000 ||
            nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96)
            throw std::runtime_error("--exe is not the verified 3527 PE profile");
        const uint64_t table = static_cast<uint64_t>(dos.e_lfanew) + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                               nt.FileHeader.SizeOfOptionalHeader;
        for (uint32_t n = 0; n < nt.FileHeader.NumberOfSections; ++n)
            sections_.push_back(object<IMAGE_SECTION_HEADER>(table + n * sizeof(IMAGE_SECTION_HEADER)));
        exceptions_ = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    }
    std::vector<uint8_t> rva(uint32_t address, size_t size) {
        for (const auto& s : sections_) {
            if (address < s.VirtualAddress) continue;
            const uint64_t delta = static_cast<uint64_t>(address) - s.VirtualAddress;
            if (delta + size <= s.SizeOfRawData) return at(s.PointerToRawData + delta, size);
        }
        throw std::runtime_error("RVA is outside raw section data");
    }
    RUNTIME_FUNCTION function(uint32_t address) {
        if (!exceptions_.Size || exceptions_.Size > 0x1000000) throw std::runtime_error("invalid PE unwind table");
        const auto table = rva(exceptions_.VirtualAddress, exceptions_.Size);
        for (size_t n = 0; n + sizeof(RUNTIME_FUNCTION) <= table.size(); n += sizeof(RUNTIME_FUNCTION)) {
            RUNTIME_FUNCTION f{};
            std::memcpy(&f, table.data() + n, sizeof f);
            if (f.BeginAddress == address) return f;
        }
        throw std::runtime_error("native function has no unwind record");
    }
};

struct ExecutableSerializer {
    uint8_t* code = nullptr;
    std::array<RUNTIME_FUNCTION, 3> unwind{};
    bool registered = false;
    explicit ExecutableSerializer(PeReader& pe) {
        constexpr uint32_t start = 0x2494230;
        const auto f = pe.function(start);
        if (f.EndAddress != start + 0xAE) throw std::runtime_error("unexpected serializer boundary");
        const auto bytes = pe.rva(start, 0xAE);
        const auto header = pe.rva(f.UnwindData, 4);
        // No exception-handler or chained-unwind addresses may be copied verbatim.
        if (header[0] != 1 || header[1] != 0x0A || header[2] != 4 || header[3] != 0 ||
            bytes[0x0A] != 0x48 || bytes[0x0B] != 0x83 || bytes[0x0C] != 0x39 || bytes[0x0D] != 0 ||
            bytes.back() != 0xC3) throw std::runtime_error("unexpected serializer/unwind shape");
        const auto native_unwind = pe.rva(f.UnwindData, 4 + 2 * header[2]);
        const auto inverse_function = pe.function(0x24942E0);
        if (inverse_function.EndAddress != 0x24943F8) throw std::runtime_error("unexpected inverse boundary");
        const auto inverse = pe.rva(0x24942E0, 0x118);
        const auto inverse_header = pe.rva(inverse_function.UnwindData, 4);
        if (inverse_header[0] != 0x11 || inverse_header[1] != 0x0F || inverse_header[2] != 6 || inverse_header[3] != 0)
            throw std::runtime_error("unexpected inverse unwind shape");
        auto inverse_unwind = pe.rva(inverse_function.UnwindData, 4 + 2 * inverse_header[2]);
        // The empty/read-failure branches use no owned temporaries or direct
        // calls. Preserve their native stack-unwind codes, but omit the game
        // exception handler whose RVA cannot be used in this isolated copy.
        // Nonempty inverse replay is deliberately not supported.
        inverse_unwind[0] = 1;
        auto call_window = pe.rva(0x22E2650, 16);
        const uint8_t prefix[]{0x48, 0x8B, 0x4F, 0x08, 0x48, 0x8B, 0xD3, 0xE8};
        const uint8_t suffix[]{0x0F, 0xB7, 0x57, 0x10};
        int32_t native_relative = 0;
        std::memcpy(&native_relative, call_window.data() + 8, sizeof native_relative);
        if (std::memcmp(call_window.data(), prefix, sizeof prefix) != 0 ||
            std::memcmp(call_window.data() + 12, suffix, sizeof suffix) != 0 ||
            0x22E265Cll + native_relative != start)
            throw std::runtime_error("unexpected native call-site window");
        // Test-owned ABI adapter: preserve RBX/RDI, align the stack, and put
        // the supplied entry/archive into the native caller's registers.
        const uint8_t adapter[]{0x53, 0x57, 0x48, 0x83, 0xEC, 0x28,
                                0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xDA};
        // Return the word loaded by the original instruction AFTER the call.
        const uint8_t tail[]{0x89, 0xD0, 0x48, 0x83, 0xC4, 0x28, 0x5F, 0x5B, 0xC3};
        const uint8_t adapter_unwind[]{1, 6, 3, 0, 6, 0x42, 2, 0x70, 1, 0x30, 0, 0};
        const int32_t replay_relative = -static_cast<int32_t>(0x2000 + sizeof adapter + 12);
        std::memcpy(call_window.data() + 8, &replay_relative, sizeof replay_relative);
        code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!code) throw std::runtime_error("cannot allocate replay code");
        std::memcpy(code, bytes.data(), bytes.size());
        std::memcpy(code + 0x100, native_unwind.data(), native_unwind.size());
        std::memcpy(code + 0x1000, inverse.data(), inverse.size());
        std::memcpy(code + 0x1200, inverse_unwind.data(), inverse_unwind.size());
        std::memcpy(code + 0x2000, adapter, sizeof adapter);
        std::memcpy(code + 0x2000 + sizeof adapter, call_window.data(), call_window.size());
        std::memcpy(code + 0x2000 + sizeof adapter + call_window.size(), tail, sizeof tail);
        std::memcpy(code + 0x2100, adapter_unwind, sizeof adapter_unwind);
        DWORD previous = 0;
        if (!VirtualProtect(code, 0x3000, PAGE_EXECUTE_READ, &previous)) {
            VirtualFree(code, 0, MEM_RELEASE); code = nullptr;
            throw std::runtime_error("cannot protect replay code");
        }
        FlushInstructionCache(GetCurrentProcess(), code, 0x3000);
        unwind[0] = {0, 0xAE, 0x100};
        unwind[1] = {0x1000, 0x1118, 0x1200};
        unwind[2] = {0x2000, 0x2025, 0x2100};
        registered = RtlAddFunctionTable(unwind.data(), static_cast<DWORD>(unwind.size()), reinterpret_cast<DWORD64>(code)) != FALSE;
        if (!registered) {
            VirtualFree(code, 0, MEM_RELEASE); code = nullptr;
            throw std::runtime_error("cannot register replay unwind data");
        }
    }
    ~ExecutableSerializer() {
        if (registered) RtlDeleteFunctionTable(unwind.data());
        if (code) VirtualFree(code, 0, MEM_RELEASE);
    }
    ExecutableSerializer(const ExecutableSerializer&) = delete;
    ExecutableSerializer& operator=(const ExecutableSerializer&) = delete;
};

struct Archive {
    void** vtable;
    std::array<uint8_t, 64> bytes{};
    size_t size = 0;
    bool overflow = false;
    size_t position = 0;
};
void __fastcall archive_write(Archive* self, const void* source, size_t size) {
    if (size > self->bytes.size() - self->size) { self->overflow = true; return; }
    std::memcpy(self->bytes.data() + self->size, source, size);
    self->size += size;
}
bool __fastcall archive_read(Archive* self, void* destination, size_t size) {
    if (self->position > self->size || size > self->size - self->position) return false;
    std::memcpy(destination, self->bytes.data() + self->position, size);
    self->position += size;
    return true;
}
void* g_archive_vtable[6]{nullptr, reinterpret_cast<void*>(&archive_write), nullptr, nullptr, nullptr,
                         reinterpret_cast<void*>(&archive_read)};
struct Contents {
    void** vtable;
    std::array<uint8_t, 4> calls{};
    size_t count = 0;
    void note(uint8_t slot) { if (count < calls.size()) calls[count++] = slot; }
};
void __fastcall content_write(Contents* self, Archive* archive) {
    self->note(0x60);
    const uint8_t payload[]{0xDE, 0xAD, 0xBE, 0xEF};
    archive_write(archive, payload, sizeof payload);
}
uint8_t __fastcall content_38(Contents* self) { self->note(0x38); return 0x31; }
uint8_t __fastcall content_30(Contents* self) { self->note(0x30); return 0x52; }
uint8_t __fastcall content_28(Contents* self) { self->note(0x28); return 0x73; }
using SerializerFn = void(__fastcall*)(uintptr_t, Archive*);
using InverseFn = bool(__fastcall*)(uintptr_t, Archive*);
using CallerFn = uint32_t(__fastcall*)(uintptr_t, Archive*, uintptr_t);
struct Fault { DWORD code = 0; uintptr_t pc = 0, address = 0; ULONG_PTR access = 0; };
int capture_fault(EXCEPTION_POINTERS* ep, Fault* fault) {
    const auto* e = ep->ExceptionRecord;
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    fault->code = e->ExceptionCode;
    fault->pc = reinterpret_cast<uintptr_t>(e->ExceptionAddress);
    if (e->NumberParameters >= 2) { fault->access = e->ExceptionInformation[0]; fault->address = e->ExceptionInformation[1]; }
    return EXCEPTION_EXECUTE_HANDLER;
}
// No C++ unwind objects here: the exact copied function has its own registered
// native unwind record, allowing its intentional fault to reach this SEH frame.
bool invoke(SerializerFn fn, uintptr_t holder, Archive* archive, Fault* fault) {
    __try { fn(holder, archive); return true; }
    __except (capture_fault(GetExceptionInformation(), fault)) { return false; }
}

bool invoke_inverse(InverseFn fn, uintptr_t holder, Archive* archive, bool* result, Fault* fault) {
    __try { *result = fn(holder, archive); return true; }
    __except (capture_fault(GetExceptionInformation(), fault)) { return false; }
}

uint32_t g_mid_calls = 0;
void replay_call_hook(SafetyHookContext& ctx) {
    ++g_mid_calls;
    ctx.rcx = interaction_condition_argument(ctx.rcx, ctx.rdi);
}

void native_tests(const std::filesystem::path& path) {
    PeReader pe(path);
    ExecutableSerializer native(pe);
    const auto fn = reinterpret_cast<SerializerFn>(native.code);
    Archive null_output{g_archive_vtable};
    Fault fault;
    CHECK(!invoke(fn, 0, &null_output, &fault));
    CHECK(fault.code == EXCEPTION_ACCESS_VIOLATION);
    CHECK(fault.pc == reinterpret_cast<uintptr_t>(native.code) + 0x0A);
    CHECK(fault.address == 0 && fault.access == 0);
    CHECK(null_output.size == 0);

    Fixture f;
    const auto before = f.entry;
    const uintptr_t substitute = f.argument();
    Archive empty{g_archive_vtable};
    fault = {};
    CHECK(substitute != 0 && invoke(fn, substitute, &empty, &fault));
    CHECK(!fault.code && !empty.overflow && empty.size == 1 && empty.bytes[0] == 0);
    CHECK(f.entry == before);
    const std::array<uint64_t, 2> zero_wrapper{};
    CHECK(substitute && std::memcmp(reinterpret_cast<const void*>(substitute), zero_wrapper.data(), sizeof zero_wrapper) == 0);

    std::array<void*, 13> content_vtable{};
    content_vtable[0x60 / 8] = reinterpret_cast<void*>(&content_write);
    content_vtable[0x38 / 8] = reinterpret_cast<void*>(&content_38);
    content_vtable[0x30 / 8] = reinterpret_cast<void*>(&content_30);
    content_vtable[0x28 / 8] = reinterpret_cast<void*>(&content_28);
    Contents contents{content_vtable.data()};
    const std::array<uint64_t, 2> wrapper{reinterpret_cast<uintptr_t>(&contents), 0xABCDEF};
    const uintptr_t original = reinterpret_cast<uintptr_t>(wrapper.data());
    const uintptr_t passed = interaction_condition_argument(original, 0);
    CHECK(passed == original);
    Archive plain{g_archive_vtable}, guarded{g_archive_vtable};
    CHECK(invoke(fn, original, &plain, &fault));
    const std::array<uint8_t, 4> expected_calls{0x60, 0x38, 0x30, 0x28};
    CHECK(contents.count == 4 && contents.calls == expected_calls);
    contents.count = 0; contents.calls = {};
    CHECK(invoke(fn, passed, &guarded, &fault));
    const std::array<uint8_t, 8> expected{1, 0xDE, 0xAD, 0xBE, 0xEF, 0x31, 0x52, 0x73};
    CHECK(!plain.overflow && !guarded.overflow && plain.size == expected.size() && guarded.size == expected.size());
    CHECK(std::memcmp(plain.bytes.data(), expected.data(), expected.size()) == 0);
    CHECK(guarded.bytes == plain.bytes && contents.count == 4 && contents.calls == expected_calls);
    CHECK(wrapper[0] == reinterpret_cast<uintptr_t>(&contents) && wrapper[1] == 0xABCDEF);

    if (empty.size != 1 || empty.bytes[0] != 0 || empty.overflow)
        throw std::runtime_error("inverse replay is restricted to the verified empty marker");
    const auto inverse = reinterpret_cast<InverseFn>(native.code + 0x1000);
    Archive input{g_archive_vtable};
    input.bytes[0] = empty.bytes[0]; input.bytes[1] = 0xA5; input.size = 2;
    std::array<uint64_t, 2> restored{};
    bool inverse_result = false;
    CHECK(invoke_inverse(inverse, reinterpret_cast<uintptr_t>(restored.data()), &input, &inverse_result, &fault));
    CHECK(inverse_result && input.position == 1 && input.bytes[1] == 0xA5 && restored == zero_wrapper);
    Archive truncated{g_archive_vtable};
    inverse_result = true;
    CHECK(invoke_inverse(inverse, reinterpret_cast<uintptr_t>(restored.data()), &truncated, &inverse_result, &fault));
    CHECK(!inverse_result && truncated.position == 0 && restored == zero_wrapper);

    // Hook the extracted CALL instruction, not a C++ stand-in. The local
    // adapter changes only its rel32 destination to the exact copied native
    // serializer. This tests SafetyHook's relocation and argument restoration;
    // production component/vtable membership remains a separate install guard.
    const auto caller = reinterpret_cast<CallerFn>(native.code + 0x2000);
    auto hook = safetyhook::create_mid(native.code + 0x2013, &replay_call_hook);
    CHECK(static_cast<bool>(hook));
    if (hook) {
        put(f.entry, 0x10, uint16_t{0xBEEF});
        const auto hook_before = f.entry;
        Archive hooked_empty{g_archive_vtable};
        CHECK(caller(0, &hooked_empty, f.address()) == 0xBEEF);
        CHECK(g_mid_calls == 1 && hooked_empty.size == 1 && hooked_empty.bytes[0] == 0 && !hooked_empty.overflow);
        CHECK(f.entry == hook_before);
        put(f.entry, 8, original);
        const auto nonempty_before = f.entry;
        contents.count = 0; contents.calls = {};
        Archive hooked_nonempty{g_archive_vtable};
        CHECK(caller(original, &hooked_nonempty, f.address()) == 0xBEEF);
        CHECK(g_mid_calls == 2 && hooked_nonempty.size == expected.size() && !hooked_nonempty.overflow);
        CHECK(hooked_nonempty.bytes == plain.bytes && contents.calls == expected_calls && contents.count == 4);
        CHECK(f.entry == nonempty_before);
    }
    std::printf("native 3527 replay: null AV +0xA; empty 00; nonempty unchanged; empty inverse; relocated CALL\n");
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        synthetic_tests();
        if (argc == 3 && std::wstring(argv[1]) == L"--exe") native_tests(std::filesystem::path(argv[2]));
        else if (argc != 1) { std::printf("usage: cdmp_tests_optd_interaction_condition [--exe PATH]\n"); return 2; }
    } catch (const std::exception& e) { std::printf("FAIL: %s\n", e.what()); ++g_failures; }
    std::printf("optd_interaction_condition: %s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
