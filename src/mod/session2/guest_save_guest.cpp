#include "mod/session2/guest_save_guest.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "common/troc_frame.h"
#include "mod/log.h"
#include "mod/render/hud.h"
#include "mod/role.h"
#include "mod/session2/guest_save_tunnel.h"
#include "mod/session2/guest_save_wire.h"

namespace cdmp::session2 {
namespace {

using gsw::AckResult;
using gsw::GuestHold;
using gsw::Kind;

constexpr size_t kKeepIncoming = 10;        // generations kept in incoming\ (the newest)
constexpr uint64_t kAccountWaitMs = 60000;  // several account folders: wait this long for the own SteamID64

std::atomic<uint8_t> g_mode{0};  // gsv::Mode
std::wstring g_save_root;        // %LOCALAPPDATA%\Pearl Abyss\CD\save
std::wstring g_progress_root;    // %LOCALAPPDATA%\cdmp\guest_progress

struct Prepared {
    bool ok = false;
    uint32_t slot = 0;
    std::wstring account;
    std::vector<uint8_t> lobby, save;
    gsw::Files files{};
    std::string why;
};

// The worker thread's jobs.
struct Job {
    enum class Kind : uint8_t { Prepare, Reprepare, Store } kind = Kind::Prepare;
    uint32_t slot = 0;
    gsw::Result result{};
    std::vector<uint8_t> lobby, save;
    std::string host_folder;
    uint64_t host_id = 0;
};

std::mutex g_mu;  // everything below (the three threads)
GuestHold g_hold;
Prepared g_prep;    // the start offer (and its retries)
Prepared g_reprep;  // the slot a 3093 asked for
bool g_prep_done = false;
std::map<std::string, uint32_t> g_applied;  // host folder -> the generation aplicar-progreso-invitado.ps1 applied
bool g_tunnel_up = false, g_uploaded = false, g_want_upload = false, g_may_upload = true;
uint32_t g_link_epoch = 0;  // the tunnel's session epoch (ClientSession::epoch)
std::deque<std::vector<uint8_t>> g_out;   // control frames for the host
// Review of phase B, item 1: the upload's chunks go out paced (guest_save_guest_take_outgoing's budget), not in one
// burst; while any is left the client's packets stay behind them (guest.cpp guest_take_outgoing).
std::optional<gsw::Outgoing> g_up_out;
std::deque<std::vector<uint8_t>> g_held;  // the client's packets held, in order
// Review item 4: at most one result waits for the worker (a second one is answered "busy": the host sends it again).
bool g_store_pending = false;
uint64_t c_refused_push = 0;
bool g_held_goodbye = false;
gsw::Status g_status{};
bool g_status_seen = false;
uint32_t g_next_xfer = 0;
uint32_t g_offer_xfer = 0;  // the current offer's (for a cancel)
std::deque<Job> g_jobs;
HANDLE g_wake = nullptr;
std::atomic<uint32_t> g_stored{0};  // generations stored in this run (the final hold waits for one more)
std::atomic<uint32_t> g_last_stored_gen{0};

// The mod's network thread only.
gsw::Assembler g_down;
gsw::Result g_down_meta{};

std::atomic<uint64_t> c_offers{0}, c_acks{0}, c_results{0}, c_chunks{0}, c_stored{0}, c_store_failed{0}, c_bad{0},
    c_held{0}, c_released{0}, c_dropped{0}, c_3016{0}, c_finals{0}, c_cancels{0};

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
uint64_t now64() { return GetTickCount64(); }
gsv::Mode mode() { return static_cast<gsv::Mode>(g_mode.load(std::memory_order_relaxed)); }

std::string utc_now() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                       t.wSecond);
}

// ---------------------------------------------------------------- files (the guest's own; never under save\)

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool read_all(const std::wstring& path, std::vector<uint8_t>& out, uint64_t max, std::string& why) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = std::format("cannot open '{}' (error {})", narrow(path), GetLastError());
        return false;
    }
    LARGE_INTEGER li{};
    bool ok = GetFileSizeEx(h, &li) != FALSE && static_cast<uint64_t>(li.QuadPart) <= max;
    if (ok) {
        out.resize(static_cast<size_t>(li.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(h);
    if (!ok) {
        out.clear();
        why = std::format("cannot read '{}' whole (at most {} bytes)", narrow(path), max);
    }
    return ok;
}

bool write_all(const std::wstring& path, std::span<const uint8_t> data, std::string& why) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        why = std::format("cannot create '{}' (error {})", narrow(path), GetLastError());
        return false;
    }
    DWORD put = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &put, nullptr) && put == data.size());
    ok = FlushFileBuffers(h) && ok;
    ok = CloseHandle(h) && ok;
    if (!ok) why = std::format("cannot write '{}' (error {})", narrow(path), GetLastError());
    return ok;
}

