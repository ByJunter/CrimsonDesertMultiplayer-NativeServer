#include "mod/session2/host_save_export.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/save_guard.h"
#include "common/sha256.h"
#include "mod/log.h"

#if __has_include("build_id.h")
#include "build_id.h"
#define CDMP_HOST_SAVE_HAVE_BUILD_ID 1
#endif

namespace cdmp::session2 {
namespace {

std::atomic<hsv::Mode> g_mode{hsv::Mode::Sandbox};
std::wstring g_dir;  // written once by configure (before any other call)
uint32_t g_keep = hsv::kDefaultKeep;
std::atomic<bool> g_dir_ok{false};  // the last archive step found the export folder fine (the journal may go there)
std::atomic<int> g_inject{0};       // tests only (host_save_test_inject)

std::mutex g_mu;  // the journal and the describe text
HANDLE g_journal = INVALID_HANDLE_VALUE;
uint64_t g_seq = 0;
uint32_t g_events = 0;
bool g_cut = false;
std::string g_run, g_run_start, g_last = "no export yet";

constexpr uint64_t kMaxManifestBytes = 16ull * 1024 * 1024;
constexpr uint64_t kMaxJournalBytes = 16ull * 1024 * 1024;
constexpr size_t kKeepJournals = 200;
constexpr int kMaxDepth = 16;

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string mod_build_text() {
#ifdef CDMP_HOST_SAVE_HAVE_BUILD_ID
    return std::format("0x{:08x}{}", static_cast<uint32_t>(CDMP_BUILD_ID), CDMP_BUILD_DIRTY ? " dirty" : "");
#else
    return "unknown";
#endif
}

hsv::Stamp now_stamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return {st.wYear, static_cast<uint8_t>(st.wMonth), static_cast<uint8_t>(st.wDay), static_cast<uint8_t>(st.wHour),
            static_cast<uint8_t>(st.wMinute), static_cast<uint8_t>(st.wSecond)};
}

std::string local_time_text() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                       st.wSecond, st.wMilliseconds);
}

std::wstring strip_seps(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}
std::wstring parent_of(const std::wstring& p) {
    const std::wstring s = strip_seps(p);
    const size_t k = s.find_last_of(L"\\/");
    if (k == std::wstring::npos || k + 1 >= s.size()) return {};
    return k == 2 && s[1] == L':' ? s.substr(0, 3) : s.substr(0, k);
}
std::wstring leaf_of(const std::wstring& p) {
    const std::wstring s = strip_seps(p);
    const size_t k = s.find_last_of(L"\\/");
    return k == std::wstring::npos ? s : s.substr(k + 1);
}

DWORD attrs_of(const std::wstring& p) { return GetFileAttributesW(p.c_str()); }
bool exists(const std::wstring& p) { return attrs_of(p) != INVALID_FILE_ATTRIBUTES; }
bool is_dir(const std::wstring& p) {
    const DWORD a = attrs_of(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool is_dots(const wchar_t* n) { return n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)); }

bool make_dirs(const std::wstring& p) {
    if (p.empty()) return false;
    if (is_dir(p)) return true;
    const std::wstring up = parent_of(p);
    if (!up.empty() && up != p && !make_dirs(up)) return false;
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring full_path(const std::wstring& p) {
    if (p.empty()) return {};
    std::vector<wchar_t> buf(MAX_PATH);
    for (int i = 0; i < 3; ++i) {
        const DWORD n = GetFullPathNameW(p.c_str(), static_cast<DWORD>(buf.size()), buf.data(), nullptr);
        if (n == 0) return p;
        if (n < buf.size()) return std::wstring(buf.data(), n);
        buf.resize(n + 1);
    }
    return p;
}

std::wstring exe_dir() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return parent_of(std::wstring(buf.data(), n));
        buf.resize(buf.size() * 2);
    }
}

// A steam_autocloud.vdf in `dir` or above it (Steam writes it into the Auto-Cloud roots it syncs).
std::optional<std::wstring> autocloud_marker(const std::wstring& dir) {
    std::wstring p = strip_seps(dir);
    for (int i = 0; i < 64 && !p.empty(); ++i) {
        const std::wstring sep = p.back() == L'\\' ? L"" : L"\\";
        if (exists(p + sep + L"steam_autocloud.vdf")) return p;
        const std::wstring up = parent_of(p);
        if (up.empty() || up == p) break;
        p = up;
    }
    return std::nullopt;
}

// The folder's volume serial and file id: the same folder after a rename on its volume, a new one when recreated.
std::string dir_id(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION i{};
    const BOOL ok = GetFileInformationByHandle(h, &i);
    CloseHandle(h);
    if (!ok) return {};
    return std::format("{:08X}:{:08X}{:08X}", i.dwVolumeSerialNumber, i.nFileIndexHigh, i.nFileIndexLow);
}

std::optional<hsv::NodeId> node_id(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION i{};
    const BOOL ok = GetFileInformationByHandle(h, &i);
    CloseHandle(h);
    if (!ok) return std::nullopt;
    return hsv::NodeId{i.dwVolumeSerialNumber, (static_cast<uint64_t>(i.nFileIndexHigh) << 32) | i.nFileIndexLow};
}

bool is_reparse(const std::wstring& path) {
    const DWORD a = attrs_of(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// The path the file system resolves `path` to (links followed), "\\?\" dropped; empty if it cannot be opened.
std::wstring final_path(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::vector<wchar_t> buf(MAX_PATH);
    DWORD n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    if (n >= buf.size()) {
        buf.resize(n + 1);
        n = GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()), FILE_NAME_NORMALIZED);
    }
    CloseHandle(h);
    if (n == 0 || n >= buf.size()) return {};
    std::wstring s(buf.data(), n);
    if (s.rfind(L"\\\\?\\UNC\\", 0) == 0) return L"\\\\" + s.substr(8);
    if (s.rfind(L"\\\\?\\", 0) == 0) return s.substr(4);
    return s;
}

