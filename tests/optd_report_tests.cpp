// RPT-0, K1: the error-report cut (src/mod/session2/report_guard.cpp), for
// real, on a fake reporter with the game's signature. The game is not needed:
// the hook goes on a function of this test, and the log goes to %TEMP%.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "mod/log.h"
#include "mod/session2/report_guard.h"

using cdmp::session2::ReportFiles;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

volatile int g_reached = 0;
volatile uint32_t g_seen_count = 0;

// Stands in for 0x1317D40: same arguments, and a body long enough to hook.
__declspec(noinline) void __fastcall fake_reporter(uint8_t* active, const char* reason, const ReportFiles* files) {
    if (!*active) return;
    g_reached = g_reached + 1;
    g_seen_count = files ? files->count : 0;
    if (reason && reason[0] == '!') g_reached = g_reached + 100;
}

// Called through a volatile pointer so the compiler cannot inline it.
cdmp::session2::ReporterFn volatile g_call = &fake_reporter;

// A path object as the game builds it: first field is the char* path.
struct PathObj {
    const char* path;
    uint64_t other;
};

bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

}  // namespace

int main() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring root = std::wstring(tmp) + L"cdmp_report_test_" + std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    const std::wstring log_path = root + L"\\reporttest.log";
    cdmp::log::open(log_path);

    uint8_t on = 1;
    PathObj a{"C:/Users/x/AppData/Local/Pearl Abyss/CD/save/123/slot0/save.save", 0};
    PathObj b{"C:/Users/x/AppData/Local/Pearl Abyss/CD/save/123/slot0/lobby.save", 0};
    PathObj c{"C:/Users/x/AppData/Local/Pearl Abyss/log/Launcher_1.log", 0};
    void* items[] = {&a, &b, &c};
    ReportFiles files{items, 3};
    ReportFiles none{nullptr, 0};

    // describe: the normal case.
    {
        const std::string d = cdmp::session2::report_describe("Savedata Save Fail Event", &files);
        CHECK(contains(d, "reason 'Savedata Save Fail Event'"));
        CHECK(contains(d, "3 files: "));
        CHECK(contains(d, "slot0/save.save | "));
        CHECK(contains(d, "slot0/lobby.save | "));
        CHECK(contains(d, "Launcher_1.log"));
    }
    // describe: every pointer bad, without crashing.
    {
        CHECK(contains(cdmp::session2::report_describe(nullptr, nullptr), "reason '(null)', files unreadable"));
        const auto bad_files = reinterpret_cast<const ReportFiles*>(uintptr_t{0x10});
        CHECK(contains(cdmp::session2::report_describe("x", bad_files), "files unreadable"));
        ReportFiles bad_data{reinterpret_cast<void**>(uintptr_t{0x10}), 2};
        const std::string d = cdmp::session2::report_describe("x", &bad_data);
        CHECK(contains(d, "2 files: (unreadable) | (unreadable)"));
        PathObj null_path{nullptr, 0};
        void* items2[] = {&null_path, nullptr};
        ReportFiles odd{items2, 2};
        const std::string d2 = cdmp::session2::report_describe(reinterpret_cast<const char*>(uintptr_t{0x10}), &odd);
        CHECK(contains(d2, "reason '(unreadable)'"));
        CHECK(contains(d2, "2 files: (null) | (unreadable)"));
        CHECK(contains(cdmp::session2::report_describe("r", &none), "0 files"));
    }
    // describe: limits (8 paths, 260 chars).
    {
        void* many[20];
        for (auto& m : many) m = &a;
        ReportFiles lots{many, 20};
        const std::string d = cdmp::session2::report_describe("r", &lots);
        CHECK(contains(d, "20 files"));
        CHECK(contains(d, " | ..."));
        size_t n = 0;
        for (size_t p = d.find("save.save"); p != std::string::npos; p = d.find("save.save", p + 1)) ++n;
        CHECK(n == 8);
        const std::string long_text(1000, 'a');
        CHECK(cdmp::session2::report_describe(long_text.c_str(), &none).size() < 400);
        // A string that ends at the edge of readable memory.
        char* page = static_cast<char*>(VirtualAlloc(nullptr, 2 * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD old = 0;
        VirtualProtect(page + 4096, 4096, PAGE_NOACCESS, &old);
        std::memset(page + 4096 - 4, 'z', 4);  // no NUL before the guard page
        CHECK(contains(cdmp::session2::report_describe(page + 4096 - 4, &none), "reason 'zzzz(unreadable)'"));
        VirtualFree(page, 0, MEM_RELEASE);
    }

    // The fake is the function body itself, not an incremental-link jmp thunk.
    const auto* body = reinterpret_cast<const uint8_t*>(&fake_reporter);
    CHECK(body[0] != 0xE9);
    uint8_t head[8];
    std::memcpy(head, body, sizeof head);

    // Before the hook the reporter runs.
    g_call(&on, "before", &files);
    CHECK(g_reached == 1);
    CHECK(!cdmp::session2::report_guard_active());

    // Wrong first bytes: nothing is installed.
    uint8_t wrong[8];
    std::memcpy(wrong, head, sizeof wrong);
    wrong[7] ^= 0xFF;
    CHECK(!cdmp::session2::report_guard_install_at(reinterpret_cast<void*>(&fake_reporter), wrong, sizeof wrong));
    CHECK(!cdmp::session2::report_guard_install_at(nullptr, head, sizeof head));
    CHECK(!cdmp::session2::report_guard_active());
    g_call(&on, "still runs", &files);
    CHECK(g_reached == 2);

    // With the hook nothing of the reporter runs, whatever the arguments.
    CHECK(cdmp::session2::report_guard_install_at(reinterpret_cast<void*>(&fake_reporter), head, sizeof head));
    CHECK(cdmp::session2::report_guard_active());
    CHECK(body[0] == 0xE9 || body[0] == 0xFF);  // patched
    uint8_t off = 0;
    g_call(&on, "Savedata Save Fail Event", &files);
    g_call(&on, "!", &files);
    g_call(&off, "sentry off", &files);
    g_call(nullptr, nullptr, nullptr);
    CHECK(g_reached == 2);
    CHECK(cdmp::session2::report_guard_dropped() == 4);
    CHECK(cdmp::session2::report_guard_dropped_live() == 2);
    // A second install is a no-op that keeps it on (the head is ours now).
    CHECK(cdmp::session2::report_guard_install_at(reinterpret_cast<void*>(&fake_reporter), head, sizeof head));
    g_call(&on, "again", &files);
    CHECK(g_reached == 2);
    CHECK(cdmp::session2::report_guard_dropped() == 5);
    // At most 16 lines in the log, however many reports.
    for (int i = 0; i < 30; ++i) g_call(&on, "flood", &files);
    CHECK(cdmp::session2::report_guard_dropped() == 35);
    CHECK(cdmp::session2::report_guard_dropped_live() == 33);

    cdmp::log::close();
    std::ifstream in(log_path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    size_t lines = 0;
    for (size_t p = text.find("NOT sent"); p != std::string::npos; p = text.find("NOT sent", p + 1)) ++lines;
    CHECK(lines == 16);
    CHECK(contains(text, "Sentry was ON: it would have gone to Pearl Abyss); NOT sent: reason 'Savedata Save Fail Event', 3 files: "));
    CHECK(contains(text, "Sentry was off: the game would not have sent it either); NOT sent: reason 'sentry off'"));
    CHECK(contains(text, "Sentry state unreadable); NOT sent: reason '(null)', files unreadable"));
    in.close();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    if (g_failures == 0) std::printf("optd report tests: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