bool make_dirs(const std::wstring& dir) {
    for (size_t at = dir.find(L'\\', 3); at != std::wstring::npos; at = dir.find(L'\\', at + 1)) {
        CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
    }
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

// A generation folder the module made: its known files, then the folder.
bool remove_own_folder(const std::wstring& dir) {
    if (!exists(dir)) return true;
    for (const wchar_t* f : {L"lobby.save", L"save.save", L"manifest.txt"}) {
        const std::wstring p = dir + L"\\" + f;
        if (exists(p)) {
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(p.c_str());
        }
    }
    return RemoveDirectoryW(dir.c_str()) != FALSE;
}

std::vector<std::wstring> list_dirs(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            continue;
        }
        const std::wstring n = fd.cFileName;
        if (n != L"." && n != L"..") out.push_back(n);
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    return out;
}

bool file_time(const std::wstring& path, uint64_t& t) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }
    t = (static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
    return true;
}

// The host's folder name under guest_progress\: its SteamID64 from its SAVE_STATUS, else the Steam peer, else
// "unknown-host" (UDP: cdmp_peer's own tests use --save-results instead).
std::string host_folder_locked() {
    uint64_t id = g_status_seen ? g_status.host_id : 0;
    if (!id) id = guest_save_peer_net_id();
    return id ? std::to_string(id) : std::string("unknown-host");
}

// ---------------------------------------------------------------- the offer (worker thread)

// `want`: the slot a 3093 asked for; nullopt = the one "Continue" would load.
Prepared prepare(std::optional<uint32_t> want, bool wait_for_account) {
    Prepared p;
    std::optional<std::wstring> account;
    const uint64_t until = now64() + (wait_for_account ? kAccountWaitMs : 0);
    for (;;) {
        account = gsw::pick_account(list_dirs(g_save_root), guest_save_own_net_id());
        if (account || now64() >= until) break;
        Sleep(250);
    }
    if (!account) {
        p.why = std::format("no account folder under '{}' (none, or several and the own SteamID64 is not known)",
                            narrow(g_save_root));
        return p;
    }
    p.account = *account;
    const std::wstring dir = g_save_root + L"\\" + *account;
    std::map<uint32_t, gsw::SlotSeen> seen;
    for (const std::wstring& name : list_dirs(dir)) {
        const auto d = gsw::parse_slot_dir(name);
        if (!d) continue;
        gsw::SlotSeen& s = seen[d->slot];
        s.slot = d->slot;
        if (d->kind == gsw::DirKind::New) s.has_new = true;
        if (d->kind == gsw::DirKind::Backup) s.has_backup = true;
        if (d->kind != gsw::DirKind::Plain) continue;
        uint64_t t = 0;
        s.has_lobby = exists(dir + L"\\" + name + L"\\lobby.save");
        s.has_save = file_time(dir + L"\\" + name + L"\\save.save", t);
        s.save_time = t;
    }
    std::vector<gsw::SlotSeen> list;
    for (const auto& [n, s] : seen) list.push_back(s);
    uint32_t slot = 0;
    if (want) {
        const auto it = seen.find(*want);
        if (!gsw::offer_slot_ok(*want) || it == seen.end() || !it->second.has_lobby || !it->second.has_save) {
            p.why = std::format("slot{} has no lobby.save + save.save (or is not 0-2 / 100-108)", *want);
            return p;
        }
        if (it->second.has_new || it->second.has_backup) {
            p.why = std::format("slot{} has a slot{}{} folder: a save was interrupted; load it once alone", *want, *want,
                                it->second.has_new ? "_new" : "_backup");
            return p;
        }
        slot = *want;
    } else {
        const gsw::Pick pick = gsw::pick_continue_slot(list);
        if (!pick.slot) {
            p.why = pick.why;
            return p;
        }
        slot = *pick.slot;
        p.why = pick.why;
    }
    const std::wstring slot_dir = dir + std::format(L"\\slot{}", slot);
    std::string why;
    if (!read_all(slot_dir + L"\\lobby.save", p.lobby, gsv::kMaxFileSize, why) ||
        !read_all(slot_dir + L"\\save.save", p.save, gsv::kMaxFileSize, why)) {
        p.why = why;
        return p;
    }
    if (gsw::check_pair(p.lobby, p.save, why) != AckResult::Ok) {
        p.why = std::format("slot{}: {}", slot, why);
        p.lobby.clear();
        p.save.clear();
        return p;
    }
    p.slot = slot;
    p.files = gsw::files_of(p.lobby, p.save);
    p.ok = true;
    return p;
}