// Review of phase E, M1: what the file system says about the export folder (hsv::resolved_dir_problem), and the text
// rules again on its resolved spelling.
std::optional<std::string> resolved_problem(const std::wstring& root, const std::wstring& save_dir,
                                            const std::wstring& game_root) {
    hsv::ResolvedDir r;
    r.readable = true;
    std::wstring p = strip_seps(root), rest, base;
    for (int i = 0; i < 64 && !p.empty(); ++i) {
        if (attrs_of(p) != INVALID_FILE_ATTRIBUTES) {
            if (is_reparse(p)) r.reparse = true;
            const auto id = node_id(p);
            if (!id) r.readable = false;
            else r.root_ancestors.push_back(*id);
            if (i == 0 && id) r.root = id;
            if (base.empty()) base = p;
        } else if (base.empty()) {
            rest = L"\\" + leaf_of(p) + rest;
        }
        const std::wstring up = parent_of(p);
        if (up.empty() || up == p) break;
        p = up;
    }
    for (const std::wstring& f : {save_dir, game_root}) {
        const auto id = node_id(f);
        if (!id) r.readable = false;
        else r.forbidden.push_back(*id);
    }
    const std::wstring save_final = final_path(save_dir), game_final = final_path(game_root);
    if (save_final.empty() || game_final.empty() || base.empty()) r.readable = false;
    for (std::wstring q = save_final; !q.empty();) {
        if (const auto id = node_id(q)) r.save_ancestors.push_back(*id);
        const std::wstring up = parent_of(q);
        if (up.empty() || up == q) break;
        q = up;
    }
    if (auto bad = hsv::resolved_dir_problem(r)) return bad;
    const std::wstring resolved = strip_seps(final_path(base)) + rest;
    if (auto bad = hsv::export_dir_problem(resolved, save_final, game_final, [](std::wstring_view a, std::wstring_view b) {
            return saveguard::is_under(a, b);
        })) {
        return std::format("resolved as '{}': {}", narrow(resolved), *bad);
    }
    return std::nullopt;
}

uint64_t ft64(const FILETIME& f) { return (static_cast<uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime; }

bool read_text(const std::wstring& path, std::string& out, uint64_t max) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && static_cast<uint64_t>(size.QuadPart) <= max;
    if (ok) {
        out.assign(static_cast<size_t>(size.QuadPart), '\0');
        DWORD got = 0;
        ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(h);
    return ok;
}

bool write_new_text(const std::wstring& path, const std::string& text, std::string& why) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = std::format("cannot create '{}' (error {})", narrow(path), GetLastError());
        return false;
    }
    DWORD w = 0;
    const bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &w, nullptr) && w == text.size() &&
                    FlushFileBuffers(h);
    const DWORD e = GetLastError();
    CloseHandle(h);
    if (!ok) why = std::format("cannot write '{}' (error {})", narrow(path), e);
    return ok;
}

// SHA-256 and size of a file, read in 64 KB pieces. `share_all`: the real save folder's files (probe's comparison),
// opened so that nothing the game or Steam does is ever refused because of this read (review L6).
bool hash_file(const std::wstring& path, std::string& sha, uint64_t& size, std::string& why, DWORD* error = nullptr,
               bool share_all = false) {
    const DWORD share = share_all ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (error) *error = e;
        why = std::format("cannot open '{}' (error {})", narrow(path), e);
        return false;
    }
    sha256::Hasher hs;
    std::vector<uint8_t> buf(64 * 1024);
    size = 0;
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            why = std::format("cannot read '{}' (error {})", narrow(path), GetLastError());
            ok = false;
            break;
        }
        if (got == 0) break;
        hs.update(std::span<const uint8_t>(buf.data(), got));
        size += got;
    }
    CloseHandle(h);
    if (ok) sha = gsv::hex(hs.finish());
    return ok;
}

// The first 0x80 bytes and the size, for gsv::check_header.
gsv::HeaderCheck header_of(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return gsv::HeaderCheck::TooShort;
    std::array<uint8_t, gsv::kHeaderSize> head{};
    DWORD got = 0;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(h, &size) && ReadFile(h, head.data(), static_cast<DWORD>(head.size()), &got, nullptr);
    CloseHandle(h);
    if (!ok) return gsv::HeaderCheck::TooShort;
    return gsv::check_header(std::span<const uint8_t>(head.data(), got), static_cast<uint64_t>(size.QuadPart));
}

struct Item {
    std::wstring rel;  // relative to the source folder
    bool dir = false;
    uint64_t size = 0;
    FILETIME created{}, accessed{}, written{};
};

// Everything in `root` (folders before their contents). A link (reparse point) fails the scan: never followed.
// steam_autocloud.vdf (Steam Cloud's marker) is left out and counted.
bool scan(const std::wstring& root, const std::wstring& rel, std::vector<Item>& out, uint32_t& skipped,
          std::string& why, int depth = 0) {
    if (depth > kMaxDepth) {
        why = std::format("'{}' is nested deeper than {} folders", narrow(root + L"\\" + rel), kMaxDepth);
        return false;
    }
    const std::wstring here = rel.empty() ? root : root + L"\\" + rel;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((here + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND) return true;
        why = std::format("cannot list '{}' (error {})", narrow(here), e);
        return false;
    }
    bool ok = true;
    do {
        if (is_dots(fd.cFileName)) continue;
        const std::wstring r = rel.empty() ? std::wstring(fd.cFileName) : rel + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            why = std::format("'{}' is a link (reparse point): not followed", narrow(root + L"\\" + r));
            ok = false;
        } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            out.push_back({r, true, 0, fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime});
            ok = scan(root, r, out, skipped, why, depth + 1);
        } else if (_wcsicmp(fd.cFileName, L"steam_autocloud.vdf") == 0) {
            ++skipped;
        } else {
            out.push_back({r, false, (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow,
                           fd.ftCreationTime, fd.ftLastAccessTime, fd.ftLastWriteTime});
        }
    } while (ok && FindNextFileW(f, &fd));
    const DWORD end = ok ? GetLastError() : static_cast<DWORD>(ERROR_NO_MORE_FILES);
    FindClose(f);
    if (ok && end != ERROR_NO_MORE_FILES) {
        why = std::format("the listing of '{}' stopped early (error {})", narrow(here), end);
        return false;
    }
    return ok;
}

