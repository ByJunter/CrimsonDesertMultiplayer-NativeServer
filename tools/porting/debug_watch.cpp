// External, observation-only debug-event recorder. It never writes target
// memory, modifies a context, suppresses a program exception, or terminates
// the target. Attaching briefly suspends its threads as required by Windows.
//
// cdmp_debug_watch.exe <pid> <output-directory> [timeout-seconds=600] [--first-chance-av]
// cdmp_debug_watch.exe --self-test-child  (this process fast-fails after 3 s)
// cdmp_debug_watch.exe --self-test-idle   (this process exits after 30 s)
//
// API ownership/continuation references:
// https://learn.microsoft.com/windows/win32/api/debugapi/nf-debugapi-waitfordebugevent
// https://learn.microsoft.com/windows/win32/api/debugapi/nf-debugapi-debugactiveprocess
// https://learn.microsoft.com/windows/win32/api/minidumpapiset/nf-minidumpapiset-minidumpwritedump
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#if !defined(_M_X64)
#error Build this helper for x64.
#endif

namespace {
volatile LONG g_stop = 0;

BOOL WINAPI on_control(DWORD code) {
    if (code == CTRL_C_EVENT || code == CTRL_BREAK_EVENT || code == CTRL_CLOSE_EVENT ||
        code == CTRL_LOGOFF_EVENT || code == CTRL_SHUTDOWN_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};

std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                       nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "<path conversion failed>";
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string timestamp() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}-{:03}",
                       t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
}

class Log {
public:
    explicit Log(const std::filesystem::path& path) : file_(path, std::ios::out | std::ios::app) {
        if (!file_) throw std::runtime_error("Cannot open watcher log");
    }
    void line(const std::string& text) {
        const std::string full = timestamp() + " " + text + "\n";
        std::fwrite(full.data(), 1, full.size(), stdout);
        std::fflush(stdout);
        file_ << full;
        file_.flush();
    }
private:
    std::ofstream file_;
};

std::string file_name(HANDLE file) {
    if (!file || file == INVALID_HANDLE_VALUE) return "<no file handle>";
    wchar_t path[32768]{};
    const DWORD length = GetFinalPathNameByHandleW(file, path, static_cast<DWORD>(std::size(path)), 0);
    if (!length || length >= std::size(path)) return "<path unavailable>";
    return utf8(std::wstring_view(path, length));
}

DWORD number(const wchar_t* text, DWORD minimum, DWORD maximum) {
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(text, &end, 10);
    if (!text[0] || !end || *end || value < minimum || value > maximum) {
        throw std::runtime_error("Invalid numeric argument");
    }
    return static_cast<DWORD>(value);
}

BOOL CALLBACK dump_callback(PVOID parameter, const PMINIDUMP_CALLBACK_INPUT input,
                            PMINIDUMP_CALLBACK_OUTPUT output) {
    if (input->CallbackType == CancelCallback) {
        const auto deadline = *static_cast<const ULONGLONG*>(parameter);
        output->CheckCancel = TRUE;
        output->Cancel = InterlockedCompareExchange(&g_stop, 0, 0) != 0 || GetTickCount64() >= deadline;
    }
    return TRUE;
}