// guest_progress\<host>\applied.txt (written by aplicar-progreso-invitado.ps1): generation=N.
void read_applied() {
    std::map<std::string, uint32_t> applied;
    for (const std::wstring& host : list_dirs(g_progress_root)) {
        std::vector<uint8_t> bytes;
        std::string why;
        if (!read_all(g_progress_root + L"\\" + host + L"\\applied.txt", bytes, 64 * 1024, why)) continue;
        const auto kv = gsw::parse_key_values(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        if (const auto g = gsw::kv_u64(kv, "generation"); g && *g <= 0xFFFFFFFFull) {
            applied[narrow(host)] = static_cast<uint32_t>(*g);
        }
    }
    std::lock_guard lock(g_mu);
    g_applied = std::move(applied);
}

// Under g_mu: the offer into the queue for the host and its chunks behind it, paced by take_outgoing; both ahead of
// the client's packets (a newer offer drops what is left of an older one: the host ignores the older transfer).
void queue_upload_locked(const Prepared& p, const char* what) {
    const uint32_t xfer = ++g_next_xfer;
    const std::string host = host_folder_locked();
    const auto applied = g_applied.find(host);
    gsw::Offer o;
    o.xfer = xfer;
    o.guest_id = guest_save_own_net_id();
    o.slot = p.slot;
    o.base_gen = applied == g_applied.end() ? 0 : applied->second;
    o.files = p.files;
    g_out.push_back(gsw::frame(gsw::encode_offer(o)));
    g_up_out.emplace(xfer, p.lobby, p.save);
    g_hold.offer_sent(xfer, p.slot);
    g_offer_xfer = xfer;
    c_offers.fetch_add(1);
    log::info("Session2 guest save: {} -> the host: SAVE_OFFER xfer {} | slot{} (account {}) | base gen {} (host folder "
              "'{}') | lobby {} B sha256 {}... | save {} B sha256 {}... | {} chunks, paced under {} KB of tunnel backlog, "
              "before any packet of the client",
              what, xfer, p.slot, narrow(p.account), o.base_gen, host, p.files[0].size,
              sha256::hex(p.files[0].sha).substr(0, 16), p.files[1].size, sha256::hex(p.files[1].sha).substr(0, 16),
              gsw::Outgoing::chunk_count(p.files[0].size, p.files[1].size), gsw::kDownloadBacklog / 1024);
}

// ---------------------------------------------------------------- results (worker thread)

void store_result(Job& j) {
    const gsw::Result& r = j.result;
    const std::wstring host_dir = g_progress_root + L"\\" + widen(gsv::guest_folder_name(j.host_folder));
    const std::wstring incoming = host_dir + L"\\incoming";
    const std::wstring final_dir = incoming + std::format(L"\\gen{}", r.gen);
    std::string why;
    AckResult result = AckResult::Ok;
    gsw::IncomingManifest m;
    m.gen = r.gen;
    m.host_id = j.host_id;
    m.slot = r.slot;
    m.reason = r.reason;
    m.time_unix = r.time_unix;
    m.received_utc = utc_now();
    m.files = r.files;
    m.root = r.root;
    m.base = r.base;
    const std::string text = gsw::incoming_manifest_text(m);
    bool already = false;
    if (exists(final_dir)) {
        // Review of phase B, item 2: the same generation only if its manifest is there AND both files on disk are the
        // announced ones (size and SHA-256), never on the manifest's word alone.
        std::vector<uint8_t> old_lobby, old_save;
        std::string w;
        already = exists(final_dir + L"\\manifest.txt") &&
                  read_all(final_dir + L"\\lobby.save", old_lobby, gsv::kMaxFileSize, w) &&
                  read_all(final_dir + L"\\save.save", old_save, gsv::kMaxFileSize, w) &&
                  gsw::files_of(old_lobby, old_save) == r.files;
        if (!already) {
            // Item 3: another (or a broken) save with this number is replaced, never kept aside without bound. The
            // host numbers above the guest's base_gen and what it acknowledged, so this is a broken folder or an
            // unapplied generation of a host whose mailbox was emptied.
            log::warn("Session2 guest save: '{}' holds another or an incomplete save with this generation number: it is "
                      "replaced",
                      narrow(final_dir));
            if (!remove_own_folder(final_dir)) {
                why = std::format("cannot remove '{}' (error {}; files of its own?)", narrow(final_dir), GetLastError());
                result = AckResult::Io;
            }
        }
    }
    if (!already && result == AckResult::Ok) {
        const std::wstring partial = final_dir + L".partial";
        remove_own_folder(partial);
        if (!make_dirs(incoming) || !make_dirs(partial) || !write_all(partial + L"\\lobby.save", j.lobby, why) ||
            !write_all(partial + L"\\save.save", j.save, why) ||
            !write_all(partial + L"\\manifest.txt",
                       std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()), why) ||
            !MoveFileExW(partial.c_str(), final_dir.c_str(), 0)) {
            if (why.empty()) why = std::format("cannot rename to '{}' (error {})", narrow(final_dir), GetLastError());
            remove_own_folder(partial);
            result = AckResult::Io;
        }
    }
    if (result == AckResult::Ok) {
        // The newest kKeepIncoming generations stay.
        std::vector<uint32_t> gens;
        for (const std::wstring& n : list_dirs(incoming)) {
            if (const auto g = gsv::parse_generation(n)) gens.push_back(*g);
        }
        for (const uint32_t old : gsv::generations_to_drop(gens, kKeepIncoming)) {
            remove_own_folder(incoming + std::format(L"\\gen{}", old));
        }
    }
    {
        std::lock_guard lock(g_mu);
        g_store_pending = false;
        g_out.push_back(gsw::frame(gsw::encode_ack(gsw::kStateResultAck, gsw::Ack{r.xfer, static_cast<uint8_t>(result), r.gen})));
    }
    if (result == AckResult::Ok) {
        c_stored.fetch_add(1);
        g_last_stored_gen.store(r.gen);
        g_stored.fetch_add(1);
        log::info("Session2 guest save: gen{} of the host STORED{} in '{}' ({}; for slot{}, line {}; lobby {} B, save {} "
                  "B, SHA-256 checked) | apply it with the game CLOSED: tools\\scripts\\aplicar-progreso-invitado.ps1 "
                  "(it applies a generation only to a slot that holds a save of its line)",
                  r.gen, already ? " (already there)" : "", narrow(final_dir), gsw::reason_name(r.reason), r.slot,
                  gsw::digest_zero(r.root) ? std::string("NONE: never applied") : sha256::hex(r.root).substr(0, 16) + "...",
                  r.files[0].size, r.files[1].size);
    } else {
        c_store_failed.fetch_add(1);
        log::error("Session2 guest save: gen{} of the host could NOT be stored: {}; SAVE_RESULT_ACK {} (the host sends "
                   "it again)",
                   r.gen, why, gsw::ack_result_name(static_cast<uint8_t>(result)));
    }
}