bool set_times(const std::wstring& path, const Item& it) {
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const BOOL ok = SetFileTime(h, &it.created, &it.accessed, &it.written);
    CloseHandle(h);
    return ok != FALSE;
}

// Copies `src` to the new file `dst` hashing what it reads, flushes the copy to disk, then reads the copy back and
// compares size and SHA-256 with the source's. `sha` = the source's hash. A save file is opened denying writers;
// `share_write` only for a journal some process may still hold open (its size is checked against `it` anyway).
bool copy_verified(const std::wstring& src, const std::wstring& dst, const Item& it, std::string& sha,
                   std::string& why, bool share_write = false) {
    HANDLE in = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ | (share_write ? FILE_SHARE_WRITE : 0u),
                            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (in == INVALID_HANDLE_VALUE) {
        why = std::format("cannot open '{}' (error {})", narrow(src), GetLastError());
        return false;
    }
    HANDLE out = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) {
        why = std::format("cannot create '{}' (error {})", narrow(dst), GetLastError());
        CloseHandle(in);
        return false;
    }
    sha256::Hasher hs;
    std::vector<uint8_t> buf(64 * 1024);
    uint64_t total = 0;
    bool ok = true;
    for (;;) {
        DWORD got = 0, put = 0;
        if (!ReadFile(in, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            why = std::format("cannot read '{}' (error {})", narrow(src), GetLastError());
            ok = false;
            break;
        }
        if (got == 0) break;
        hs.update(std::span<const uint8_t>(buf.data(), got));
        total += got;
        if (!WriteFile(out, buf.data(), got, &put, nullptr) || put != got) {
            why = std::format("cannot write '{}' (error {})", narrow(dst), GetLastError());
            ok = false;
            break;
        }
    }
    if (ok && !FlushFileBuffers(out)) {
        why = std::format("cannot flush '{}' (error {})", narrow(dst), GetLastError());
        ok = false;
    }
    CloseHandle(out);
    CloseHandle(in);
    if (!ok) return false;
    if (total != it.size) {
        why = std::format("'{}' changed while it was copied ({} bytes listed, {} read)", narrow(src), it.size, total);
        return false;
    }
    sha = gsv::hex(hs.finish());
    (void)set_times(dst, it);  // the write time matters (the pair check); a failure is not fatal
    std::string back;
    uint64_t back_size = 0;
    if (!hash_file(dst, back, back_size, why)) return false;
    if (back_size != total || back != sha) {
        why = std::format("the copy '{}' does not read back as the source (size {} / {}, SHA-256 {})", narrow(dst),
                          back_size, total, back == sha ? "same" : "differs");
        return false;
    }
    return true;
}