void capture(HANDLE process, const DEBUG_EVENT& event, const std::filesystem::path& directory,
             unsigned serial, ULONGLONG watch_deadline, Log& log) {
    const auto& info = event.u.Exception;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_ALL;
    Handle thread(OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId));
    if (!thread || !GetThreadContext(thread.value, &context)) {
        log.line(std::format("CONTEXT_FAILED thread={} error={}", event.dwThreadId, GetLastError()));
        return;
    }
    log.line(std::format("REGISTERS thread={} rip=0x{:X} rsp=0x{:X} rbp=0x{:X} rax=0x{:X} rbx=0x{:X} "
                         "rcx=0x{:X} rdx=0x{:X} rsi=0x{:X} rdi=0x{:X} r8=0x{:X} r9=0x{:X} "
                         "r10=0x{:X} r11=0x{:X} r12=0x{:X} r13=0x{:X} r14=0x{:X} r15=0x{:X}",
                         event.dwThreadId, context.Rip, context.Rsp, context.Rbp, context.Rax, context.Rbx,
                         context.Rcx, context.Rdx, context.Rsi, context.Rdi, context.R8, context.R9,
                         context.R10, context.R11, context.R12, context.R13, context.R14, context.R15));
    EXCEPTION_RECORD record = info.ExceptionRecord;
    // All exception structures passed to DbgHelp live in this debugger.
    EXCEPTION_POINTERS pointers{&record, &context};
    MINIDUMP_EXCEPTION_INFORMATION exception{event.dwThreadId, &pointers, FALSE};
    const auto path = directory / std::format("cdmp_debug.{}.{}.{}.{}.dmp", event.dwProcessId,
                                              timestamp(), serial, info.dwFirstChance ? "first" : "second");
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file) {
        log.line(std::format("DUMP_OPEN_FAILED error={} path={}", GetLastError(), utf8(path.native())));
        return;
    }
    ULONGLONG deadline = std::min(watch_deadline, GetTickCount64() + 30000ull);
    MINIDUMP_CALLBACK_INFORMATION callback{dump_callback, &deadline};
    const auto flags = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpIgnoreInaccessibleMemory);
    const BOOL okay = MiniDumpWriteDump(process, event.dwProcessId, file.value, flags,
                                       &exception, nullptr, &callback);
    const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
    FlushFileBuffers(file.value);
    log.line(std::format("DUMP_{} error=0x{:X} path={}", okay ? "WRITTEN" : "FAILED", error,
                         utf8(path.native())));
}

struct Attachment {
    DWORD pid;
    bool attached = false;
    ~Attachment() { if (attached) DebugActiveProcessStop(pid); }
};

