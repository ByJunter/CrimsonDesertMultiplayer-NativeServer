#pragma once
// P8 session 35, phase E (docs/research/s25/s35-fase-e-guardado.md): the HOST's saves made in the save sandbox
// (save_guard=redirect) are exported as immutable generations OUTSIDE the real save folder before the start-up
// rebuild of the sandbox can delete them, and `probe` classifies what was exported. The pure part of
// host_save_export.cpp (no game access, no files), so the tests can run it: the [session2] host_save modes (`real`
// refused: audit S34 §5 blocks publishing to the real folder), the slot whitelist, generation names, retention, the
// rebuild plan when an export fails, the manifest and the run journal formats, and the per-slot classification.
//
// Build 1.0.0.2976 (PE timestamp 0x6AB28F00, base 0x140000000). Nothing here writes the real save folder, and no
// verdict here claims who wrote a file: header + SHA-256 prove structure and equality, not authorship (audit S34
// §5, `check_header` and SHA are [F] as proof of authorship). "candidate" only means that every cheap check this
// code can make holds; the audit's M1/M2 conditions stay open.

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mod/session2/guest_save_logic.h"

namespace cdmp::session2::hsv {

// ---------------------------------------------------------------- the option

// [session2] host_save: sandbox (default: today's behaviour, the sandbox is rebuilt and the run before last is lost)
// | export (the previous sandbox, its _prev and any folder set aside are archived first) | probe (export, plus a
// verdict per slot of what was archived and the Save/Flush observations of the run in the manifest).
enum class Mode : uint8_t { Sandbox, Probe, Export };

struct ParsedMode {
    Mode mode = Mode::Sandbox;
    bool known = true;          // false: an unknown value, treated as sandbox (warned)
    bool refused_real = false;  // "real": NOT implemented (audit S34 §5), treated as sandbox (error line)
};

inline ParsedMode parse_mode(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) raw = raw.substr(0, c);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }
    std::string v(raw);
    for (char& ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (v.empty() || v == "sandbox" || v == "0" || v == "off") return {Mode::Sandbox, true, false};
    if (v == "export") return {Mode::Export, true, false};
    if (v == "probe") return {Mode::Probe, true, false};
    if (v == "real" || v == "1" || v == "on") return {Mode::Sandbox, true, true};
    return {Mode::Sandbox, false, false};
}

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Sandbox: return "sandbox";
        case Mode::Probe: return "probe";
        case Mode::Export: return "export";
    }
    return "?";
}

// The modes that archive the sandbox before the rebuild (and keep a run journal).
constexpr bool archives(Mode m) { return m != Mode::Sandbox; }

// [session2] host_save_keep: the valid generations kept (the newest one is never pruned).
inline constexpr uint32_t kDefaultKeep = 20;
inline constexpr uint32_t kMaxKeep = 500;
constexpr uint32_t clamp_keep(uint64_t v) { return v < 1 ? 1 : v > kMaxKeep ? kMaxKeep : static_cast<uint32_t>(v); }

// ---------------------------------------------------------------- slots

// The audit's whitelist (S34 §5, "Política propuesta"): 0-2 (the autosave rotation) and 100-108 (2910's), exact;
// 3 is CrossSave (0x186E450's branch 3 uploads it), 200-207 are the guest pool (204 = the guest's own slot,
// guest_save_logic.h; 206 is deleted by 0x186F570 after a good load). Anything else is outside the list.
enum class SlotClass : uint8_t { Host, CrossSave, GuestPool, Outside };

constexpr SlotClass slot_class(uint32_t n) {
    if (n <= 2 || (n >= 100 && n <= 108)) return SlotClass::Host;
    if (n == 3) return SlotClass::CrossSave;
    if (n >= 200 && n <= 207) return SlotClass::GuestPool;
    return SlotClass::Outside;
}
constexpr bool slot_whitelisted(uint32_t n) { return slot_class(n) == SlotClass::Host; }

inline const char* slot_class_name(SlotClass c) {
    switch (c) {
        case SlotClass::Host: return "host";
        case SlotClass::CrossSave: return "crosssave";
        case SlotClass::GuestPool: return "guest-pool";
        case SlotClass::Outside: return "outside";
    }
    return "?";
}

// A folder in an account folder: "slotN", or a leftover of the game's own commit (slotN_new, slotN_backup,
// slotN_backup_<mark>_delete; traza 11-10, p8-guardado-desviado.md §1) or anything else starting with "slotN_".
enum class FolderKind : uint8_t { Slot, New, Backup, BackupDelete, Other };
struct SlotFolder {
    FolderKind kind = FolderKind::Slot;
    uint32_t number = 0;
};

inline const char* folder_kind_name(FolderKind k) {
    switch (k) {
        case FolderKind::Slot: return "slot";
        case FolderKind::New: return "leftover _new";
        case FolderKind::Backup: return "leftover _backup";
        case FolderKind::BackupDelete: return "leftover _backup_*_delete";
        case FolderKind::Other: return "leftover slotN_*";
    }
    return "?";
}