DWORD WINAPI worker(LPVOID) {
    {
        // The start offer: the slot "Continue" would load, picked, read and checked now (before the tunnel exists).
        read_applied();
        Prepared p = prepare(std::nullopt, /*wait_for_account=*/true);
        std::lock_guard lock(g_mu);
        g_prep_done = true;
        if (p.ok) {
            log::info("Session2 guest save: the offer is ready: {} | lobby {} B, save {} B | it goes to the host at its "
                      "first control packet on a tunnel connection",
                      p.why, p.files[0].size, p.files[1].size);
            g_prep = std::move(p);
            if (g_tunnel_up && g_want_upload && g_may_upload && !g_uploaded) {
                queue_upload_locked(g_prep, "the start offer (ready after the host's first packet)");
                g_uploaded = true;
            }
        } else {
            g_hold.prepare_failed();
            log::warn("Session2 guest save: NOTHING is offered to the host: {} | this session does NOT keep your "
                      "progress (user 2 plays without your save)",
                      p.why);
            g_prep = std::move(p);
        }
    }
    for (;;) {
        WaitForSingleObject(g_wake, 1000);
        for (;;) {
            Job j;
            {
                std::lock_guard lock(g_mu);
                if (g_jobs.empty()) break;
                j = std::move(g_jobs.front());
                g_jobs.pop_front();
            }
            try {
                if (j.kind == Job::Kind::Store) {
                    store_result(j);
                } else if (j.kind == Job::Kind::Reprepare) {
                    Prepared p = prepare(j.slot, /*wait_for_account=*/false);
                    std::lock_guard lock(g_mu);
                    if (p.ok) {
                        g_reprep = std::move(p);
                        queue_upload_locked(g_reprep, "the slot the client's 3093 asked for");
                    } else {
                        g_hold.reoffer_failed();
                        log::warn("Session2 guest save: the client's 3093 asked for slot{}, which cannot be offered ({}); "
                                  "the host keeps slot{} (your progress goes back to slot{})",
                                  j.slot, p.why, g_hold.slot(), g_hold.slot());
                    }
                }
            } catch (...) {
                // Out of memory: the store is answered as an I/O failure (the host sends it again), a re-offer as
                // not possible (the offered slot stays).
                c_store_failed.fetch_add(1);
                try {
                    std::lock_guard lock(g_mu);
                    if (j.kind == Job::Kind::Store) {
                        g_store_pending = false;
                        g_out.push_back(gsw::frame(gsw::encode_ack(
                            gsw::kStateResultAck,
                            gsw::Ack{j.result.xfer, static_cast<uint8_t>(AckResult::Io), j.result.gen})));
                    } else if (j.kind == Job::Kind::Reprepare) {
                        g_hold.reoffer_failed();
                    }
                } catch (...) {
                }
            }
        }
    }
}

void post_job(Job&& j) {
    g_jobs.push_back(std::move(j));  // under g_mu (callers)
    if (g_wake) SetEvent(g_wake);
}

// ---------------------------------------------------------------- the host's frames (network thread)

void on_ack(const gsw::Ack& a) {
    c_acks.fetch_add(1);
    std::lock_guard lock(g_mu);
    bool retry = false;
    if (!g_hold.on_ack(a, retry)) {
        log::info("Session2 guest save: SAVE_ACK xfer {} ({}) is not for the current offer (xfer {}): ignored", a.xfer,
                  gsw::ack_result_name(a.result), g_hold.xfer());
        return;
    }
    if (a.result == static_cast<uint8_t>(AckResult::Ok)) {
        log::info("Session2 guest save: the host ACCEPTED the upload xfer {} (slot{}){}", a.xfer, g_hold.slot(),
                  a.gen ? std::format(": it continues THIS save from its mailbox gen{} (the same save, with progress you "
                                      "have not applied yet: it never goes back)",
                                      a.gen)
                        : std::string(": user 2 plays YOUR save if the host is in mode 1"));
        return;
    }
    log::warn("Session2 guest save: the host REFUSED the upload xfer {}: {}{}", a.xfer, gsw::ack_result_name(a.result),
              retry ? "; offered once more" : "; this session does NOT keep your progress");
    if (retry) {
        const Prepared& p = g_reprep.ok && g_reprep.slot == g_hold.slot() ? g_reprep : g_prep;
        if (p.ok) queue_upload_locked(p, "the same slot again (retry)");
    }
}