int watch(DWORD pid, const std::filesystem::path& directory, DWORD seconds, bool dump_first_chance_av) {
    std::filesystem::create_directories(directory);
    Log log(directory / std::format("debug_watch.{}.{}.log", pid, timestamp()));
    Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE |
                                SYNCHRONIZE, FALSE, pid));
    if (!process) {
        log.line(std::format("OPEN_PROCESS_FAILED pid={} error={}", pid, GetLastError()));
        return 2;
    }
    Attachment attachment{pid};
    if (!DebugActiveProcess(pid)) {
        log.line(std::format("ATTACH_FAILED pid={} error={}", pid, GetLastError()));
        return 2;
    }
    attachment.attached = true;
    if (!DebugSetProcessKillOnExit(FALSE)) {
        const DWORD error = GetLastError();
        log.line(std::format("KILL_ON_EXIT_DISABLE_FAILED error={}; detaching", error));
        return 2;  // Attachment attempts immediate detach; never proceed with unsafe exit policy.
    }
    log.line(std::format("ATTACHED pid={} kill_on_exit=false timeout_seconds={} first_chance_av_dumps={}",
                         pid, seconds, dump_first_chance_av ? 3 : 0));
    SetConsoleCtrlHandler(on_control, TRUE);
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(seconds) * 1000;
    bool initial_breakpoint = true;
    unsigned captures = 0;
    unsigned observed_first_chance = 0;
    unsigned first_chance_av_records = 0;
    unsigned first_chance_av_dumps = 0;
    while (!InterlockedCompareExchange(&g_stop, 0, 0) && GetTickCount64() < deadline) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) {
            const DWORD error = GetLastError();
            if (error == ERROR_SEM_TIMEOUT) continue;
            log.line(std::format("WAIT_FAILED error={}", error));
            break;
        }
        DWORD disposition = event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT ? DBG_EXCEPTION_NOT_HANDLED : DBG_CONTINUE;
        bool exited = false;
        try {
            switch (event.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT: {
                Handle image(event.u.CreateProcessInfo.hFile);
                log.line(std::format("PROCESS_IMAGE base=0x{:X} path={}",
                    reinterpret_cast<uintptr_t>(event.u.CreateProcessInfo.lpBaseOfImage), file_name(image.value)));
                // Windows closes CREATE_PROCESS/THREAD process and thread handles
                // when their EXIT debug events are continued. Close only hFile here.
                break;
            }
            case LOAD_DLL_DEBUG_EVENT: {
                Handle image(event.u.LoadDll.hFile);
                log.line(std::format("MODULE base=0x{:X} path={}",
                    reinterpret_cast<uintptr_t>(event.u.LoadDll.lpBaseOfDll), file_name(image.value)));
                break;
            }
            case EXCEPTION_DEBUG_EVENT: {
                const auto& info = event.u.Exception;
                const auto& record = info.ExceptionRecord;
                if (initial_breakpoint && info.dwFirstChance && record.ExceptionCode == EXCEPTION_BREAKPOINT) {
                    initial_breakpoint = false;
                    disposition = DBG_CONTINUE;
                    log.line("ATTACH_BREAKPOINT continued; all program exceptions remain unhandled by watcher");
                    break;
                }
                const bool selected = !info.dwFirstChance || record.ExceptionCode == 0xC0000409u;
                const bool dump_av = dump_first_chance_av && info.dwFirstChance &&
                                     record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                                     first_chance_av_dumps < 3;
                const bool record_av = info.dwFirstChance && record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                                       first_chance_av_records++ < 32;
                if (selected || record_av || observed_first_chance++ < 12) {
                    std::string params;
                    for (DWORD i = 0; i < record.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i) {
                        params += std::format(" p{}=0x{:X}", i, record.ExceptionInformation[i]);
                    }
                    log.line(std::format("EXCEPTION thread={} code=0x{:X} first={} address=0x{:X} flags=0x{:X}{}",
                        event.dwThreadId, record.ExceptionCode, info.dwFirstChance,
                        reinterpret_cast<uintptr_t>(record.ExceptionAddress), record.ExceptionFlags, params));
                }
                if (record_av) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    Handle thread(OpenThread(THREAD_GET_CONTEXT, FALSE, event.dwThreadId));
                    if (thread && GetThreadContext(thread.value, &context)) {
                        log.line(std::format("FIRST_CHANCE_AV_REGISTERS thread={} rip=0x{:X} rcx=0x{:X} "
                                             "rdx=0x{:X} rsp=0x{:X}", event.dwThreadId,
                                             context.Rip, context.Rcx, context.Rdx, context.Rsp));
                    } else {
                        log.line(std::format("FIRST_CHANCE_AV_CONTEXT_FAILED thread={} error={}",
                                             event.dwThreadId, GetLastError()));
                    }
                }
                if (dump_av) ++first_chance_av_dumps;
                if (selected || dump_av) capture(process.value, event, directory, ++captures, deadline, log);
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT:
                log.line(std::format("EXIT_PROCESS pid={} code=0x{:X}", event.dwProcessId,
                                      event.u.ExitProcess.dwExitCode));
                exited = true;
                break;
            default:
                break;
            }
        } catch (const std::exception& error) {
            log.line(std::string("EVENT_RECORD_FAILED: ") + error.what());
        }
        // Continue even if recording fails, preserving the exception's normal path.
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition)) {
            log.line(std::format("CONTINUE_FAILED error={}", GetLastError()));
            break;
        }
        if (exited) {
            attachment.attached = false;
            SetConsoleCtrlHandler(on_control, FALSE);
            return 0;
        }
    }
    const BOOL detached = DebugActiveProcessStop(pid);
    const DWORD error = detached ? ERROR_SUCCESS : GetLastError();
    if (detached) attachment.attached = false;
    log.line(std::format("DETACH {} error={} reason={}", detached ? "OK" : "FAILED", error,
                         InterlockedCompareExchange(&g_stop, 0, 0) ? "control signal" : "timeout or wait failure"));
    SetConsoleCtrlHandler(on_control, FALSE);
    return detached ? 0 : 3;
}
}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test-child") {
        std::printf("Self-test child PID %lu: fast-fail(5) in 3 seconds.\n", GetCurrentProcessId());
        std::fflush(stdout);
        Sleep(3000);
        __fastfail(FAST_FAIL_INVALID_ARG);
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test-idle") {
        std::printf("Self-test idle PID %lu: normal exit in 30 seconds.\n", GetCurrentProcessId());
        std::fflush(stdout);
        Sleep(30000);
        return 0;
    }
    if (argc < 3 || argc > 5 || (argc == 5 && std::wstring_view(argv[4]) != L"--first-chance-av")) {
        std::fwprintf(stderr, L"Usage: %ls <pid> <output-directory> [timeout-seconds=600] [--first-chance-av]\n"
                              L"       %ls --self-test-child | --self-test-idle\n", argv[0], argv[0]);
        return 2;
    }
    try {
        return watch(number(argv[1], 1, std::numeric_limits<DWORD>::max()),
                     std::filesystem::absolute(argv[2]), argc >= 4 ? number(argv[3], 1, 3600) : 600,
                     argc == 5);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "debug_watch: %s\n", error.what());
        return 2;
    }
}