// "slot0".."slot999" (no leading zero but "slot0"; "slot" in any case, as the file system compares it); nullopt
// when the name is not a slot folder at all.
inline std::optional<SlotFolder> parse_slot_folder(std::wstring_view name) {
    if (name.size() < 5) return std::nullopt;
    for (size_t i = 0; i < 4; ++i) {
        const wchar_t c = name[i] >= L'A' && name[i] <= L'Z' ? static_cast<wchar_t>(name[i] - L'A' + L'a') : name[i];
        if (c != L"slot"[i]) return std::nullopt;
    }
    size_t i = 4;
    uint32_t n = 0;
    while (i < name.size() && name[i] >= L'0' && name[i] <= L'9' && i < 7) {
        n = n * 10 + static_cast<uint32_t>(name[i++] - L'0');
    }
    const size_t digits = i - 4;
    if (digits == 0 || (digits > 1 && name[4] == L'0')) return std::nullopt;
    if (i < name.size() && name[i] >= L'0' && name[i] <= L'9') return std::nullopt;  // more than 3 digits
    if (i == name.size()) return SlotFolder{FolderKind::Slot, n};
    if (name[i] != L'_') return std::nullopt;  // "slot1x"
    std::wstring rest(name.substr(i));
    for (wchar_t& c : rest) c = c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    if (rest == L"_new") return SlotFolder{FolderKind::New, n};
    if (rest == L"_backup") return SlotFolder{FolderKind::Backup, n};
    if (rest.size() > 15 && rest.rfind(L"_backup_", 0) == 0 && rest.ends_with(L"_delete")) {
        return SlotFolder{FolderKind::BackupDelete, n};
    }
    return SlotFolder{FolderKind::Other, n};
}

