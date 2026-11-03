#pragma once
// Option D, piece P6 (docs/OPCION-D-EMULADOR.md §4.6): while the host's
// server has a second user, nothing may write to the host's saves. A second
// ServerUserActor could end up serialised into the host's save slot.
//
// This is the policy only (pure logic, unit-tested): which file operations
// touch the save folder in a way that can change it. The hooks that ask it
// live in src/mod/session2/. Reads stay allowed: the server loads the host's
// slot for user 2 in the first experiments (E3).
//
// Session 24 (save_guard=trace): refusing makes the game's autosave fail and
// retry every 10 s ("Failed to save data"). Before the save folder can be
// redirected to a sandbox, the trace logs the game's real sequence of file
// operations under the folder without refusing any.
//
// Session 24 (save_guard=redirect): the whole folder is copied to a sandbox
// at start and every path under it is rewritten to the sandbox (Redirector):
// the game saves as usual and the real folder is never written.

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cdmp::saveguard {

// [session2] save_guard: 0 = off (nothing hooked), 1 = block (P6, the
// default), trace = nothing refused, every operation under the folder logged
// (lab only: the saves are NOT protected), redirect = every operation under
// the folder goes to a sandbox copy of it (the real folder is not written).
enum class Mode : uint8_t { Off, Block, Trace, Redirect };
const char* mode_name(Mode mode);  // "off", "block", "trace", "redirect"
// "trace" or "redirect" in any case, or a number read as
// GetPrivateProfileInt reads it (leading digits, so "1 ; note" is 1): 0 =
// Off, any other = Block, as the switch was read before session 24. Spaces
// around are ignored. Anything else = nullopt (the caller keeps the safe
// default, Block).
std::optional<Mode> parse_mode(std::string_view text);

// Win32 path as given to the file API -> comparable form: '/' -> '\',
// "\\?\" and "\\.\" prefixes dropped, repeated separators collapsed, no
// trailing separator, lower case (ASCII and Latin-1 letters; the save path
// is under %LOCALAPPDATA%, which may hold the user's name). Relative paths
// and ".." are NOT resolved here: the caller resolves them first
// (GetFullPathNameW).
std::wstring normalize(std::wstring_view path);

// True if `path` is `dir` itself or inside it (both normalized here).
bool is_under(std::wstring_view path, std::wstring_view dir);

// save_guard=redirect: `full_path` (absolute, as GetFullPathNameW returns it;
// a "\\?\" or "\\.\" prefix is allowed) moved from the save folder to the
// sandbox. When `full_path` is `save_dir` itself or inside it (compared as
// is_under() compares: any case, '/' or '\', repeated separators, on a
// component boundary, so "savegame" is not "save"), the result is
//   [the input's "\\?\" or "\\.\" prefix] + `sandbox_dir` (its own spelling,
//   '/' as '\', without a prefix or trailing separators of its own) + the
//   rest of `full_path` after the folder exactly as written (case,
//   separators, a trailing separator, wildcards).
// So `save_dir` itself is the sandbox root. Anything else is nullopt, and the
// caller passes its path through untouched: another folder, the sandbox
// itself, an empty `save_dir` or `sandbox_dir`, or a sandbox inside the save
// folder or containing it (its paths could not be told from the real ones).
std::optional<std::wstring> redirect_path(std::wstring_view full_path, std::wstring_view save_dir,
                                          std::wstring_view sandbox_dir);

// redirect_path() with both folders prepared once, for the hooks: under(),
// and map() of a path that is not under the save folder, allocate nothing.
class Redirector {
public:
    Redirector(std::wstring_view save_dir, std::wstring_view sandbox_dir);

    // False for the folder pairs redirect_path() refuses: nothing maps.
    bool valid() const { return valid_; }
    // True if map(full_path) has a value.
    bool under(std::wstring_view full_path) const { return match(full_path) != kNoMatch; }
    std::optional<std::wstring> map(std::wstring_view full_path) const;

    const std::wstring& save_dir() const { return dir_; }  // normalized
    const std::wstring& sandbox() const { return sandbox_; }

private:
    static constexpr size_t kNoMatch = static_cast<size_t>(-1);
    // Index in `full_path` right after the save folder, or kNoMatch.
    size_t match(std::wstring_view full_path) const;

    std::wstring dir_;      // normalize(save_dir)
    std::wstring sandbox_;  // as given: '/' as '\', no prefix, no trailing separator
    bool valid_ = false;
};

// CreateFile* arguments that may change a file: write/delete/attribute/ACL
// access, MAXIMUM_ALLOWED or GENERIC_ALL, a disposition that creates or
// truncates, or FILE_FLAG_DELETE_ON_CLOSE. OPEN_ALWAYS on a file that
// `exists` creates nothing, so with read access it is a read.
bool create_may_write(uint32_t desired_access, uint32_t creation_disposition, uint32_t flags_and_attributes,
                      bool exists = false);

enum class Op : uint8_t { Create, Delete, MoveFrom, MoveTo, Copy, Attributes, RemoveDir, CreateDir };
const char* op_name(Op op);

// Trace text for the file APIs' bit fields: "0x<hex>" and, when not 0, the
// known bits by their Win32 names and any other bits as "+0x<hex>", e.g.
// "0xC0000000 (GENERIC_READ|GENERIC_WRITE)".
std::string access_text(uint32_t desired_access);
std::string share_text(uint32_t share_mode);            // FILE_SHARE_*
std::string file_flags_text(uint32_t flags_and_attrs);  // FILE_FLAG_* and FILE_ATTRIBUTE_*
std::string move_flags_text(uint32_t flags);            // MOVEFILE_*
// "2 (CREATE_ALWAYS)"; an unknown value is the number only.
std::string disposition_text(uint32_t creation_disposition);

// The trace's line budget: at most `total` lines, of which at most
// `non_writes` for reads and queries, so a burst of those (the save list at
// load, existence checks) cannot use up the lines of the save itself.
// Thread-safe.
class TraceBudget {
public:
    TraceBudget(uint32_t total, uint32_t non_writes) : total_(total), non_write_cap_(non_writes) {}

    struct Line {
        uint64_t seq = 0;             // 1-based line number; 0 = over a cap, do not log
        bool last = false;            // the trace's last line: say that it stops
        bool last_non_write = false;  // the last read/query line: say so
    };
    Line take(bool write);

    uint64_t logged() const;
    uint64_t skipped() const { return skipped_.load(std::memory_order_relaxed); }
    uint32_t total() const { return total_; }
    uint32_t non_write_cap() const { return non_write_cap_; }

private:
    uint32_t total_, non_write_cap_;
    std::atomic<uint64_t> lines_{0}, non_writes_{0}, skipped_{0};
};

class Guard {
public:
    // `save_dir`: %LOCALAPPDATA%\Pearl Abyss\CD\save (any spelling).
    explicit Guard(std::wstring_view save_dir) : dir_(normalize(save_dir)) {}

    void set_active(bool on) { active_.store(on, std::memory_order_relaxed); }
    bool active() const { return active_.load(std::memory_order_relaxed); }

    // True = refuse the operation (the hook fails it with ACCESS_DENIED).
    bool block_create(std::wstring_view full_path, uint32_t access, uint32_t disposition, uint32_t flags,
                      bool exists = false);
    // Delete, move (either end), copy destination, attributes, directories.
    bool block_path(Op op, std::wstring_view full_path);

    uint64_t blocked() const { return blocked_.load(std::memory_order_relaxed); }
    uint64_t allowed_reads() const { return reads_.load(std::memory_order_relaxed); }
    const std::wstring& dir() const { return dir_; }

private:
    std::wstring dir_;
    std::atomic<bool> active_{false};
    std::atomic<uint64_t> blocked_{0}, reads_{0};
};

}  // namespace cdmp::saveguard