void result_ack(uint32_t xfer, AckResult r, uint32_t gen) {
    std::lock_guard lock(g_mu);
    g_out.push_back(gsw::frame(gsw::encode_ack(gsw::kStateResultAck, gsw::Ack{xfer, static_cast<uint8_t>(r), gen})));
}

void on_result(const gsw::Result& r) {
    c_results.fetch_add(1);
    if (g_down.active()) {
        log::info("Session2 guest save: SAVE_RESULT gen{} replaces the transfer xfer {} in progress ({} of {} B)", r.gen,
                  g_down.xfer(), g_down.received(), g_down.total());
    }
    if (const AckResult b = g_down.begin(r.xfer, r.files); b != AckResult::Ok) {
        result_ack(r.xfer, b, r.gen);
        return;
    }
    g_down_meta = r;
    bool offered_root = false;
    {
        std::lock_guard lock(g_mu);
        const Prepared& p = g_reprep.ok && g_reprep.slot == g_hold.slot() ? g_reprep : g_prep;
        offered_root = p.ok && p.files[1].sha == r.root;
    }
    log::info("Session2 guest save: SAVE_RESULT gen{} from the host (xfer {}, {}, for slot{}; lobby {} B, save {} B) | "
              "line {}",
              r.gen, r.xfer, gsw::reason_name(r.reason), r.slot, r.files[0].size, r.files[1].size,
              gsw::digest_zero(r.root) ? std::string("NONE (an older host build: the apply script refuses it)")
              : offered_root
                  ? sha256::hex(r.root).substr(0, 16) + "... (the save you offered in this session)"
                  : sha256::hex(r.root).substr(0, 16) +
                        "... (not the file you offered: a continuation of an earlier generation, or one the host had "
                        "not delivered yet)");
}

void on_chunk(const gsw::Chunk& c) {
    c_chunks.fetch_add(1);
    AckResult err = AckResult::Ok;
    switch (g_down.add(c, err)) {
        case gsw::Assembler::Step::Ignored:
        case gsw::Assembler::Step::Need: return;
        case gsw::Assembler::Step::Error:
            log::error("Session2 guest save: the host's transfer xfer {} broke (chunk out of order or past its size)",
                       c.xfer);
            result_ack(c.xfer, err, g_down_meta.gen);
            return;
        case gsw::Assembler::Step::Complete: break;
    }
    std::string why;
    const gsw::Result meta = g_down_meta;
    if (const AckResult v = g_down.verify(why); v != AckResult::Ok) {
        log::error("Session2 guest save: gen{} of the host FAILED its check: {}; SAVE_RESULT_ACK {}", meta.gen, why,
                   gsw::ack_result_name(static_cast<uint8_t>(v)));
        result_ack(meta.xfer, v, meta.gen);
        g_down.reset();
        return;
    }
    // Review item 4: one result at most waits for the worker (each holds up to 2 x 16 MB): a second one is answered
    // "busy" and the host sends it again (its retries); the bytes are moved, never copied.
    {
        std::lock_guard lock(g_mu);
        if (g_store_pending) {
            g_out.push_back(gsw::frame(gsw::encode_ack(
                gsw::kStateResultAck, gsw::Ack{meta.xfer, static_cast<uint8_t>(AckResult::Busy), meta.gen})));
            log::warn("Session2 guest save: gen{} arrived while the previous one is still being written: SAVE_RESULT_ACK "
                      "busy (the host sends it again)",
                      meta.gen);
            g_down.reset();
            return;
        }
        g_store_pending = true;
    }
    Job j;
    j.kind = Job::Kind::Store;
    j.result = meta;
    j.lobby = g_down.take(Kind::Lobby);
    j.save = g_down.take(Kind::Save);
    g_down.reset();
    std::lock_guard lock(g_mu);
    j.host_folder = host_folder_locked();
    j.host_id = g_status_seen && g_status.host_id ? g_status.host_id : guest_save_peer_net_id();
    post_job(std::move(j));
}

void on_status(const gsw::Status& s) {
    std::lock_guard lock(g_mu);
    const bool changed = !g_status_seen || s.mode != g_status.mode;
    g_status = s;
    g_status_seen = true;
    if (!changed) return;
    const bool keeps = s.mode == static_cast<uint8_t>(gsw::HostMode::Ready) ||
                       s.mode == static_cast<uint8_t>(gsw::HostMode::Active);
    if (keeps) {
        log::info("Session2 guest save: the host says: {} | its slot {} | host {} | last gen {}",
                  gsw::host_mode_name(s.mode), s.guest_slot, s.host_id, s.last_gen);
    } else {
        log::warn("Session2 guest save: the host says: {} | last game error 0x{:X} | host {}", gsw::host_mode_name(s.mode),
                  s.last_error, s.host_id);
    }
}

}  // namespace

