// Two copies of the game on one PC (src/mod/role_logic.h, docs/research/dos-instancias.md): CDMP_ROLE's parsing, the
// ini choice, the role-tagged file names (and that the default log's pruning never takes them), the guest's save
// folder as a redirect of the real one, and the log itself: a host-role process and a guest-role one (and a second
// process of the host role) writing next to each other in one folder never touch each other's files.
//
// SCOPE: pure logic plus src/mod/log.cpp on a folder in %TEMP% (child processes for the other roles). Not the game.
// The guest's save redirect with the real KernelBase hooks: optd_guest_role_tests.cpp.

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "common/save_guard.h"
#include "mod/log.h"
#include "mod/log_names.h"
#include "mod/role.h"
#include "mod/role_logic.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using cdmp::role::Role;
namespace role = cdmp::role;
namespace names = cdmp::log::names;

void test_parse() {
    CHECK(role::parse(L"") == Role::None);
    CHECK(role::parse(L"  \t") == Role::None);
    CHECK(role::parse(L"host") == Role::Host);
    CHECK(role::parse(L"GUEST") == Role::Guest);
    CHECK(role::parse(L" Guest \r\n") == Role::Guest);
    CHECK(!role::parse(L"gest").has_value());
    CHECK(!role::parse(L"host2").has_value());
    CHECK(!role::parse(L"none").has_value());
    CHECK(!role::parse(L"ho st").has_value());
    std::printf("CDMP_ROLE parsing: OK\n");
}

void test_names() {
    // No role: exactly the names of before.
    CHECK(role::tag(Role::None).empty());
    CHECK(role::log_name(Role::None) == L"cdmp.log");
    CHECK(role::log_name(Role::None, /*extra=*/true) == L"cdmp.log");
    CHECK(role::tagged(L"cdmp_packets", Role::None) == L"cdmp_packets");
    CHECK(role::tagged(L"cdmp_crash", Role::None) == L"cdmp_crash");
    // Roles.
    CHECK(role::log_name(Role::Host) == L"cdmp.host.log");
    CHECK(role::log_name(Role::Guest) == L"cdmp.guest.log");
    CHECK(role::log_name(Role::Host, true) == L"cdmp.host.extra.log");
    CHECK(role::tagged(L"cdmp_packets", Role::Guest) == L"cdmp_packets.guest");
    CHECK(role::tagged(L"cdmp_crash", Role::Host) == L"cdmp_crash.host");
    CHECK(role::mutex_name(Role::Host) == L"Local\\cdmp_role_host");
    CHECK(role::mutex_name(Role::Guest) == L"Local\\cdmp_role_guest");
    CHECK(role::mutex_name(Role::Host) != role::mutex_name(Role::Guest));

    // The process-wide role (role.h): None until set.
    CHECK(role::current() == Role::None);
    CHECK(role::tagged(L"cdmp_packets") == L"cdmp_packets");
    role::set_current(Role::Guest);
    CHECK(role::tagged(L"cdmp_packets") == L"cdmp_packets.guest");
    role::set_current(Role::None);

    // The dated logs: each stem's pruning only sees its own files (log.cpp prune -> names::to_delete).
    const std::wstring stamp = names::stamp(2026, 10, 14, 18, 30, 5);
    const std::wstring host = names::dated(L"cdmp.host", stamp, 1, L".log");
    const std::wstring guest = names::dated(L"cdmp.guest", stamp, 1, L".log");
    const std::wstring extra = names::dated(L"cdmp.host.extra", stamp, 2, L".log");
    const std::wstring plain = names::dated(L"cdmp", stamp, 1, L".log");
    CHECK(host == L"cdmp.host_20261014-183005.log");
    CHECK(extra == L"cdmp.host.extra_20261014-183005_2.log");
    CHECK(names::is_dated(host, L"cdmp.host", L".log"));
    CHECK(!names::is_dated(host, L"cdmp", L".log"));
    CHECK(!names::is_dated(guest, L"cdmp.host", L".log"));
    CHECK(!names::is_dated(extra, L"cdmp.host", L".log"));
    CHECK(!names::is_dated(plain, L"cdmp.host", L".log"));
    std::vector<std::wstring> many;
    for (unsigned i = 1; i <= 12; ++i) many.push_back(names::dated(L"cdmp.guest", stamp, i, L".log"));
    many.push_back(host);
    many.push_back(plain);
    const auto gone = names::to_delete(many, L"cdmp.guest", L".log", 10);
    CHECK(gone.size() == 2);
    for (const auto& g : gone) CHECK(names::is_dated(g, L"cdmp.guest", L".log"));
    CHECK(names::to_delete(many, L"cdmp", L".log", 0).size() == 1);  // only cdmp_<stamp>.log is the plain stem's
    std::printf("Role-tagged file names: OK\n");
}

void test_ini() {
    auto c = role::choose_ini(Role::None, true, true);
    CHECK(c.name == L"cdmp.ini" && !c.fell_back);
    c = role::choose_ini(Role::None, false, false);
    CHECK(c.name == L"cdmp.ini" && !c.fell_back);
    c = role::choose_ini(Role::Host, true, true);
    CHECK(c.name == L"cdmp.host.ini" && !c.fell_back);
    c = role::choose_ini(Role::Guest, true, false);
    CHECK(c.name == L"cdmp.guest.ini" && !c.fell_back);
    c = role::choose_ini(Role::Guest, false, true);  // the only case that reads cdmp.ini: a warning
    CHECK(c.name == L"cdmp.ini" && c.fell_back);
    c = role::choose_ini(Role::Host, false, false);  // neither: the default is written as the role's own
    CHECK(c.name == L"cdmp.host.ini" && !c.fell_back);
    std::printf("Ini choice: OK\n");
}