// Deletes `path` (a folder or file this code made under `root`), never the save folder or anything around it. A link
// (reparse point) anywhere in it is REFUSED, neither followed nor removed (review M1): the tree is then left partly
// deleted and false is returned.
bool delete_own_tree(const std::wstring& path, const std::wstring& root, const std::wstring& save_dir) {
    if (!saveguard::is_under(path, root) || saveguard::normalize(path) == saveguard::normalize(root) ||
        saveguard::is_under(path, save_dir) || saveguard::is_under(save_dir, path)) {
        return false;
    }
    const DWORD a = attrs_of(path);
    if (a == INVALID_FILE_ATTRIBUTES) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) {
        log::warn("Session2 host save: '{}' is a link (reparse point): not deleted", narrow(path));
        return false;
    }
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(path.c_str()) != FALSE;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((path + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    bool ok = true;
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (!is_dots(fd.cFileName)) ok = delete_own_tree(path + L"\\" + fd.cFileName, root, save_dir) && ok;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return ok && RemoveDirectoryW(path.c_str());
}

// ---------------------------------------------------------------- what is already exported

struct Gen {
    std::wstring name;
    bool partial = false;
    bool valid = false;
    hsv::Manifest manifest;
};

// The generations named sources it is identical to (the "<generation>" part of identical_to).
std::vector<std::wstring> refs_of(const hsv::Manifest& m) {
    std::vector<std::wstring> out;
    for (const hsv::SourceRecord& s : m.sources) {
        if (const size_t k = s.identical_to.find('/'); k != std::string::npos) out.push_back(widen(s.identical_to.substr(0, k)));
    }
    return out;
}

std::vector<Gen> list_generations(const std::wstring& root) {
    std::vector<Gen> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((root + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (is_dots(fd.cFileName) || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        std::wstring name = fd.cFileName;
        Gen g;
        g.name = name;
        const bool partial = name.size() > hsv::kPartialSuffix.size() && name.ends_with(hsv::kPartialSuffix);
        const std::wstring_view bare =
            partial ? std::wstring_view(name).substr(0, name.size() - hsv::kPartialSuffix.size()) : std::wstring_view(name);
        if (!hsv::parse_generation_name(bare)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            // Never read, deduplicated against or pruned (review M1); it still holds its name.
            log::warn("Session2 host save: '{}' is a link (reparse point): not used as a generation",
                      narrow(root + L"\\" + name));
            g.partial = true;
            out.push_back(std::move(g));
            continue;
        }
        if (partial) {
            g.partial = true;
            out.push_back(std::move(g));
            continue;
        }
        std::string text;
        if (read_text(root + L"\\" + name + L"\\" + widen(std::string(hsv::kManifestFile)), text, kMaxManifestBytes)) {
            if (auto m = hsv::parse_manifest(text)) {
                g.valid = m->complete && m->generation == narrow(name);
                g.manifest = std::move(*m);
            }
        }
        out.push_back(std::move(g));
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

// Review M2: the files of `label` in `g` as they are on disk NOW (present, size, SHA-256), before anything is taken
// as already exported there.
bool generation_source_intact(const std::wstring& root, const Gen& g, const std::string& label, std::string& why) {
    const std::wstring dir = root + L"\\" + g.name, base = dir + L"\\" + widen(label);
    if (is_reparse(dir) || is_reparse(base)) {
        why = "a link (reparse point) in it";
        return false;
    }
    for (const hsv::FileRecord& f : g.manifest.files) {
        if (f.source != label) continue;
        std::string sha;
        uint64_t size = 0;
        if (!hash_file(base + L"\\" + widen(f.path), sha, size, why)) return false;
        if (size != f.size || sha != f.sha256) {
            why = std::format("'{}' does not match its manifest", f.path);
            return false;
        }
    }
    return true;
}

struct RunJournal {
    std::wstring path;
    hsv::Journal journal;
};

std::vector<RunJournal> load_journals(const std::wstring& runs) {
    std::vector<RunJournal> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((runs + L"\\*.txt").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring p = runs + L"\\" + fd.cFileName;
        std::string text;
        if (!read_text(p, text, kMaxJournalBytes)) continue;
        if (auto j = hsv::parse_journal(text)) out.push_back({p, std::move(*j)});
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

// The run journals beyond the newest kKeepJournals (by name: run ids start with their time).
void prune_journals(const std::wstring& runs, const std::wstring& root, const std::wstring& save_dir) {
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((runs + L"\\*.txt").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) names.emplace_back(fd.cFileName);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    if (names.size() <= kKeepJournals) return;
    std::sort(names.begin(), names.end());
    for (size_t i = 0; i + kKeepJournals < names.size(); ++i) (void)delete_own_tree(runs + L"\\" + names[i], root, save_dir);
}

// ---------------------------------------------------------------- one source folder

struct Source {
    std::string label;
    std::wstring path;
    bool aside = false;
    std::vector<Item> items;
    uint32_t skipped = 0;  // steam_autocloud.vdf left out
    std::vector<hsv::TreeFile> tree;
    std::string tree_id;
    std::string identical_to;  // "<generation>/<label>"
    std::wstring identical_gen;
    const RunJournal* journal = nullptr;
    uint64_t bytes = 0;
};

bool hash_source(Source& s, std::string& why) {
    for (const Item& it : s.items) {
        if (it.dir) continue;
        hsv::TreeFile t;
        t.path = narrow(it.rel);
        uint64_t size = 0;
        if (!hash_file(s.path + L"\\" + it.rel, t.sha256, size, why)) return false;
        if (size != it.size) {
            why = std::format("'{}' changed while it was read", narrow(s.path + L"\\" + it.rel));
            return false;
        }
        t.size = size;
        s.bytes += size;
        s.tree.push_back(std::move(t));
    }
    return true;
}

// The real folder's file `rel` (account\slotN\file) against the tree's: true identical, false different or absent,
// nullopt when it cannot be read. READ ONLY.
std::optional<bool> same_as_real(const std::wstring& save_dir, const std::string& rel, const hsv::TreeFile* mine) {
    if (!mine) return std::nullopt;
    std::string sha, why;
    uint64_t size = 0;
    DWORD e = 0;
    if (!hash_file(save_dir + L"\\" + widen(rel), sha, size, why, &e, true)) {
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return false;
        return std::nullopt;
    }
    return size == mine->size && sha == mine->sha256;
}

const hsv::TreeFile* find_file(const Source& s, const std::string& rel) {
    const std::string low = hsv::lower_ascii(rel);
    for (const hsv::TreeFile& t : s.tree) {
        if (hsv::lower_ascii(t.path) == low) return &t;
    }
    return nullptr;
}
const Item* find_item(const Source& s, const std::wstring& rel) {
    for (const Item& it : s.items) {
        if (!it.dir && _wcsicmp(it.rel.c_str(), rel.c_str()) == 0) return &it;
    }
    return nullptr;
}

// The account folders, slot folders and (probe) the verdicts of one copied source, from the EXPORTED copy.
void describe_slots(const Source& s, const std::wstring& copy_root, const std::wstring& save_dir, bool probe,
                    const std::string& gen, hsv::Manifest& m) {
    for (const Item& acc : s.items) {
        if (!acc.dir || acc.rel.find(L'\\') != std::wstring::npos || !hsv::account_folder(acc.rel)) continue;
        const std::string account = narrow(acc.rel);
        m.accounts.push_back(s.label + "|" + account);
        for (const Item& sl : s.items) {
            if (!sl.dir || sl.rel.size() <= acc.rel.size() + 1 ||
                _wcsnicmp(sl.rel.c_str(), (acc.rel + L"\\").c_str(), acc.rel.size() + 1) != 0) {
                continue;
            }
            const std::wstring folder = sl.rel.substr(acc.rel.size() + 1);
            if (folder.find(L'\\') != std::wstring::npos) continue;
            const auto parsed = hsv::parse_slot_folder(folder);
            if (!parsed) continue;
            m.slots.push_back({s.label, account, narrow(folder),
                               parsed->kind == hsv::FolderKind::Slot
                                   ? hsv::slot_class_name(hsv::slot_class(parsed->number))
                                   : hsv::folder_kind_name(parsed->kind)});
            if (!probe) continue;
            hsv::SlotFacts f;
            f.folder = parsed;
            const std::wstring lobby_rel = sl.rel + L"\\lobby.save", save_rel = sl.rel + L"\\save.save";
            const Item* lobby = find_item(s, lobby_rel);
            const Item* save = find_item(s, save_rel);
            f.lobby = lobby != nullptr;
            f.save = save != nullptr;
            if (lobby) f.lobby_check = header_of(copy_root + L"\\" + lobby_rel);
            if (save) f.save_check = header_of(copy_root + L"\\" + save_rel);
            if (lobby && save) {
                const auto l = same_as_real(save_dir, narrow(lobby_rel), find_file(s, narrow(lobby_rel)));
                const auto r = same_as_real(save_dir, narrow(save_rel), find_file(s, narrow(save_rel)));
                if (l && r) f.identical_to_real = *l && *r;
                const uint64_t a = ft64(lobby->written), b = ft64(save->written);
                f.pair_gap_ms = (a > b ? a - b : b - a) / 10000;
            }
            f.obs = hsv::observe_slot(s.journal ? &s.journal->journal : nullptr, parsed->number);
            const hsv::Classification c = hsv::classify_slot(f);
            m.probes.push_back({s.label, account, narrow(folder), hsv::verdict_name(c.verdict), c.reasons});
            log::info("Session2 host save probe: {}/{} {}\\{} = {}: {}", gen, s.label, account, narrow(folder),
                      hsv::verdict_name(c.verdict), c.reasons);
        }
    }
}

bool copy_source(const Source& s, const std::wstring& dst_root, hsv::Manifest& m, std::string& why) {
    if (!CreateDirectoryW(dst_root.c_str(), nullptr)) {
        why = std::format("cannot create '{}' (error {})", narrow(dst_root), GetLastError());
        return false;
    }
    for (const Item& it : s.items) {
        const std::wstring dst = dst_root + L"\\" + it.rel;
        if (it.dir) {
            if (!CreateDirectoryW(dst.c_str(), nullptr)) {
                why = std::format("cannot create '{}' (error {})", narrow(dst), GetLastError());
                return false;
            }
            continue;
        }
        std::string sha;
        if (!copy_verified(s.path + L"\\" + it.rel, dst, it, sha, why)) return false;
        const hsv::TreeFile* hashed = find_file(s, narrow(it.rel));
        if (!hashed || hashed->sha256 != sha) {
            why = std::format("'{}' changed between its hash and its copy", narrow(s.path + L"\\" + it.rel));
            return false;
        }
        m.files.push_back({s.label, narrow(it.rel), it.size, sha, ft64(it.written)});
    }
    // Folder times after their contents (adding files changed them); deepest first.
    for (auto it = s.items.rbegin(); it != s.items.rend(); ++it) {
        if (it->dir) (void)set_times(dst_root + L"\\" + it->rel, *it);
    }
    return true;
}

// Copies the run journal of `s` (if any) into <partial>\journals\<label>.txt, verified, listed as source "journal".
bool copy_journal(const Source& s, const std::wstring& partial, hsv::Manifest& m, std::string& why) {
    if (!s.journal) return true;
    const std::wstring dir = partial + L"\\journals";
    if (!make_dirs(dir)) {
        why = std::format("cannot create '{}' (error {})", narrow(dir), GetLastError());
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(s.journal->path.c_str(), GetFileExInfoStandard, &a)) {
        why = std::format("cannot read '{}' (error {})", narrow(s.journal->path), GetLastError());
        return false;
    }
    Item it;
    it.rel = widen(s.label) + L".txt";
    it.size = (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
    it.created = a.ftCreationTime;
    it.accessed = a.ftLastAccessTime;
    it.written = a.ftLastWriteTime;
    std::string sha;
    // The journal of an earlier run (closed with its process; the size check catches a late append).
    if (!copy_verified(s.journal->path, dir + L"\\" + it.rel, it, sha, why, true)) return false;
    m.files.push_back({"journal", narrow(it.rel), it.size, sha, ft64(it.written)});
    return true;
}

}  // namespace

// ---------------------------------------------------------------- configuration

void host_save_configure(const HostSaveConfig& cfg) {
    const hsv::ParsedMode p = hsv::parse_mode(cfg.mode);
    if (p.refused_real) {
        log::error("Session2 host save: host_save='{}' (publishing to the real save folder) is NOT implemented: the "
                   "audit (S34 section 5) blocks it until M1/M2, provenance and recovery are proven; using sandbox",
                   cfg.mode);
    } else if (!p.known) {
        log::warn("Session2 host save: host_save='{}' is not sandbox, probe or export; using sandbox", cfg.mode);
    }
    g_keep = hsv::clamp_keep(cfg.keep);
    g_dir_ok.store(false);  // checked again by the archive step
    if (!hsv::archives(p.mode)) {
        g_mode.store(hsv::Mode::Sandbox);
        log::info("Session2 host save: host_save=sandbox (the sandbox is rebuilt from the real folder at every start; "
                  "the host's saves of a run are lost two starts later)");
        return;
    }
    std::wstring dir = cfg.dir;
    const std::wstring base = exe_dir();
    if (dir.empty()) {
        dir = base + L"\\cdmp_save_exports";
    } else if (!(dir.size() >= 3 && dir[1] == L':' && (dir[2] == L'\\' || dir[2] == L'/')) &&
               !(dir.size() >= 2 && (dir[0] == L'\\' || dir[0] == L'/') && (dir[1] == L'\\' || dir[1] == L'/'))) {
        dir = base + L"\\" + dir;
    }
    g_dir = strip_seps(full_path(dir));
    {
        std::lock_guard lk(g_mu);
        g_run = narrow(hsv::stamp_text(now_stamp())) + std::format("-p{}", GetCurrentProcessId());
        g_run_start = local_time_text();
    }
    g_mode.store(p.mode);
    log::warn("Session2 host save: host_save={} | the previous sandbox is exported to '{}' before each rebuild "
              "(verified, keep {} generations){} | the real save folder is never written",
              hsv::mode_name(p.mode), narrow(g_dir), g_keep,
              p.mode == hsv::Mode::Probe ? " | probe: each archived slot classified, Save/Flush journaled" : "");
}

hsv::Mode host_save_mode() { return g_mode.load(); }
std::wstring host_save_dir() { return hsv::archives(g_mode.load()) ? g_dir : std::wstring(); }

// ---------------------------------------------------------------- the archive before the rebuild

HostSaveArchive host_save_archive(const std::wstring& save_dir, const std::wstring& sandbox) noexcept {
    HostSaveArchive r;
    const hsv::Mode mode = g_mode.load();
    if (!hsv::archives(mode)) return r;
    try {
        const std::wstring box = strip_seps(sandbox);
        const std::wstring parent = parent_of(box), leaf = leaf_of(box);
        const hsv::Stamp stamp = now_stamp();
        // The sources: the sandbox, its _prev, and every folder set aside by an earlier failed export.
        std::vector<Source> sources;
        std::vector<std::wstring> siblings;
        {
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileExW((parent + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                        nullptr, 0);
            if (f != INVALID_HANDLE_VALUE) {
                do {
                    if (is_dots(fd.cFileName) || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    siblings.emplace_back(fd.cFileName);
                } while (FindNextFileW(f, &fd));
                FindClose(f);
            }
        }
        r.aside_name = hsv::aside_name(leaf, stamp, siblings);
        if (is_dir(box)) sources.push_back({"sandbox", box});
        if (is_dir(box + L"_prev")) sources.push_back({"prev", box + L"_prev"});
        std::sort(siblings.begin(), siblings.end());
        for (const std::wstring& s : siblings) {
            if (auto g = hsv::parse_aside_name(leaf, s)) {
                Source src{"aside-" + narrow(*g), parent + L"\\" + s};
                src.aside = true;
                sources.push_back(std::move(src));
                ++r.asides_existing;
            }
        }
        const auto fail = [&](const std::string& why) {
            log::error("Session2 host save: the export FAILED ({}): nothing is deleted; the sandbox is set aside "
                       "and archived at a later start", why);
            std::lock_guard lk(g_mu);
            g_last = "export FAILED at start: " + why;
            r.outcome = hsv::Archive::Failed;
            r.generation.clear();
            r.archived_aside.clear();
            return r;
        };
        // Where to: checked first, also when there is nothing to archive (this run's journal goes there too).
        const std::wstring root = g_dir;
        const std::wstring game_root = parent_of(parent_of(strip_seps(save_dir)));  // ...\Pearl Abyss
        std::optional<std::string> bad = hsv::export_dir_problem(
            root, save_dir, game_root, [](std::wstring_view a, std::wstring_view b) { return saveguard::is_under(a, b); });
        if (bad) {
            bad = std::format("the export folder '{}' is refused: {}", narrow(root), *bad);
        } else if (saveguard::is_under(root, box) || saveguard::is_under(box, root) ||
                   saveguard::is_under(root, box + L"_prev") || saveguard::is_under(root, parent)) {
            bad = std::format("the export folder '{}' overlaps the sandbox folders", narrow(root));
        } else if (auto marker = autocloud_marker(root)) {
            bad = std::format("'{}' has a steam_autocloud.vdf (a Steam Auto-Cloud root): not an export folder",
                              narrow(*marker));
        } else if (auto res = resolved_problem(root, save_dir, game_root)) {
            bad = std::format("the export folder '{}' is refused: {}", narrow(root), *res);
        } else if (!make_dirs(root)) {
            bad = std::format("cannot create the export folder '{}' (error {})", narrow(root), GetLastError());
        } else if (auto again = resolved_problem(root, save_dir, game_root)) {  // now that it exists
            bad = std::format("the export folder '{}' is refused: {}", narrow(root), *again);
        } else if (is_reparse(root + L"\\runs")) {
            bad = std::format("'{}\\runs' is a link (reparse point)", narrow(root));
        }
        g_dir_ok.store(!bad);
        if (sources.empty()) {
            r.outcome = hsv::Archive::Nothing;
            if (bad) log::error("Session2 host save: nothing to archive, but {}: no export nor journal in this run", *bad);
            std::lock_guard lk(g_mu);
            g_last = bad ? "export folder refused: " + *bad : "nothing to export at start";
            return r;
        }
        if (bad) return fail(*bad);
        for (const Source& s : sources) {
            if (saveguard::is_under(root, s.path) || saveguard::is_under(s.path, root)) {
                return fail(std::format("the export folder '{}' overlaps '{}'", narrow(root), narrow(s.path)));
            }
        }
        const std::wstring runs = root + L"\\runs";
        std::vector<Gen> gens = list_generations(root);
        const std::vector<RunJournal> journals = load_journals(runs);
        size_t partials = 0;
        for (const Gen& g : gens) partials += g.partial ? 1 : 0;

        // Read and hash every source (the real folder is not touched here).
        std::string why;
        for (Source& s : sources) {
            if (!scan(s.path, L"", s.items, s.skipped, why) || !hash_source(s, why)) return fail(why);
            s.tree_id = dir_id(s.path);
            for (const RunJournal& j : journals) {
                if (!s.tree_id.empty() && j.journal.header.sandbox_id == s.tree_id) s.journal = &j;
            }
            for (Gen& g : gens) {
                if (!g.valid || !s.identical_to.empty()) continue;
                for (const hsv::SourceRecord& rec : g.manifest.sources) {
                    if (!rec.identical_to.empty()) continue;
                    if (!hsv::same_tree(s.tree, hsv::tree_of(g.manifest, rec.label))) continue;
                    // Review M2: the manifest says so; the disk must too.
                    std::string damage;
                    if (!generation_source_intact(root, g, rec.label, damage)) {
                        log::warn("Session2 host save: generation {} is damaged ({}): treated as invalid, '{}' is "
                                  "copied again", narrow(g.name), damage, narrow(s.path));
                        g.valid = false;
                        break;
                    }
                    s.identical_to = narrow(g.name) + "/" + rec.label;
                    s.identical_gen = g.name;
                    break;
                }
            }
            if (!s.identical_to.empty()) {
                log::info("Session2 host save: '{}' ({}, {} files, {} bytes, written by run {}) is identical to the "
                          "verified generation {}: not copied again",
                          narrow(s.path), s.label, s.tree.size(), s.bytes, s.journal ? s.journal->journal.header.run : "unknown",
                          s.identical_to);
            }
        }
        std::vector<std::wstring> protect;
        for (const Source& s : sources) {
            if (!s.identical_gen.empty()) protect.push_back(s.identical_gen);
        }
        const bool anything_new =
            std::any_of(sources.begin(), sources.end(), [](const Source& s) { return s.identical_to.empty(); });
        if (!anything_new) {
            for (const Source& s : sources) {
                if (s.aside) r.archived_aside.push_back(s.path);
            }
            r.outcome = hsv::Archive::Done;
            prune_journals(runs, root, save_dir);  // review L5: also on starts without a new generation
            log::info("Session2 host save: nothing new to export ({} folder(s), all identical to verified "
                      "generations){}", sources.size(),
                      partials ? std::format("; {} incomplete export(s) (.partial) kept, delete them by hand", partials)
                               : std::string());
            std::lock_guard lk(g_mu);
            g_last = "nothing new at start (identical to " + sources.front().identical_to + ")";
            return r;
        }

        // The new generation, written as <name>.partial and renamed once its manifest is on disk.
        std::vector<std::wstring> names;
        for (const Gen& g : gens) names.push_back(g.name);
        const std::wstring name = hsv::unique_generation_name(stamp, names);
        if (name.empty()) return fail("no free generation name");
        const std::wstring partial = root + L"\\" + name + std::wstring(hsv::kPartialSuffix);
        const std::wstring final_dir = root + L"\\" + name;
        if (!CreateDirectoryW(partial.c_str(), nullptr)) {
            return fail(std::format("cannot create '{}' (error {})", narrow(partial), GetLastError()));
        }
        hsv::Manifest m;
        m.mod_build = mod_build_text();
        m.generation = narrow(name);
        m.created = local_time_text();
        {
            SYSTEMTIME u;
            GetSystemTime(&u);
            m.created_utc = std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", u.wYear, u.wMonth, u.wDay, u.wHour,
                                        u.wMinute, u.wSecond);
        }
        std::vector<hsv::GenerationEntry> entries;
        for (const Gen& g : gens) entries.push_back({g.name, g.valid, g.manifest.seq, refs_of(g.manifest)});
        m.seq = hsv::next_sequence(entries);
        {
            std::lock_guard lk(g_mu);
            m.run = g_run;
            m.run_start = g_run_start;
        }
        m.mode = hsv::mode_name(mode);
        uint64_t files = 0, bytes = 0;
        bool ok = true;
        for (const Source& s : sources) {
            hsv::SourceRecord rec{s.label, narrow(s.path), s.journal ? s.journal->journal.header.run : "unknown",
                                  s.tree_id, s.identical_to};
            m.sources.push_back(rec);
            if (!s.identical_to.empty()) continue;
            if (s.skipped) {
                m.notes.push_back(std::format("{}: {} steam_autocloud.vdf not copied (Steam Cloud's marker)", s.label,
                                              s.skipped));
            }
            const std::wstring dst = partial + L"\\" + widen(s.label);
            if (!copy_source(s, dst, m, why) || !copy_journal(s, partial, m, why)) {
                ok = false;
                break;
            }
            files += s.tree.size();
            bytes += s.bytes;
            describe_slots(s, dst, save_dir, mode == hsv::Mode::Probe, m.generation, m);
        }
        if (ok) {
            if (mode == hsv::Mode::Probe) {
                m.notes.push_back("probe verdicts are observations, not authorisation: nothing is published to the "
                                  "real folder (audit S34 section 5)");
            }
            m.complete = true;
            const std::string text = hsv::manifest_text(m);
            const std::wstring mpath = partial + L"\\" + widen(std::string(hsv::kManifestFile));
            std::string back;
            if (g_inject.load() == 1) {
                why = "manifest write failed (injected by a test)";
                ok = false;
            } else {
                ok = write_new_text(mpath, text, why);
            }
            if (ok && (!read_text(mpath, back, kMaxManifestBytes) || back != text || !hsv::parse_manifest(back))) {
                why = "the manifest does not read back";
                ok = false;
            }
        }
        if (ok && g_inject.load() == 2) {
            why = "rename failed (injected by a test)";
            ok = false;
        } else if (ok && !MoveFileExW(partial.c_str(), final_dir.c_str(), MOVEFILE_WRITE_THROUGH)) {
            why = std::format("cannot rename '{}' to '{}' (error {})", narrow(partial), narrow(final_dir),
                              GetLastError());
            ok = false;
        }
        if (!ok) {
            if (!delete_own_tree(partial, root, save_dir)) {
                log::warn("Session2 host save: the incomplete '{}' could not be deleted (delete it by hand)",
                          narrow(partial));
            }
            return fail(why);
        }
        r.outcome = hsv::Archive::Done;
        r.generation = name;
        for (const Source& s : sources) {
            if (s.aside) r.archived_aside.push_back(s.path);
        }
        // Retention: never the newest valid generation, never the one just made nor one a source matched.
        entries.push_back({name, true, m.seq, refs_of(m)});
        protect.push_back(name);
        const std::vector<std::wstring> drop = hsv::plan_prune(entries, g_keep, protect);
        std::string pruned;
        for (const std::wstring& d : drop) {
            const bool gone = delete_own_tree(root + L"\\" + d, root, save_dir);
            pruned += std::format("{}{}{}", pruned.empty() ? "" : ", ", narrow(d), gone ? "" : " (NOT deleted)");
        }
        prune_journals(runs, root, save_dir);
        size_t valid = 1;
        for (const Gen& g : gens) valid += g.valid ? 1 : 0;
        log::warn("Session2 host save EXPORT: generation '{}' made in '{}' and verified ({} folder(s) copied, {} files, "
                  "{} bytes, SHA-256 read back from the copies; manifest {}); valid generations {} (keep {}), "
                  "pruned: {}{}",
                  narrow(name), narrow(root),
                  std::count_if(sources.begin(), sources.end(), [](const Source& s) { return s.identical_to.empty(); }),
                  files, bytes, hsv::kManifestFile, valid - drop.size(), g_keep, pruned.empty() ? "none" : pruned,
                  partials ? std::format("; {} incomplete export(s) (.partial) kept, delete them by hand", partials)
                           : std::string());
        std::lock_guard lk(g_mu);
        g_last = "generation " + narrow(name) + " exported at start";
        return r;
    } catch (...) {
        log::error("Session2 host save: the export FAILED (an exception): nothing is deleted");
        r.outcome = hsv::Archive::Failed;
        r.generation.clear();
        r.archived_aside.clear();
        return r;
    }
}

// ---------------------------------------------------------------- this run's journal

void host_save_run_started(const std::wstring& sandbox) noexcept {
    if (!hsv::archives(g_mode.load())) return;
    if (!g_dir_ok.load()) {
        log::warn("Session2 host save: no journal for this run (the export folder was refused or not checked): probe "
                  "will say unknown for what this run saves");
        return;
    }
    try {
        const std::wstring runs = g_dir + L"\\runs";
        const std::string id = dir_id(sandbox);
        std::string run, start;
        {
            std::lock_guard lk(g_mu);
            run = g_run;
            start = g_run_start;
        }
        if (!make_dirs(runs)) {
            log::warn("Session2 host save: cannot create '{}' (error {}): no journal for this run (probe will say "
                      "unknown)", narrow(runs), GetLastError());
            return;
        }
        HANDLE h = INVALID_HANDLE_VALUE;
        std::wstring path;
        for (int i = 1; i < 100 && h == INVALID_HANDLE_VALUE; ++i) {
            const std::string r = i == 1 ? run : std::format("{}-{}", run, i);
            path = runs + L"\\" + widen(r) + L".txt";
            h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
            if (h != INVALID_HANDLE_VALUE) run = r;
            else if (GetLastError() != ERROR_FILE_EXISTS) break;
        }
        if (h == INVALID_HANDLE_VALUE) {
            log::warn("Session2 host save: cannot create the journal '{}' (error {}): probe will say unknown",
                      narrow(path), GetLastError());
            return;
        }
        const std::string head = hsv::journal_header_text({run, start, id, mod_build_text(), hsv::mode_name(g_mode.load())});
        DWORD w = 0;
        WriteFile(h, head.data(), static_cast<DWORD>(head.size()), &w, nullptr);
        {
            std::lock_guard lk(g_mu);
            g_journal = h;
            g_run = run;  // with its -N when the name was taken
        }
        log::info("Session2 host save: this run is '{}', journal '{}' (sandbox id {})", run, narrow(path),
                  id.empty() ? "unread" : id);
    } catch (...) {
    }
}

namespace {
void append(const std::string& line, bool event) {
    std::lock_guard lk(g_mu);
    if (g_journal == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    if (event) {
        if (g_events >= hsv::kMaxJournalEvents) {
            if (!g_cut) {
                const std::string t = std::string(hsv::kJournalTruncated) + "\n";
                WriteFile(g_journal, t.data(), static_cast<DWORD>(t.size()), &w, nullptr);
                g_cut = true;
            }
            return;
        }
        ++g_events;
    }
    WriteFile(g_journal, line.data(), static_cast<DWORD>(line.size()), &w, nullptr);
}
}  // namespace

void host_save_note_hooks(bool save, bool flush) noexcept {
    if (!hsv::archives(g_mode.load())) return;
    try {
        append(hsv::journal_hooks_line(save, flush), false);
    } catch (...) {
    }
}

void host_save_observe_save(const hsv::SaveEvent& event) noexcept {
    if (!hsv::archives(g_mode.load())) return;
    try {
        auto ev = event;
        {
            std::lock_guard lk(g_mu);
            ev.seq = ++g_seq;
        }
        ev.time = local_time_text();
        append(hsv::journal_save_line(ev), true);
    } catch (...) {
    }
}

void host_save_observe_flush(const hsv::FlushEvent& event) noexcept {
    if (!hsv::archives(g_mode.load())) return;
    try {
        auto ev = event;
        {
            std::lock_guard lk(g_mu);
            ev.seq = ++g_seq;
        }
        ev.time = local_time_text();
        append(hsv::journal_flush_line(ev), true);
    } catch (...) {
    }
}

void host_save_test_inject(int step) noexcept { g_inject.store(step); }

std::string host_save_describe() {
    const hsv::Mode mode = g_mode.load();
    if (!hsv::archives(mode)) return {};
    std::lock_guard lk(g_mu);
    return std::format("host save {}: {}, journal {} events{}", hsv::mode_name(mode), g_last, g_events,
                       g_journal == INVALID_HANDLE_VALUE ? " (NO journal)" : g_cut ? " (cap reached)" : "");
}

}  // namespace cdmp::session2