void guest_save_guest_configure(const std::string& raw) {
    const gsv::ParsedMode p = gsv::parse_mode(raw);
    if (!p.known) {
        log::warn("Session2 guest save: guest_own_save='{}' is not 0, probe or 1; it stays off", raw);
        return;
    }
    if (p.mode == gsv::Mode::Off) return;
    wchar_t local[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        log::warn("Session2 guest save: %LOCALAPPDATA% not found; guest_own_save stays off");
        return;
    }
    // CDMP_ROLE=guest (role_logic.h): its saves are in save_cdmp_guest (the redirect sends it there anyway; named
    // here so the lines say where).
    g_save_root = role::current() == role::Role::Guest ? role::guest_save_dir(local) : role::save_dir(local);
    g_progress_root = std::wstring(local) + L"\\cdmp\\guest_progress";
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    {
        std::lock_guard lock(g_mu);
        g_next_xfer = static_cast<uint32_t>(GetTickCount64() & 0xFFFFFF) << 4;
        g_hold.set_enabled(p.mode == gsv::Mode::On);
        g_hold.prepare_started();
    }
    g_mode.store(static_cast<uint8_t>(p.mode));
    log::warn("Session2 guest save (P8 S25, phase B): guest_own_save={} on the GUEST | {} | the slot Continue would "
              "load under '{}\\<account>' is picked and checked now, uploaded at the host's first packet (caps 0x{:02X}) | "
              "results stored in '{}\\<host>\\incoming\\gen<n>' (never under save\\: apply them with the game closed, "
              "tools\\scripts\\aplicar-progreso-invitado.ps1)",
              gsv::mode_name(p.mode),
              p.mode == gsv::Mode::On
                  ? "the client's 3093 waits for the host's SAVE_ACK (30 s at most), its goodbye waits behind one "
                    "SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op') for the final save (15 s at most)"
                  : "PROBE: upload and results only; the client's packets are never held and no Req is added",
              narrow(g_save_root), gsw::kCapsGuestSave, narrow(g_progress_root));
    CloseHandle(CreateThread(nullptr, 0, worker, nullptr, 0, nullptr));
}

uint8_t guest_save_guest_caps() { return mode() != gsv::Mode::Off ? gsw::kCapsGuestSave : uint8_t{0}; }

void guest_save_guest_tunnel(bool up, uint32_t epoch) {
    if (mode() == gsv::Mode::Off) return;
    bool ended = false;
    try {
        std::lock_guard lock(g_mu);
        // A new connection (session epoch) is a new tunnel even if it never looked down: its upload goes again.
        ended = (!up && g_tunnel_up) || epoch != g_link_epoch;
        g_link_epoch = epoch;
        g_tunnel_up = up;
        if (ended) {
            g_uploaded = false;
            g_want_upload = false;
            g_out.clear();  // they belonged to the connection that ended
            g_up_out.reset();
        }
    } catch (...) {
    }
    if (ended) g_down.reset();
}

void guest_save_guest_conversation_begin(uint32_t conversation) {
    // Review of c4367f8, 6: the first remote conversation (and so rejoin=0) changes nothing here.
    if (mode() == gsv::Mode::Off || conversation < 2) return;
    size_t dropped = 0;
    std::string upload;
    bool final_was_done = false;
    try {
        std::lock_guard lock(g_mu);
        final_was_done = g_hold.final_done();
        g_hold.new_conversation();
        dropped = g_held.size();
        c_dropped.fetch_add(dropped);
        g_held.clear();
        g_held_goodbye = false;
        g_status_seen = false;  // the next SAVE_STATUS (the host repeats it within 5 s) decides the final save
        upload = gsw::upload_name(g_hold.upload());
    } catch (...) {
    }
    try {
        log::info("Session2 guest save: remote conversation {} (re-entry in the same game run): NO new upload (your PC's "
                  "slot does not have the previous conversation's progress until it is applied with the game closed; "
                  "the host plays its live guest slot, written by that conversation's final save) | upload state {} | "
                  "final save of the previous conversation {} | held packets dropped {}",
                  conversation, upload, final_was_done ? "sent" : "not sent", dropped);
    } catch (...) {
    }
}

void guest_save_guest_host_seen(bool may_upload) {
    if (mode() == gsv::Mode::Off) return;
    try {
        std::lock_guard lock(g_mu);
        g_may_upload = may_upload;
        if (!g_tunnel_up || g_uploaded || !may_upload) return;
        if (g_prep.ok) {
            queue_upload_locked(g_prep, "the start offer");
            g_uploaded = true;
        } else if (!g_prep_done) {
            g_want_upload = true;  // the worker queues it as soon as it is ready
        }
    } catch (...) {
    }
}