void test_save_dirs() {
    const std::wstring local = L"C:\\Users\\x\\AppData\\Local";
    const std::wstring save = role::save_dir(local), guest = role::guest_save_dir(local);
    CHECK(save == L"C:\\Users\\x\\AppData\\Local\\Pearl Abyss\\CD\\save");
    CHECK(guest == L"C:\\Users\\x\\AppData\\Local\\Pearl Abyss\\CD\\save_cdmp_guest");
    // Neither inside the other (component boundary): the redirect is valid and its target is never "under save".
    CHECK(!cdmp::saveguard::is_under(guest, save));
    CHECK(!cdmp::saveguard::is_under(save, guest));
    const cdmp::saveguard::Redirector r(save, guest);
    CHECK(r.valid());
    CHECK(r.map(save + L"\\1234567890\\slot108\\save.save") == guest + L"\\1234567890\\slot108\\save.save");
    CHECK(r.map(save) == guest);
    CHECK(r.map(L"c:/users/X/appdata/local/pearl abyss/cd/SAVE/1/slot0_new/lobby.save") ==
          guest + L"/1/slot0_new/lobby.save");  // the rest exactly as written (save_guard.h redirect_path)
    CHECK(!r.map(guest + L"\\1\\slot108\\save.save").has_value());  // already the guest's: through untouched
    CHECK(!r.map(L"C:\\Users\\x\\AppData\\Local\\Pearl Abyss\\CD\\cdmp_save_sandbox\\1\\save.save").has_value());
    CHECK(!r.under(L"C:\\Users\\x\\AppData\\Local\\Pearl Abyss\\CD\\savegame\\x"));
    std::printf("Guest save folder mapping: OK\n");
}

// ------------------------------------------------------------------ the log: three processes, one folder

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool has(const std::string& text, std::string_view what) { return text.find(what) != std::string::npos; }

std::wstring self_path() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

int run_child(const std::wstring& args) {
    std::wstring cmd = L"\"" + self_path() + L"\" " + args;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

// A child: opens `dir`\<log_name(role, extra)> as the mod would, writes one line, closes.
int child_log(const std::wstring& dir, const std::wstring& which) {
    const Role r = which == L"guest" ? Role::Guest : Role::Host;
    const bool extra = which == L"extra";
    if (!cdmp::log::open(dir + L"\\" + role::log_name(r, extra))) return 2;
    cdmp::log::info("child {} line", extra ? "extra" : role::name_a(r));
    cdmp::log::close();
    return 0;
}

void test_logs() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"cdmp_optd_twoinst_" + std::to_wstring(GetCurrentProcessId());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);

    // The host's main process keeps its log open (as the game does) while the others start.
    CHECK(cdmp::log::open(dir + L"\\" + role::log_name(Role::Host)));
    const std::wstring host_dated = cdmp::log::current_path();
    cdmp::log::info("host main line");
    CHECK(cdmp::log::flush(3000));
    CHECK(run_child(L"log \"" + dir + L"\" guest") == 0);
    CHECK(run_child(L"log \"" + dir + L"\" extra") == 0);  // the game's second process of the host role
    cdmp::log::info("host main second line");
    cdmp::log::close();

    const std::filesystem::path d(dir);
    CHECK(std::filesystem::path(host_dated).filename().wstring().rfind(L"cdmp.host_", 0) == 0);
    const std::string host_alias = read_file(d / L"cdmp.host.log");
    CHECK(has(host_alias, "host main line") && has(host_alias, "host main second line"));
    CHECK(!has(host_alias, "child"));
    const std::string guest_alias = read_file(d / L"cdmp.guest.log");
    CHECK(has(guest_alias, "child guest line") && !has(guest_alias, "host main"));
    const std::string extra_alias = read_file(d / L"cdmp.host.extra.log");
    CHECK(has(extra_alias, "child extra line") && !has(extra_alias, "host main"));
    CHECK(!std::filesystem::exists(d / L"cdmp.log"));  // the role-less name is nobody's here
    size_t host_dated_files = 0, guest_dated = 0, extra_dated = 0;
    for (const auto& e : std::filesystem::directory_iterator(d)) {
        const std::wstring n = e.path().filename().wstring();
        host_dated_files += names::is_dated(n, L"cdmp.host", L".log");
        guest_dated += names::is_dated(n, L"cdmp.guest", L".log");
        extra_dated += names::is_dated(n, L"cdmp.host.extra", L".log");
    }
    CHECK(host_dated_files == 1 && guest_dated == 1 && extra_dated == 1);
    std::filesystem::remove_all(dir, ec);
    std::printf("Logs of three processes in one folder: OK\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 4 && std::wstring_view(argv[1]) == L"log") return child_log(argv[2], argv[3]);
    test_parse();
    test_names();
    test_ini();
    test_save_dirs();
    test_logs();
    if (g_failures == 0) std::printf("All two-instance tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