// An account folder: all digits (the save root "%#/%#/save/%#" names it after the account, 0x5714110).
inline bool account_folder(std::wstring_view name) {
    if (name.empty() || name.size() > 20) return false;
    return std::all_of(name.begin(), name.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// ---------------------------------------------------------------- generation names

struct Stamp {
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
};

inline std::wstring stamp_text(const Stamp& s) {
    wchar_t b[32];
    swprintf(b, 32, L"%04u%02u%02u-%02u%02u%02u", s.year % 10000u, s.month % 100u, s.day % 100u, s.hour % 100u,
             s.minute % 100u, s.second % 100u);
    return b;
}

// "20261014-190232" (seq 1), "20261014-190232-2" (seq 2, the same second again), ...
inline std::wstring generation_name(const Stamp& s, uint32_t seq = 1) {
    std::wstring n = stamp_text(s);
    if (seq >= 2) n += L"-" + std::to_wstring(seq);
    return n;
}

inline constexpr std::wstring_view kPartialSuffix = L".partial";

struct GenerationName {
    std::wstring stamp;  // "20261014-190232"
    uint32_t seq = 1;
};

// Exactly what generation_name makes (seq 2..9999, no leading zero); nothing else (not ".partial", not "runs").
inline std::optional<GenerationName> parse_generation_name(std::wstring_view name) {
    if (name.size() < 15) return std::nullopt;
    for (size_t i = 0; i < 15; ++i) {
        const bool digit = name[i] >= L'0' && name[i] <= L'9';
        if (i == 8 ? name[i] != L'-' : !digit) return std::nullopt;
    }
    GenerationName g{std::wstring(name.substr(0, 15)), 1};
    if (name.size() == 15) return g;
    if (name[15] != L'-' || name.size() < 17 || name.size() > 20 || name[16] == L'0') return std::nullopt;
    uint32_t n = 0;
    for (size_t i = 16; i < name.size(); ++i) {
        if (name[i] < L'0' || name[i] > L'9') return std::nullopt;
        n = n * 10 + static_cast<uint32_t>(name[i] - L'0');
    }
    if (n < 2) return std::nullopt;
    g.seq = n;
    return g;
}

inline bool generation_before(const GenerationName& a, const GenerationName& b) {
    return a.stamp != b.stamp ? a.stamp < b.stamp : a.seq < b.seq;
}

// The name for a new generation at `s`: above every number `existing` already used with that stamp (folder names,
// ".partial" ones included), so a name freed by pruning is never reused and the new one sorts last within its
// second. Empty when the numbers ran out.
inline std::wstring unique_generation_name(const Stamp& s, std::span<const std::wstring> existing) {
    const std::wstring stamp = stamp_text(s);
    uint32_t top = 0;
    for (const std::wstring& e : existing) {
        std::wstring_view v = e;
        if (v.size() > kPartialSuffix.size() && v.ends_with(kPartialSuffix)) v.remove_suffix(kPartialSuffix.size());
        if (const auto g = parse_generation_name(v); g && g->stamp == stamp) top = std::max(top, g->seq);
    }
    if (top >= 9999) return {};
    return generation_name(s, top + 1);
}

// A sandbox that could not be exported is renamed, never deleted: "<sandbox>_unexported_<generation name>".
inline constexpr std::wstring_view kAsideInfix = L"_unexported_";

inline std::wstring aside_name(std::wstring_view sandbox_leaf, const Stamp& s, std::span<const std::wstring> existing) {
    for (uint32_t seq = 1; seq < 10000; ++seq) {
        const std::wstring n = std::wstring(sandbox_leaf) + std::wstring(kAsideInfix) + generation_name(s, seq);
        const bool taken = std::any_of(existing.begin(), existing.end(),
                                       [&](const std::wstring& e) { return _wcsicmp(e.c_str(), n.c_str()) == 0; });
        if (!taken) return n;
    }
    return {};
}

// "<sandbox_leaf>_unexported_<generation>" -> the generation part; nullopt otherwise.
inline std::optional<std::wstring> parse_aside_name(std::wstring_view sandbox_leaf, std::wstring_view name) {
    const std::wstring prefix = std::wstring(sandbox_leaf) + std::wstring(kAsideInfix);
    if (name.size() <= prefix.size() || _wcsnicmp(name.data(), prefix.c_str(), prefix.size()) != 0) {
        return std::nullopt;
    }
    const std::wstring_view rest = name.substr(prefix.size());
    if (!parse_generation_name(rest)) return std::nullopt;
    return std::wstring(rest);
}

// ---------------------------------------------------------------- retention

struct GenerationEntry {
    std::wstring name;
    bool valid = false;  // a generation name with a complete manifest whose files verify (anything else: never pruned)
    uint64_t seq = 0;    // the manifest's monotonic sequence (0 = none: ordered by name only, before any numbered one)
    std::vector<std::wstring> refs;  // generations its sources are identical_to (their files live only there)
};

// The order of "newest": the manifest's monotonic sequence, not the local-time name (a clock set back, or DST, would
// make a new generation sort before an old one); the name only breaks ties and orders those without a sequence.
inline bool generation_older(const GenerationEntry& a, const GenerationEntry& b) {
    if (a.seq != b.seq) return a.seq < b.seq;
    const auto pa = parse_generation_name(a.name), pb = parse_generation_name(b.name);
    if (pa && pb) return generation_before(*pa, *pb);
    return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
}

// The next sequence number: above every one already used (valid generations or not).
inline uint64_t next_sequence(std::span<const GenerationEntry> entries) {
    uint64_t top = 0;
    for (const GenerationEntry& e : entries) top = std::max(top, e.seq);
    return top + 1;
}

// The valid generations to delete (oldest first) so that at most `keep` (>= 1) valid ones remain, never the newest
// valid one, never one in `protect` (the one just made, and those a source of this start was found identical to), and
// never one a kept generation refers to through identical_to (its files are the only copy). Invalid, partial or
// unknown folders are never in the result, so more than `keep` may remain.
inline std::vector<std::wstring> plan_prune(std::span<const GenerationEntry> entries, uint32_t keep,
                                            std::span<const std::wstring> protect = {}) {
    keep = clamp_keep(keep);
    std::vector<GenerationEntry> valid;
    for (const GenerationEntry& e : entries) {
        if (!e.valid || !parse_generation_name(e.name)) continue;
        const bool dup = std::any_of(valid.begin(), valid.end(), [&](const GenerationEntry& v) {
            return _wcsicmp(v.name.c_str(), e.name.c_str()) == 0;
        });
        if (!dup) valid.push_back(e);
    }
    std::sort(valid.begin(), valid.end(), generation_older);
    const auto same = [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; };
    std::vector<std::wstring> out;
    if (valid.size() <= keep) return out;
    size_t excess = valid.size() - keep;
    for (size_t i = 0; i + 1 < valid.size() && excess > 0; ++i) {  // i + 1 < size: never the newest
        const bool kept = std::any_of(protect.begin(), protect.end(), [&](const std::wstring& p) {
            return same(valid[i].name, p);
        });
        if (kept) continue;
        out.push_back(valid[i].name);
        --excess;
    }
    // A generation a kept one refers to stays (repeated: keeping it may keep what it refers to).
    for (bool changed = true; changed;) {
        changed = false;
        for (const GenerationEntry& v : valid) {
            const bool dropped = std::any_of(out.begin(), out.end(), [&](const std::wstring& o) { return same(o, v.name); });
            if (dropped) continue;
            for (const std::wstring& r : v.refs) {
                const auto it = std::find_if(out.begin(), out.end(), [&](const std::wstring& o) { return same(o, r); });
                if (it != out.end()) {
                    out.erase(it);
                    changed = true;
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- the rebuild

// What the archive step before the sandbox rebuild did.
enum class Archive : uint8_t {
    Off,      // host_save=sandbox: nothing archived (today's behaviour)
    Nothing,  // nothing to archive (no sandbox, no _prev, nothing set aside)
    Done,     // everything archived and verified (or already identical to a verified generation)
    Failed,   // something could not be archived: nothing may be deleted
};

inline const char* archive_name(Archive a) {
    switch (a) {
        case Archive::Off: return "off";
        case Archive::Nothing: return "nothing to archive";
        case Archive::Done: return "archived";
        case Archive::Failed: return "FAILED";
    }
    return "?";
}

struct RebuildPlan {
    bool delete_prev = false;             // the old <sandbox>_prev is deleted ...
    bool rotate_sandbox_to_prev = false;  // ... and <sandbox> becomes <sandbox>_prev (today's behaviour)
    bool set_sandbox_aside = false;       // <sandbox> is renamed <sandbox>_unexported_<gen>, _prev is left alone
    bool fall_back_to_block = false;      // too many set aside already: nothing touched, the guard blocks this run
};

// At most this many sandboxes are kept set aside; one more failed export falls back to block (writes refused, the
// "Failed to save data" loop of P6) instead of filling the disk.
inline constexpr size_t kMaxAside = 3;

// Fail safe: when the archive failed nothing is deleted, the old sandbox is set aside (disk instead of data) and
// _prev stays; the next start in export/probe archives all of them again. With kMaxAside already set aside: block.
constexpr RebuildPlan plan_rebuild(bool sandbox_exists, Archive a, size_t asides_existing = 0) {
    if (!sandbox_exists) return {};
    if (a == Archive::Failed) {
        if (asides_existing >= kMaxAside) return {false, false, false, true};
        return {false, false, true, false};
    }
    return {true, true, false, false};
}

// ---------------------------------------------------------------- the export folder

// Where the generations may go: an absolute folder outside %LOCALAPPDATA%\Pearl Abyss (the save folder's tree, its
// sandboxes, and the Auto-Cloud root `save\<id>` that steam_autocloud.vdf marks), not containing the save folder,
// and not under Steam's userdata (Steam Cloud's local copies). nullopt = fine. (The file system part also refuses a
// folder with a steam_autocloud.vdf in it or above it.) All paths absolute; compared as saveguard::is_under does.
template <class IsUnder>
std::optional<std::string> export_dir_problem(std::wstring_view dir, std::wstring_view save_dir,
                                              std::wstring_view game_data_root, IsUnder is_under) {
    if (dir.empty()) return "empty";
    const bool drive = dir.size() >= 3 && dir[1] == L':' && (dir[2] == L'\\' || dir[2] == L'/');
    const bool unc = dir.size() >= 3 && (dir[0] == L'\\' || dir[0] == L'/') && (dir[1] == L'\\' || dir[1] == L'/');
    if (!drive && !unc) return "not an absolute path";
    if (!game_data_root.empty() && is_under(dir, game_data_root)) {
        return "inside %LOCALAPPDATA%\\Pearl Abyss (the save folder's tree, maybe in a Steam Cloud root)";
    }
    if (!save_dir.empty() && is_under(save_dir, dir)) return "it contains the save folder";
    std::wstring low(dir);
    for (wchar_t& c : low) c = c == L'/' ? L'\\' : (c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c);
    low += L'\\';
    if (low.find(L"\\steam\\userdata\\") != std::wstring::npos) return "under Steam's userdata (Steam Cloud's copies)";
    return std::nullopt;
}

// The same rules on what the file system says, not on the spelling (review of phase E, M1): a junction, symbolic
// link, 8.3 short name, SUBST drive or \\localhost\C$ path names the same folder under another text. The file system
// part (host_save_export.cpp) opens each existing ancestor of the export folder, nearest first, and gives its
// volume serial + file id, whether any of them is a reparse point, the ids of the save folder and of
// %LOCALAPPDATA%\Pearl Abyss (and of every ancestor of the save folder, resolved) and the export folder's own id when
// it exists. nullopt = fine.
struct NodeId {
    uint32_t volume = 0;
    uint64_t index = 0;
    bool operator==(const NodeId&) const = default;
};

struct ResolvedDir {
    bool readable = false;                // every id below could be read
    bool reparse = false;                 // the export folder or an ancestor of it is a reparse point
    std::vector<NodeId> root_ancestors;   // the existing export folder (or its nearest existing ancestor) and up
    std::optional<NodeId> root;           // the export folder itself, when it exists
    std::vector<NodeId> forbidden;        // the save folder and %LOCALAPPDATA%\Pearl Abyss (followed if links)
    std::vector<NodeId> save_ancestors;   // the save folder (resolved) and every folder above it
};

inline std::optional<std::string> resolved_dir_problem(const ResolvedDir& r) {
    if (!r.readable) return "its folders (or the save folder's) could not be identified";
    if (r.reparse) return "it or a folder above it is a link (junction, symbolic link or mount point)";
    for (const NodeId& a : r.root_ancestors) {
        if (std::find(r.forbidden.begin(), r.forbidden.end(), a) != r.forbidden.end()) {
            return "it resolves inside the save folder or %LOCALAPPDATA%\\Pearl Abyss";
        }
    }
    if (r.root && std::find(r.save_ancestors.begin(), r.save_ancestors.end(), *r.root) != r.save_ancestors.end()) {
        return "it resolves to a folder that contains the save folder";
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- the manifest

// The text formats keep one record per line and '|' between fields; a field never holds '|', CR or LF ('_' instead).
inline std::string field(std::string_view v) {
    std::string s(v);
    for (char& c : s) {
        if (c == '|' || c == '\r' || c == '\n') c = '_';
    }
    return s;
}

inline std::vector<std::string_view> split(std::string_view s, char sep = '|') {
    std::vector<std::string_view> out;
    size_t at = 0;
    for (;;) {
        const size_t p = s.find(sep, at);
        if (p == std::string_view::npos) {
            out.push_back(s.substr(at));
            return out;
        }
        out.push_back(s.substr(at, p - at));
        at = p + 1;
    }
}

inline std::optional<uint64_t> parse_u64(std::string_view s) {
    if (s.empty() || s.size() > 20) return std::nullopt;
    uint64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return std::nullopt;
        const uint64_t d = static_cast<uint64_t>(c - '0');
        if (v > (UINT64_MAX - d) / 10) return std::nullopt;
        v = v * 10 + d;
    }
    return v;
}

inline std::optional<int64_t> parse_i64(std::string_view s) {
    const bool neg = !s.empty() && s.front() == '-';
    const auto u = parse_u64(neg ? s.substr(1) : s);
    if (!u || *u > static_cast<uint64_t>(INT64_MAX)) return std::nullopt;
    return neg ? -static_cast<int64_t>(*u) : static_cast<int64_t>(*u);
}

inline bool sha_text_ok(std::string_view s) {
    return s.size() == 64 &&
           std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

struct SourceRecord {
    std::string label;          // "sandbox", "prev", "aside-<gen>"
    std::string from;           // the folder it was copied from (UTF-8)
    std::string produced_by = "unknown";  // the run whose sandbox it was (its journal), or "unknown"
    std::string tree_id;        // the folder's volume:file id when archived ("" if unread)
    std::string identical_to;   // not copied: byte-identical to "<generation>/<label>" (empty = copied here)
};

struct FileRecord {
    std::string source;  // a SourceRecord label, or "journal"
    std::string path;    // relative to the source folder, '\' separated (UTF-8)
    uint64_t size = 0;
    std::string sha256;  // 64 lower-case hex digits
    uint64_t mtime = 0;  // FILETIME of the last write (copied onto the export)
};

struct SlotRecord {
    std::string source, account, folder;
    std::string cls;  // slot_class_name or folder_kind_name
};

struct ProbeRecord {
    std::string source, account, folder;
    std::string verdict;  // verdict_name
    std::string reasons;
};

struct Manifest {
    uint32_t format = 1;
    std::string game_build = "1.0.0.2976";
    std::string pe_timestamp = "0x6AB28F00";
    std::string mod_build;   // "0x1234abcd" (+ " dirty"), or "unknown"
    std::string generation;  // its folder name
    std::string created;     // local time "2026-10-14 19:02:32" (for people)
    std::string created_utc; // "2026-10-14T17:02:32Z"
    uint64_t seq = 0;        // monotonic across the export folder: the order of "newest" (never the local-time name)
    std::string run;         // the run that made this export (its start)
    std::string run_start;
    std::string mode;        // host_save at that start
    std::vector<SourceRecord> sources;
    std::vector<std::string> accounts;  // "source|account"
    std::vector<FileRecord> files;
    std::vector<SlotRecord> slots;
    std::vector<ProbeRecord> probes;
    std::vector<std::string> notes;
    bool complete = false;  // the last line "state=complete" (written after every file was verified)
};

inline constexpr std::string_view kManifestHeader = "cdmp host save generation (P8 S35 phase E)";
inline constexpr std::string_view kManifestFile = "manifest.txt";

inline std::string manifest_text(const Manifest& m) {
    std::string s(kManifestHeader);
    s += "\nformat=" + std::to_string(m.format) + "\n";
    s += "game_build=" + field(m.game_build) + "\n";
    s += "pe_timestamp=" + field(m.pe_timestamp) + "\n";
    s += "mod_build=" + field(m.mod_build) + "\n";
    s += "generation=" + field(m.generation) + "\n";
    s += "created=" + field(m.created) + "\n";
    s += "created_utc=" + field(m.created_utc) + "\n";
    s += "seq=" + std::to_string(m.seq) + "\n";
    s += "run=" + field(m.run) + "\n";
    s += "run_start=" + field(m.run_start) + "\n";
    s += "mode=" + field(m.mode) + "\n";
    for (const SourceRecord& r : m.sources) {
        s += "source=" + field(r.label) + "|" + field(r.from) + "|" + field(r.produced_by) + "|" + field(r.tree_id) +
             "|" + field(r.identical_to) + "\n";
    }
    for (const std::string& a : m.accounts) {
        const auto parts = split(a);
        s += "account=" + field(parts[0]) + "|" + field(parts.size() > 1 ? parts[1] : std::string_view()) + "\n";
    }
    for (const FileRecord& f : m.files) {
        s += "file=" + field(f.source) + "|" + field(f.path) + "|" + std::to_string(f.size) + "|" + field(f.sha256) +
             "|" + std::to_string(f.mtime) + "\n";
    }
    for (const SlotRecord& r : m.slots) {
        s += "slot=" + field(r.source) + "|" + field(r.account) + "|" + field(r.folder) + "|" + field(r.cls) + "\n";
    }
    for (const ProbeRecord& r : m.probes) {
        s += "probe=" + field(r.source) + "|" + field(r.account) + "|" + field(r.folder) + "|" + field(r.verdict) +
             "|" + field(r.reasons) + "\n";
    }
    for (const std::string& n : m.notes) s += "note=" + field(n) + "\n";
    if (m.complete) s += "state=complete\n";
    return s;
}

// nullopt on a wrong header or a malformed record (unknown keys are skipped). `complete` only from the last line.
inline std::optional<Manifest> parse_manifest(std::string_view text) {
    Manifest m;
    bool first = true, ended = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        if (e == std::string_view::npos) e = text.size();
        std::string_view line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (first) {
            if (line != kManifestHeader) return std::nullopt;
            first = false;
            continue;
        }
        if (line.empty()) continue;
        if (ended) return std::nullopt;  // nothing after state=complete
        const size_t eq = line.find('=');
        if (eq == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, eq), v = line.substr(eq + 1);
        if (key == "format") {
            const auto n = parse_u64(v);
            if (!n || *n != 1) return std::nullopt;
            m.format = 1;
        } else if (key == "game_build") {
            m.game_build = v;
        } else if (key == "pe_timestamp") {
            m.pe_timestamp = v;
        } else if (key == "mod_build") {
            m.mod_build = v;
        } else if (key == "generation") {
            m.generation = v;
        } else if (key == "created_utc") {
            m.created_utc = v;
        } else if (key == "seq") {
            const auto n = parse_u64(v);
            if (!n) return std::nullopt;
            m.seq = *n;
        } else if (key == "created") {
            m.created = v;
        } else if (key == "run") {
            m.run = v;
        } else if (key == "run_start") {
            m.run_start = v;
        } else if (key == "mode") {
            m.mode = v;
        } else if (key == "source") {
            const auto p = split(v);
            if (p.size() != 5 || p[0].empty()) return std::nullopt;
            m.sources.push_back({std::string(p[0]), std::string(p[1]), std::string(p[2]), std::string(p[3]),
                                 std::string(p[4])});
        } else if (key == "account") {
            const auto p = split(v);
            if (p.size() != 2) return std::nullopt;
            m.accounts.push_back(std::string(p[0]) + "|" + std::string(p[1]));
        } else if (key == "file") {
            const auto p = split(v);
            if (p.size() != 5) return std::nullopt;
            const auto size = parse_u64(p[2]);
            const auto mtime = parse_u64(p[4]);
            if (p[0].empty() || p[1].empty() || !size || !mtime || !sha_text_ok(p[3])) return std::nullopt;
            m.files.push_back({std::string(p[0]), std::string(p[1]), *size, std::string(p[3]), *mtime});
        } else if (key == "slot") {
            const auto p = split(v);
            if (p.size() != 4) return std::nullopt;
            m.slots.push_back({std::string(p[0]), std::string(p[1]), std::string(p[2]), std::string(p[3])});
        } else if (key == "probe") {
            const auto p = split(v);
            if (p.size() != 5) return std::nullopt;
            m.probes.push_back(
                {std::string(p[0]), std::string(p[1]), std::string(p[2]), std::string(p[3]), std::string(p[4])});
        } else if (key == "note") {
            m.notes.emplace_back(v);
        } else if (key == "state") {
            if (v != "complete") return std::nullopt;
            m.complete = true;
            ended = true;
        }
    }
    if (first) return std::nullopt;
    return m;
}

// ---------------------------------------------------------------- trees and verification

struct TreeFile {
    std::string path;  // relative, '\' separated
    uint64_t size = 0;
    std::string sha256;
};

inline std::string lower_ascii(std::string_view s) {
    std::string o(s);
    for (char& c : o) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    return o;
}

// The same files (paths compared without case, as the file system does), sizes and hashes.
inline bool same_tree(std::vector<TreeFile> a, std::vector<TreeFile> b) {
    if (a.size() != b.size()) return false;
    const auto by_path = [](const TreeFile& x, const TreeFile& y) { return lower_ascii(x.path) < lower_ascii(y.path); };
    std::sort(a.begin(), a.end(), by_path);
    std::sort(b.begin(), b.end(), by_path);
    for (size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(a[i].path) != lower_ascii(b[i].path) || a[i].size != b[i].size || a[i].sha256 != b[i].sha256) {
            return false;
        }
    }
    return true;
}

inline std::vector<TreeFile> tree_of(const Manifest& m, std::string_view source) {
    std::vector<TreeFile> out;
    for (const FileRecord& f : m.files) {
        if (f.source == source) out.push_back({f.path, f.size, f.sha256});
    }
    return out;
}

// Every expected file present in `actual` with the same size and hash: the problems, one per file (empty = verified).
inline std::vector<std::string> verify_tree(const std::vector<TreeFile>& expected, const std::vector<TreeFile>& actual) {
    std::vector<std::string> problems;
    for (const TreeFile& e : expected) {
        const auto it = std::find_if(actual.begin(), actual.end(),
                                     [&](const TreeFile& a) { return lower_ascii(a.path) == lower_ascii(e.path); });
        if (it == actual.end()) {
            problems.push_back(e.path + ": missing");
        } else if (it->size != e.size) {
            problems.push_back(e.path + ": size " + std::to_string(it->size) + " instead of " + std::to_string(e.size));
        } else if (it->sha256 != e.sha256) {
            problems.push_back(e.path + ": SHA-256 differs");
        }
    }
    for (const TreeFile& a : actual) {
        const bool known = std::any_of(expected.begin(), expected.end(),
                                       [&](const TreeFile& e) { return lower_ascii(a.path) == lower_ascii(e.path); });
        if (!known) problems.push_back(a.path + ": not in the manifest");
    }
    return problems;
}

// ---------------------------------------------------------------- the run journal (observations)

// One file per run, <export dir>\runs\<run>.txt, appended while the run lasts: its sandbox's folder id (so the
// next start knows which run wrote the sandbox it archives, and the _prev after it), then one line per
// SavePlayerToSlot 0x186E450 / FlushPendingSaveData 0x1871930 entry the guest_save_host hooks saw (guest_own_save
// probe or 1; with 0 nothing is hooked and the journal says so). Observation only: it authorises nothing.
inline constexpr std::string_view kJournalHeader = "cdmp host save run journal (P8 S35 phase E)";
inline constexpr std::string_view kJournalTruncated = "truncated";
inline constexpr uint32_t kMaxJournalEvents = 4000;

struct JournalHeader {
    std::string run, start, sandbox_id, mod_build, mode;
};

// The audit's "positive identity" (S34 §5 item 1, AUDITORIA-5 change 1): the validated host user or the game's own
// session, never classify_owner's fallback "no session 2 was ever built".
inline bool positive_host(const gsv::UserFacts& f) {
    return gsv::classify_owner(f) == gsv::Owner::Host && (f.is_host || (f.session_known && f.session_is_host));
}

inline const char* owner_code(gsv::Owner o) {
    switch (o) {
        case gsv::Owner::Host: return "host";
        case gsv::Owner::User2: return "user2";
        case gsv::Owner::Unidentified: return "unidentified";
    }
    return "?";
}
inline std::optional<gsv::Owner> parse_owner_code(std::string_view s) {
    if (s == "host") return gsv::Owner::Host;
    if (s == "user2") return gsv::Owner::User2;
    if (s == "unidentified") return gsv::Owner::Unidentified;
    return std::nullopt;
}

inline constexpr uint32_t kNoSlot = 0xFFFFFFFFu;

struct SaveEvent {
    uint64_t seq = 0;
    std::string time;
    gsv::Owner owner = gsv::Owner::Unidentified;
    bool positive_host = false;
    std::string caller;        // "autosave", "manual 2910", "OTHER"
    uint32_t asked = 0;        // the slot the game asked for
    uint32_t written = kNoSlot;  // the slot the original got (kNoSlot: refused, nothing written)
    std::string action;        // "pass", "REMAP", "REFUSE"
    bool result_read = false;  // *res read (protected)
    int32_t result = -1;
    uint32_t depth = 0;        // Save/Flush calls already open on this thread (0 = not nested)
    uint64_t redirect_failed = 0;  // save_hooks_redirect_failed() at that moment
};

struct FlushEvent {
    uint64_t seq = 0;
    std::string time;
    gsv::Owner owner = gsv::Owner::Unidentified;
    bool positive_host = false;
    std::string caller;  // "logout phase", "flush timer", "OTHER"
    bool pending_read = false;
    int32_t pending = -1;  // mgr+0x5C before the call: -1 = nothing pending
    bool result_read = false;
    int32_t result = -1;
    uint32_t depth = 0;
};

struct Journal {
    JournalHeader header;
    bool hooks = false;     // a "hooks=" line: Save/Flush were observed in this run
    bool hook_save = false, hook_flush = false;
    bool truncated = false;  // the cap was reached, or a line could not be read: not every event is known
    std::vector<SaveEvent> saves;
    std::vector<FlushEvent> flushes;
};

inline std::string journal_header_text(const JournalHeader& h) {
    std::string s(kJournalHeader);
    s += "\nrun=" + field(h.run) + "\nstart=" + field(h.start) + "\nsandbox_id=" + field(h.sandbox_id) +
         "\nmod_build=" + field(h.mod_build) + "\nmode=" + field(h.mode) + "\n";
    return s;
}

inline std::string journal_hooks_line(bool save, bool flush) {
    return std::string("hooks=save:") + (save ? "1" : "0") + " flush:" + (flush ? "1" : "0") + "\n";
}

inline std::string slot_text(uint32_t s) { return s == kNoSlot ? std::string("-") : std::to_string(s); }

inline std::string journal_save_line(const SaveEvent& e) {
    return "save|" + std::to_string(e.seq) + "|" + field(e.time) + "|" + owner_code(e.owner) + "|" +
           (e.positive_host ? "1" : "0") + "|" + field(e.caller) + "|" + std::to_string(e.asked) + "|" +
           slot_text(e.written) + "|" + field(e.action) + "|" + (e.result_read ? std::to_string(e.result) : "?") +
           "|" + std::to_string(e.depth) + "|" + std::to_string(e.redirect_failed) + "\n";
}

inline std::string journal_flush_line(const FlushEvent& e) {
    return "flush|" + std::to_string(e.seq) + "|" + field(e.time) + "|" + owner_code(e.owner) + "|" +
           (e.positive_host ? "1" : "0") + "|" + field(e.caller) + "|" +
           (e.pending_read ? std::to_string(e.pending) : "?") + "|" + (e.result_read ? std::to_string(e.result) : "?") +
           "|" + std::to_string(e.depth) + "\n";
}

// nullopt only on a wrong header; an unreadable event line (a cut write) marks the journal truncated.
inline std::optional<Journal> parse_journal(std::string_view text) {
    Journal j;
    bool first = true;
    size_t at = 0;
    while (at < text.size()) {
        size_t e = text.find('\n', at);
        const bool cut = e == std::string_view::npos;
        if (cut) e = text.size();
        std::string_view line = text.substr(at, e - at);
        at = e + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (first) {
            if (line != kJournalHeader) return std::nullopt;
            first = false;
            continue;
        }
        if (line.empty()) continue;
        if (cut) {  // the last line has no end: a write cut by a crash
            j.truncated = true;
            break;
        }
        if (line == kJournalTruncated) {
            j.truncated = true;
            continue;
        }
        const auto kv = [&](std::string_view key, std::string& out) {
            if (line.size() > key.size() && line.substr(0, key.size()) == key && line[key.size()] == '=') {
                out = line.substr(key.size() + 1);
                return true;
            }
            return false;
        };
        if (kv("run", j.header.run) || kv("start", j.header.start) || kv("sandbox_id", j.header.sandbox_id) ||
            kv("mod_build", j.header.mod_build) || kv("mode", j.header.mode)) {
            continue;
        }
        if (line.rfind("hooks=", 0) == 0) {
            j.hooks = true;
            j.hook_save = line.find("save:1") != std::string_view::npos;
            j.hook_flush = line.find("flush:1") != std::string_view::npos;
            continue;
        }
        const auto p = split(line);
        const auto read_result = [](std::string_view s, bool& read, int32_t& v) {
            if (s == "?") {
                read = false;
                return true;
            }
            const auto n = parse_i64(s);
            if (!n || *n < INT32_MIN || *n > INT32_MAX) return false;
            read = true;
            v = static_cast<int32_t>(*n);
            return true;
        };
        if (p[0] == "save" && p.size() == 12) {
            SaveEvent ev;
            const auto seq = parse_u64(p[1]);
            const auto owner = parse_owner_code(p[3]);
            const auto asked = parse_u64(p[6]);
            const auto depth = parse_u64(p[10]);
            const auto rf = parse_u64(p[11]);
            std::optional<uint64_t> written = p[7] == "-" ? std::optional<uint64_t>(kNoSlot) : parse_u64(p[7]);
            if (seq && owner && asked && *asked < kNoSlot && written && *written <= kNoSlot && depth && rf &&
                (p[4] == "0" || p[4] == "1") && read_result(p[9], ev.result_read, ev.result)) {
                ev.seq = *seq;
                ev.time = p[2];
                ev.owner = *owner;
                ev.positive_host = p[4] == "1";
                ev.caller = p[5];
                ev.asked = static_cast<uint32_t>(*asked);
                ev.written = static_cast<uint32_t>(*written);
                ev.action = p[8];
                ev.depth = static_cast<uint32_t>(std::min<uint64_t>(*depth, 1000));
                ev.redirect_failed = *rf;
                j.saves.push_back(std::move(ev));
                continue;
            }
        } else if (p[0] == "flush" && p.size() == 9) {
            FlushEvent ev;
            const auto seq = parse_u64(p[1]);
            const auto owner = parse_owner_code(p[3]);
            const auto depth = parse_u64(p[8]);
            if (seq && owner && depth && (p[4] == "0" || p[4] == "1") &&
                read_result(p[6], ev.pending_read, ev.pending) && read_result(p[7], ev.result_read, ev.result)) {
                ev.seq = *seq;
                ev.time = p[2];
                ev.owner = *owner;
                ev.positive_host = p[4] == "1";
                ev.caller = p[5];
                ev.depth = static_cast<uint32_t>(std::min<uint64_t>(*depth, 1000));
                j.flushes.push_back(std::move(ev));
                continue;
            }
        }
        j.truncated = true;  // an event line this code cannot read: not every event is known
    }
    if (first) return std::nullopt;
    return j;
}

// ---------------------------------------------------------------- probe: one slot

enum class Verdict : uint8_t { Candidate, NotCandidate, Unknown };
inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::Candidate: return "candidate";
        case Verdict::NotCandidate: return "not-candidate";
        case Verdict::Unknown: return "unknown";
    }
    return "?";
}

// What the journal of the run that wrote a tree says about one slot.
struct SlotObservation {
    bool journal = false;            // the producing run's journal was found
    bool hooks = false;              // ... and Save was hooked in that run (guest_own_save probe or 1)
    bool truncated = false;          // ... and it is not complete
    uint32_t host_positive_ok = 0;   // host saves written to this slot: positive identity, *res read as 0, not nested
    uint32_t host_fallback = 0;      // host saves written to this slot identified only by the fallback
    uint32_t host_failed = 0;        // host saves written to this slot whose *res was not 0 or not readable
    uint32_t nested = 0;             // saves written to this slot inside another Save/Flush of the same thread
    uint32_t other_owner = 0;        // user 2 / unidentified saves the original wrote to this slot
    uint32_t flush_pending_here = 0; // flushes that found this slot pending at +0x5C (a deferred buffer)
    uint32_t flush_pending_unread = 0;  // flushes whose pending slot could not be read (any slot)
    uint64_t redirect_failed = 0;    // the largest save_hooks_redirect_failed() seen at a save
};

inline SlotObservation observe_slot(const Journal* j, uint32_t slot) {
    SlotObservation o;
    if (!j) return o;
    o.journal = true;
    o.hooks = j->hooks && j->hook_save;
    o.truncated = j->truncated;
    for (const SaveEvent& e : j->saves) {
        o.redirect_failed = std::max(o.redirect_failed, e.redirect_failed);
        if (e.written != slot) continue;
        if (e.depth > 0) ++o.nested;
        if (e.owner != gsv::Owner::Host) {
            ++o.other_owner;
        } else if (!e.result_read || e.result != 0) {
            ++o.host_failed;
        } else if (!e.positive_host) {
            ++o.host_fallback;
        } else if (e.depth == 0) {
            ++o.host_positive_ok;
        }
    }
    for (const FlushEvent& f : j->flushes) {
        if (!f.pending_read) {
            ++o.flush_pending_unread;
        } else if (f.pending >= 0 && static_cast<uint32_t>(f.pending) == slot) {
            ++o.flush_pending_here;
        }
    }
    return o;
}

// lobby.save and save.save written further apart than this are not taken as one Save (one took ~120 ms in S34).
inline constexpr uint64_t kPairGapMs = 10000;

struct SlotFacts {
    std::optional<SlotFolder> folder;  // nullopt: not a slot folder
    bool lobby = false, save = false;  // the two files present
    gsv::HeaderCheck lobby_check = gsv::HeaderCheck::Ok, save_check = gsv::HeaderCheck::Ok;
    std::optional<bool> identical_to_real;  // both files byte-identical to the real folder's (nullopt: unknown)
    uint64_t pair_gap_ms = 0;               // |write time of lobby - write time of save|
    SlotObservation obs;
};

struct Classification {
    Verdict verdict = Verdict::Unknown;
    std::string reasons;
};

// Every rule that excludes a slot comes first; then what makes it unknown; "candidate" only when everything this
// code can check holds, and always with the audit's open conditions in its reasons.
inline Classification classify_slot(const SlotFacts& f) {
    const auto no = [](std::string why) { return Classification{Verdict::NotCandidate, std::move(why)}; };
    const auto unknown = [](std::string why) { return Classification{Verdict::Unknown, std::move(why)}; };
    if (!f.folder) return no("not a slot folder");
    if (f.folder->kind != FolderKind::Slot) {
        return no(std::string(folder_kind_name(f.folder->kind)) +
                  ": the game's own commit was cut here; only its native recovery may use it");
    }
    const uint32_t n = f.folder->number;
    switch (slot_class(n)) {
        case SlotClass::Host: break;
        case SlotClass::CrossSave: return no("slot 3 is CrossSave (0x186E450 branch 3 uploads it): excluded");
        case SlotClass::GuestPool:
            return no(n == gsv::kGuestSlot ? "slot 204 is the guest's own slot (guest_own_save): never the host's"
                                           : "slots 200-207 are the guest pool: never the host's");
        case SlotClass::Outside: return no("outside the whitelist 0-2 / 100-108");
    }
    if (!f.lobby || !f.save) return no(!f.lobby && !f.save ? "no lobby.save nor save.save" : !f.lobby ? "no lobby.save" : "no save.save");
    if (f.lobby_check != gsv::HeaderCheck::Ok) return no(std::string("lobby.save header: ") + gsv::header_check_name(f.lobby_check));
    if (f.save_check != gsv::HeaderCheck::Ok) return no(std::string("save.save header: ") + gsv::header_check_name(f.save_check));
    if (f.identical_to_real && *f.identical_to_real) return no("identical to the real folder's slot: nothing new");
    const SlotObservation& o = f.obs;
    if (o.other_owner) return no(std::to_string(o.other_owner) + " save(s) of user 2 / an unidentified user went to this slot");
    if (!o.journal) return unknown("no journal of the run that wrote it (it ran with host_save=sandbox, or before)");
    if (!o.hooks) return unknown("SavePlayerToSlot was not observed in that run (guest_own_save=0)");
    if (o.truncated) return unknown("the run's journal is incomplete (cap reached or a cut line)");
    if (!f.identical_to_real) return unknown("the real folder's slot could not be read to compare");
    if (o.nested) return unknown(std::to_string(o.nested) + " save(s) to this slot nested in another Save/Flush");
    if (o.host_failed) return unknown(std::to_string(o.host_failed) + " host save(s) to this slot failed or had an unreadable result: the pair may be mixed");
    if (o.flush_pending_here) return unknown("a deferred Flush found this slot pending: the buffer's provenance is not known");
    if (o.flush_pending_unread) return unknown("a Flush's pending slot could not be read");
    if (o.redirect_failed) return unknown(std::to_string(o.redirect_failed) + " redirected call(s) failed in that run");
    if (f.pair_gap_ms > kPairGapMs) return unknown("lobby.save and save.save were written " + std::to_string(f.pair_gap_ms / 1000) + " s apart: maybe not one Save");
    if (!o.host_positive_ok) {
        if (o.host_fallback) return unknown("the host's save(s) here were identified only by the fallback (no guest session), not positively");
        return unknown("changed, but no Save of the host to this slot was observed (another path, or outside the hooks)");
    }
    return {Verdict::Candidate,
            std::to_string(o.host_positive_ok) +
                " host save(s) with positive identity and result 0, no other owner, headers valid, changed; NOT "
                "proven: complete I/O coverage, nested contexts across threads, an indivisible pair, world isolation "
                "(audit S34 section 5, M1/M2)"};
}

}  // namespace cdmp::session2::hsv