bool guest_save_guest_on_control(const std::vector<uint8_t>& packet) {
    const auto payload = gsw::save_payload(packet);
    if (!payload) return false;
    if (mode() == gsv::Mode::Off) return true;  // a host that sends them anyway: never the game's
    try {
        const std::span<const uint8_t> p = *payload;
        switch (p[0]) {
            case gsw::kStateAck:
                if (const auto a = gsw::decode_ack(p, gsw::kStateAck)) {
                    on_ack(*a);
                } else {
                    c_bad.fetch_add(1);
                }
                break;
            case gsw::kStateResult:
                if (const auto r = gsw::decode_result(p)) {
                    on_result(*r);
                } else if (const auto xfer = gsw::other_version_result_xfer(p)) {
                    // Review of 12f8dfb, item 5: another build on the host (another SAVE_RESULT version): said once and
                    // answered, never silently dropped (the host would send ~1.2 MB again and again).
                    c_bad.fetch_add(1);
                    static std::atomic<bool> said{false};
                    if (!said.exchange(true)) {
                        log::error("Session2 guest save: the host sends SAVE_RESULT version {} (this mod speaks {}): "
                                   "UPDATE THE MOD ON BOTH PCs; its saves are not stored",
                                   static_cast<unsigned>(p[1]), static_cast<unsigned>(gsw::kResultVersion));
                    }
                    result_ack(*xfer, AckResult::Protocol, 0);
                } else {
                    c_bad.fetch_add(1);
                }
                break;
            case gsw::kStateChunk:
                if (const auto c = gsw::decode_chunk(p)) {
                    on_chunk(*c);
                } else {
                    c_bad.fetch_add(1);
                }
                break;
            case gsw::kStateStatus:
                if (const auto s = gsw::decode_status(p)) {
                    on_status(*s);
                } else {
                    c_bad.fetch_add(1);
                }
                break;
            default: c_bad.fetch_add(1); break;  // 6 / 11 only go the other way
        }
    } catch (...) {
        g_down.reset();
    }
    return true;
}

void guest_save_guest_on_host_packet(uint16_t opcode, std::span<const uint8_t> payload) {
    if (opcode != gsw::kOpSaveToFileCurrentPlayerAck || mode() == gsv::Mode::Off) return;
    try {
        // u32 key | u32 slot | u8 ok | u8 flag | u32 result (writer 0x2A1FD40). A failed final save releases the
        // goodbye at once: no generation will come.
        const auto a = gsw::parse_save_ack(payload);
        {
            std::lock_guard lock(g_mu);
            if (a) g_hold.final_answer(a->ok && a->result == 0);
        }
        if (c_3016.fetch_add(1) < 16) {
            if (a) {
                log::info("Session2 guest save: SaveToFileCurrentPlayerAck 3016 from the host's server: character "
                          "0x{:08X} slot {} -> {} (result 0x{:X}){}",
                          a->key, a->slot, a->ok && a->result == 0 ? "SAVED" : "NOT saved", a->result,
                          a->ok && a->result == 0 ? "; its SAVE_RESULT follows when it went to your slot" : "");
            } else {
                log::info("Session2 guest save: SaveToFileCurrentPlayerAck 3016 of {} bytes (not the 14 expected)",
                          payload.size());
            }
        }
    } catch (...) {
    }
}

std::deque<std::vector<uint8_t>> guest_save_guest_take_outgoing(size_t link_backlog, bool& upload_pending) {
    std::deque<std::vector<uint8_t>> out;
    upload_pending = false;
    if (mode() == gsv::Mode::Off) return out;
    try {
        std::lock_guard lock(g_mu);
        out.swap(g_out);
        // Review item 1: the upload's chunks, only while the tunnel's own backlog stays under the download's bound;
        // the rest goes on the next loops (the client's packets wait behind them: upload_pending).
        size_t used = link_backlog;
        for (const auto& f : out) used += f.size();
        std::vector<uint8_t> payload;
        while (g_up_out && used < gsw::kDownloadBacklog) {
            if (!g_up_out->next(payload)) {
                g_up_out.reset();
                break;
            }
            out.push_back(gsw::frame(payload));
            used += out.back().size();
        }
        if (g_up_out && g_up_out->done()) g_up_out.reset();
        upload_pending = g_up_out.has_value();
    } catch (...) {
    }
    return out;
}

void guest_save_guest_push_refused(size_t refused) {
    if (!refused || mode() == gsv::Mode::Off) return;
    try {
        std::lock_guard lock(g_mu);
        if (c_refused_push++ < 8) {
            log::warn("Session2 guest save: the tunnel refused {} packet(s) (its 16 MB backlog, or a broken link): the "
                      "link resets and the upload goes again on the next connection",
                      refused);
        }
    } catch (...) {
    }
}

GuestSaveSend guest_save_guest_on_send(const std::vector<uint8_t>& packet, bool sent_3093) {
    GuestSaveSend r;
    if (mode() != gsv::Mode::On || packet.size() < troc::kInternalHeaderSize) return r;
    try {
        const uint16_t op = static_cast<uint16_t>(packet[0] | (packet[1] << 8));
        std::optional<uint32_t> slot;
        if (op == gsw::kOpLoginCharacterReq && packet.size() >= troc::kHeaderSize + 13) {
            slot = gsw::get32(packet.data() + troc::kHeaderSize + 9);
        }
        std::lock_guard lock(g_mu);
        const bool final_ok =
            sent_3093 && g_status_seen && g_status.mode == static_cast<uint8_t>(gsw::HostMode::Active);
        const GuestHold::OnSend d = g_hold.on_send(op, slot, now64(), final_ok, g_stored.load());
        if (d.reoffer) {
            Job j;
            j.kind = Job::Kind::Reprepare;
            j.slot = *d.reoffer;
            post_job(std::move(j));
            log::info("Session2 guest save: the client's 3093 asks for slot{}, not the offered slot{}: that slot is "
                      "offered first; the 3093 waits",
                      *d.reoffer, g_hold.slot());
        }
        if (d.slot_kept) {
            // P8 re-entry (review of c4367f8, 3): the host keeps playing its live guest slot; nothing is uploaded.
            log::warn("Session2 guest save: re-entry: the client's 3093 asks for slot{} (not the offered slot{}); NOT "
                      "offered (your PC's slot lacks the previous conversation's progress): the host plays its live "
                      "guest slot 204, written by that conversation's final save",
                      *d.slot_kept, g_hold.slot());
            render::hud_notice(render::Notice::ReentrySlotKept, 12000);
        }
        if (d.save_first) {
            // SaveToFileCurrentPlayerReq 2910 for manual slot 100 named "Co-op" (slot byte 0): it saves now on the
            // host (a 3037 does not: guest_save_wire.h), remapped to the guest's slot 204 there.
            r.before = gsw::make_final_save_req();
            c_finals.fetch_add(1);
            log::info("Session2 guest save: the client says goodbye (2001) while the host plays your save: a "
                      "SaveToFileCurrentPlayerReq 2910 (slot {}, 'Co-op') goes first and the goodbye waits for the final "
                      "save's SAVE_RESULT (15 s at most; at once if the server's 3016 says it failed)",
                      gsw::kFinalSaveSlot);
        }
        if (d.hold) {
            if (g_held.empty()) {
                log::info("Session2 guest save: the client's packets are HELD from opcode {} on (upload {})", op,
                          gsw::upload_name(g_hold.upload()));
            }
            if (op == gsw::kOpLogoutSessionReq) g_held_goodbye = true;
            g_held.push_back(packet);
            c_held.fetch_add(1);
            r.held = true;
        }
    } catch (...) {
    }
    return r;
}

GuestSaveRelease guest_save_guest_release(bool broken) {
    GuestSaveRelease r;
    if (mode() != gsv::Mode::On) return r;
    try {
        std::lock_guard lock(g_mu);
        if (g_held.empty() && g_hold.hold() == GuestHold::Hold::Off) return r;
        if (broken) {
            r.goodbye = g_held_goodbye;
            c_dropped.fetch_add(g_held.size());
            if (!g_held.empty()) {
                log::warn("Session2 guest save: the connection to the host broke: {} held packet(s) dropped", g_held.size());
            }
            g_held.clear();
            g_held_goodbye = false;
            g_hold.on_link_lost();
            return r;
        }
        const GuestHold::Release rel = g_hold.poll(now64(), g_stored.load());
        if (rel == GuestHold::Release::None) return r;
        r.packets.assign(std::make_move_iterator(g_held.begin()), std::make_move_iterator(g_held.end()));
        r.goodbye = g_held_goodbye;
        g_held.clear();
        g_held_goodbye = false;
        c_released.fetch_add(r.packets.size());
        const bool bad = rel == GuestHold::Release::GaveUp || rel == GuestHold::Release::Timeout ||
                         rel == GuestHold::Release::NoOffer || rel == GuestHold::Release::FinalTimeout ||
                         rel == GuestHold::Release::FinalFailed;
        if (bad) {
            log::warn("Session2 guest save: {} held packet(s) released: {}", r.packets.size(), gsw::release_name(rel));
        } else {
            log::info("Session2 guest save: {} held packet(s) released: {}", r.packets.size(), gsw::release_name(rel));
        }
        if (rel == GuestHold::Release::Timeout) {
            // Tell the host this end gave up (it logs it; mode 1 there still refuses anything but the guest's slot).
            g_out.push_back(gsw::frame(gsw::encode_ack(
                gsw::kStateAck, gsw::Ack{g_offer_xfer, static_cast<uint8_t>(AckResult::Cancelled), 0})));
            c_cancels.fetch_add(1);
        }
    } catch (...) {
    }
    return r;
}

std::string guest_save_guest_describe() {
    if (mode() == gsv::Mode::Off) return {};
    std::lock_guard lock(g_mu);
    return std::format(" | guest save {}: offer {} (slot{}, offers {}, acks {}) | host {} | results {} (stored {}, last "
                       "gen {}, failed {}), chunks {}, malformed {} | held {} now {} (released {}, dropped {}) | final "
                       "2910 {} | 3016 seen {} | gave up {}{}",
                       gsv::mode_name(mode()), gsw::upload_name(g_hold.upload()), g_hold.slot(), c_offers.load(),
                       c_acks.load(), g_status_seen ? gsw::host_mode_name(g_status.mode) : "no status yet",
                       c_results.load(), c_stored.load(), g_last_stored_gen.load(), c_store_failed.load(),
                       c_chunks.load(), c_bad.load(), c_held.load(), g_held.size(), c_released.load(), c_dropped.load(),
                       c_finals.load(), c_3016.load(), c_cancels.load(),
                       g_up_out ? std::format(" | upload sending ({} of {} B)", g_up_out->sent(), g_up_out->total())
                                : std::string());
}

}  // namespace cdmp::session2
