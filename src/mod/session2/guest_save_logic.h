#pragma once
// P8 session 25, the guest's own save on the host, PHASE A (host side only):
// docs/research/noche/progreso-invitado.md §3 and its "Implementado (fase A)"
// section. The pure part of guest_save_host.cpp, with no game access, so the
// tests can run it: the [session2] guest_own_save modes, the slot rules, the
// save header check (the game's own, 0x2357DA0 / 0x2358EF0), the decisions of
// the LoadSlot (0x186F570) and SavePlayerToSlot (0x186E450) entry hooks per
// caller, the slot-file plan for 0x235B100, and the mailbox generations.
//
// Build 1.0.0.2976 (base 0x140000000). Every address below is an RVA.
//
// THE SLOT IS 204, NOT 207 (deviation from the report, found in phase A):
// GetOrCreateSlot (.xtls 0xEF537A0, through the .rsrc thunk 0x2355EC0) sets
// slot+0x699 = 1 for every "slotN" whose N is not a number, is 201 (an
// obfuscated constant: [.xtls 0xEC0FE84] ^ 0x48D9F781 = 0xC9) or is 205-207
// (`N - 0xCD < 3`), on the existing path too. CommitSlot (.xtls 0xEF51850)
// does no I/O at all while +0x699 is set, so a 207 made (or touched) by
// GetOrCreateSlot is a MEMORY-ONLY slot: its load reads nothing and returns 0
// (the empty-character trap of the report's §1.4), its save writes nothing.
// The disk slots LoadSlot accepts in 200-207 are 200, 202, 203 and 204; 204
// is the guest's (the report's 4-player pool becomes 200, 202, 203, 204).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cdmp::session2::gsv {

// ---------------------------------------------------------------- the option

// [session2] guest_own_save: 0 (default) | probe | 1.
//   probe: the LoadSlot / SavePlayerToSlot / FlushPendingSaveData probes log
//          who calls them (caller, user, slot, result, thread); the debug
//          slot is staged and validated; nothing is remapped, moved or refused.
//   1:     user 2 loads and saves its own slot (kGuestSlot) in the sandbox;
//          fail closed: never the host's slot.
enum class Mode : uint8_t { Off, Probe, On };

struct ParsedMode {
    Mode mode = Mode::Off;
    bool known = true;  // false: an unknown value, treated as Off (warned)
};

inline ParsedMode parse_mode(std::string_view raw) {
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    if (const size_t c = raw.find_first_of(";#"); c != std::string_view::npos) raw = raw.substr(0, c);
    while (!raw.empty() && (raw.back() == ' ' || raw.back() == '\t' || raw.back() == '\r' || raw.back() == '\n')) {
        raw.remove_suffix(1);
    }
    std::string v(raw);
    for (char& ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    if (v.empty() || v == "0" || v == "off" || v == "no" || v == "false") return {Mode::Off, true};
    if (v == "probe") return {Mode::Probe, true};
    if (v == "1" || v == "on" || v == "yes" || v == "true") return {Mode::On, true};
    return {Mode::Off, false};
}

inline const char* mode_name(Mode m) {
    switch (m) {
        case Mode::Off: return "0";
        case Mode::Probe: return "probe";
        case Mode::On: return "1";
    }
    return "?";
}

// ---------------------------------------------------------------- slots

inline constexpr uint32_t kGuestSlot = 204;

// The numbers LoadSlot 0x186F570 accepts (0x186F5D7..F0): 0-2, 3, 100-108, 200-207.
constexpr bool slot_number_valid(uint32_t n) { return n < 3 || n == 3 || (n - 100) < 9 || (n - 200) < 8; }

// GetOrCreateSlot's rule for a numeric name (see the top): +0x699 = 1, no disk I/O.
constexpr bool slot_memory_only(uint32_t n) { return n == 201 || (n - 205) < 3; }

// A slot the guest's save may own on the host: accepted by LoadSlot, in 200-207 (nobody else writes them:
// SavePlayerToSlot has two callers, the autosave 0-2 rotation and 2910's 100-108; the refresh skips only 3 and
// 201; the title predicate, UserLogin's "most recent" and Sentry's attachments 0x186D9A0 count 0-2/100-108), and
// a disk slot (not memory-only).
constexpr bool guest_slot_allowed(uint32_t n) { return n >= 200 && n <= 207 && !slot_memory_only(n); }

// "slot204" (the game formats "%#%#" with "slot", 0x186F74E).
inline std::string slot_name(uint32_t n) { return "slot" + std::to_string(n); }

// ---------------------------------------------------------------- the save header

// The 0x80 bytes at the start of lobby.save / save.save, as the game checks them in the loader 0x2357DA0
// (0x2357F69..CB) and in the start-up enumeration 0x2358EF0 (both copy all 0x80 bytes to fileObj+0x98):
//   +0x00 u32 "SAVE" (0x45564153)   +0x04 u16 version 1..2     +0x06 u16 header size 0x80
//   +0x0C u32 flags, only bit 1     +0x12 u32 raw size != 0    +0x16 u32 data size, 0 < data <= raw
// and the loader reads `data` bytes after the header ([fileObj+0xAE]); phase A also wants the file to be exactly
// 0x80 + data (the report's §3.8) and at most kMaxFileSize.
inline constexpr size_t kHeaderSize = 0x80;
inline constexpr uint32_t kMagic = 0x45564153;
inline constexpr uint64_t kMaxFileSize = 16ull * 1024 * 1024;

// The raw (decompressed) size the loader allocates for: the game's own check is only 0 < data <= raw, so a forged raw
// size could ask it for gigabytes. The real files are far below both bounds (template: lobby 0x21B for 0x186 of data,
// save 2 759 863 for 736 603, x3.7); LZ4's own ceiling is 255:1.
inline constexpr uint64_t kMaxRawSize = 64ull * 1024 * 1024;
inline constexpr uint64_t kMaxRawRatio = 255;

enum class HeaderCheck : uint8_t {
    Ok, TooShort, Magic, Version, HeaderSize, Flags, RawSize, DataSize, FileSize, TooBig, RawTooBig
};

inline const char* header_check_name(HeaderCheck c) {
    switch (c) {
        case HeaderCheck::Ok: return "ok";
        case HeaderCheck::TooShort: return "shorter than its 0x80-byte header";
        case HeaderCheck::Magic: return "magic is not SAVE";
        case HeaderCheck::Version: return "version not 1 or 2";
        case HeaderCheck::HeaderSize: return "header size not 0x80";
        case HeaderCheck::Flags: return "flags other than bit 1";
        case HeaderCheck::RawSize: return "raw size 0";
        case HeaderCheck::DataSize: return "data size 0 or above the raw size";
        case HeaderCheck::FileSize: return "file size is not 0x80 + data size";
        case HeaderCheck::TooBig: return "file larger than 16 MB";
        case HeaderCheck::RawTooBig: return "raw size above 64 MB or 255 times the data";
    }
    return "?";
}

struct HeaderInfo {
    uint16_t version = 0;
    uint32_t flags = 0;
    uint32_t raw_size = 0;
    uint32_t data_size = 0;
};

template <class T>
T load_le(const uint8_t* p) {
    T v{};
    std::memcpy(&v, p, sizeof v);
    return v;
}

// `head` = at least the first 0x80 bytes of the file, `file_size` = the whole file's size.
inline HeaderCheck check_header(std::span<const uint8_t> head, uint64_t file_size, HeaderInfo* out = nullptr) {
    if (head.size() < kHeaderSize || file_size < kHeaderSize) return HeaderCheck::TooShort;
    const uint8_t* h = head.data();
    HeaderInfo i;
    i.version = load_le<uint16_t>(h + 4);
    i.flags = load_le<uint32_t>(h + 0xC);
    i.raw_size = load_le<uint32_t>(h + 0x12);
    i.data_size = load_le<uint32_t>(h + 0x16);
    if (out) *out = i;
    if (load_le<uint32_t>(h) != kMagic) return HeaderCheck::Magic;
    if (i.version == 0 || i.version > 2) return HeaderCheck::Version;
    if (load_le<uint16_t>(h + 6) != kHeaderSize) return HeaderCheck::HeaderSize;
    if ((i.flags & ~2u) != 0) return HeaderCheck::Flags;
    if (i.raw_size == 0) return HeaderCheck::RawSize;
    if (i.data_size == 0 || i.data_size > i.raw_size) return HeaderCheck::DataSize;
    if (i.raw_size > kMaxRawSize || i.raw_size > kMaxRawRatio * i.data_size) return HeaderCheck::RawTooBig;
    if (file_size > kMaxFileSize) return HeaderCheck::TooBig;
    if (file_size != kHeaderSize + static_cast<uint64_t>(i.data_size)) return HeaderCheck::FileSize;
    return HeaderCheck::Ok;
}

using Header = std::array<uint8_t, kHeaderSize>;

// After a load: the game's copy at fileObj+0x98 (the loader copies the file's 0x80 bytes there) against the
// header read from the slot's file right before the load.
inline bool header_matches(const Header& loaded, const Header& on_disk) {
    return std::memcmp(loaded.data(), on_disk.data(), kHeaderSize) == 0;
}

// The two files of a slot.
enum class FileKind : uint8_t { Lobby, Save };
inline const char* file_kind_name(FileKind k) { return k == FileKind::Lobby ? "lobby" : "save"; }
inline const wchar_t* file_kind_file(FileKind k) { return k == FileKind::Lobby ? L"lobby.save" : L"save.save"; }

// 0x20688A0 loads "lobby" when [[actor+0x88]+1] == 0 (the user actor), else "save" (the character) (0x2068A5x).
inline FileKind load_kind(uint8_t descriptor_byte1) { return descriptor_byte1 == 0 ? FileKind::Lobby : FileKind::Save; }

// ---------------------------------------------------------------- the slot's files (0x235B100, the real layout)

// Live test of 12-10 00:36 (cdmp_20261012-003108.log): phase A read slot+0x688 as file OBJECTS and found "lobby 0
// save 0 other 2" after two good AddFile calls. The slot keeps its files in two structures [E]:
//   +0x688/+0x690  a vector of `const char*`, one per file: the NAME's characters. RemoveFile 0x235B450 strcmp's
//                  each entry with the name (0x235B4D4..); AddFile pushes the inserted key's chars (0x235B3ED..42F).
//   +0x008         the name -> file object hash map that the loader uses (0x20688A0: HashLittle 0x1364780, then
//                  HashMap_Find 0x612620(slot+8, hash, &key, &index), the object at entry+0x10, 0x20689D9..9FF).
//                  Its layout, from HashMap_Find: +0x00 u32 bucket count; +0x10 bucket array, 0x100 bytes per
//                  bucket: u32 n, then n x {u32 hash, u32 index} from +8; +0x18 entry* array (by index). An entry:
//                  +0x08 key = a string header {char* chars; u32 length (0xFFFFFFFF = not counted yet)}, +0x10 the
//                  value (the 0x140-byte file object, header at +0x98: what AddFile returns, 0x2358EF0 writes it).
// The names are lower case (0x20688A0 _strlwr's component names; "lobby" / "save" are its literals) and compared
// with the case as is (memcmp / strcmp).
inline constexpr uint32_t kSlotNameMap = 0x08;
inline constexpr uint32_t kMapBucketCount = 0x00;
inline constexpr uint32_t kMapBuckets = 0x10;
inline constexpr uint32_t kMapEntries = 0x18;
inline constexpr uint32_t kBucketSize = 0x100;
inline constexpr uint32_t kBucketPairs = 0x08;
inline constexpr uint32_t kMaxBucketPairs = (kBucketSize - kBucketPairs) / 8;  // 31
inline constexpr uint32_t kEntryKey = 0x08;
inline constexpr uint32_t kEntryValue = 0x10;
inline constexpr uint32_t kKeyChars = 0x00;
inline constexpr uint32_t kKeyLength = 0x08;
inline constexpr uint32_t kSlotNames = 0x688;
inline constexpr uint32_t kSlotNameCount = 0x690;
inline constexpr uint32_t kMaxBuckets = 4096;
inline constexpr uint32_t kMaxEntryIndex = 1u << 16;
inline constexpr uint32_t kMaxSlotFiles = 64;

enum class FileName : uint8_t { Lobby, Save, Other };
// A name's characters (up to the first NUL or the buffer's end).
inline FileName classify_file_name(std::span<const char> chars) {
    const auto end = std::find(chars.begin(), chars.end(), '\0');
    const std::string_view s(chars.data(), static_cast<size_t>(end - chars.begin()));
    if (s == "lobby") return FileName::Lobby;
    if (s == "save") return FileName::Save;
    return FileName::Other;
}

struct SlotFiles {
    uint32_t lobby = 0, save = 0, other = 0;
};

// One name as read (for the log too): where, its first bytes (up to 8, stopping at a NUL), what it is.
struct NameRead {
    uintptr_t ptr = 0;
    bool readable = false;
    uint8_t length = 0;  // bytes read before the NUL (8 = no NUL in the first 8)
    char chars[8] = {};
    FileName kind = FileName::Other;
};
struct MapEntryRead {
    uintptr_t entry = 0, value = 0;
    uint32_t hash = 0, key_length = 0;
    NameRead name;
};

struct SlotFilesView {
    bool ok = false;
    const char* error = "";
    SlotFiles names;   // the char* vector (+0x688)
    SlotFiles mapped;  // the map entries (+0x08) with a file object
    uintptr_t lobby = 0, save = 0;  // the file objects the loader finds
    std::vector<NameRead> name_list;
    std::vector<MapEntryRead> map_list;
};

// `mem.read(address, out, size)` -> bool: a guarded read (the game's memory in the mod, a fake image in the tests).
template <class Mem>
bool read_u32(const Mem& mem, uintptr_t a, uint32_t& v) {
    return a && mem.read(a, &v, sizeof v);
}
template <class Mem>
bool read_ptr(const Mem& mem, uintptr_t a, uintptr_t& v) {
    return a && mem.read(a, &v, sizeof v);
}

// Byte by byte up to 8 (a name near the end of a page must not fail the read of the rest).
template <class Mem>
NameRead read_name(const Mem& mem, uintptr_t chars) {
    NameRead n;
    n.ptr = chars;
    if (!chars) return n;
    for (uint8_t i = 0; i < sizeof n.chars; ++i) {
        char c = 0;
        if (!mem.read(chars + i, &c, 1)) return n;
        if (c == '\0') {
            n.readable = true;
            n.length = i;
            n.kind = classify_file_name(std::span<const char>(n.chars, sizeof n.chars));
            return n;
        }
        n.chars[i] = c;
    }
    n.readable = true;
    n.length = sizeof n.chars;
    n.kind = FileName::Other;  // longer than any name of ours
    return n;
}

inline void count_name(SlotFiles& f, FileName k) {
    (k == FileName::Lobby ? f.lobby : k == FileName::Save ? f.save : f.other) += 1;
}

template <class Mem>
SlotFilesView read_slot_files(const Mem& mem, uintptr_t slot) {
    SlotFilesView v;
    uint32_t count = 0;
    uintptr_t data = 0;
    if (!read_u32(mem, slot + kSlotNameCount, count) || count > kMaxSlotFiles) {
        v.error = "the name vector's count is unreadable or too large";
        return v;
    }
    if (count && (!read_ptr(mem, slot + kSlotNames, data) || !data)) {
        v.error = "the name vector is unreadable";
        return v;
    }
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t chars = 0;
        if (!read_ptr(mem, data + 8ull * i, chars)) {
            v.error = "a name pointer is unreadable";
            return v;
        }
        const NameRead n = read_name(mem, chars);
        if (!n.readable) {
            v.error = "a name is unreadable";
            v.name_list.push_back(n);
            return v;
        }
        v.name_list.push_back(n);
        count_name(v.names, n.kind);
    }
    const uintptr_t map = slot + kSlotNameMap;
    uint32_t buckets_n = 0;
    uintptr_t buckets = 0, entries = 0;
    if (!read_u32(mem, map + kMapBucketCount, buckets_n) || buckets_n > kMaxBuckets) {
        v.error = "the name map's bucket count is unreadable or too large";
        return v;
    }
    if (buckets_n && (!read_ptr(mem, map + kMapBuckets, buckets) || !buckets || !read_ptr(mem, map + kMapEntries, entries) ||
                      !entries)) {
        v.error = "the name map's arrays are unreadable";
        return v;
    }
    for (uint32_t b = 0; b < buckets_n; ++b) {
        const uintptr_t bucket = buckets + static_cast<uintptr_t>(b) * kBucketSize;
        uint32_t pairs = 0;
        if (!read_u32(mem, bucket, pairs) || pairs > kMaxBucketPairs) {
            v.error = "a bucket is unreadable or too full";
            return v;
        }
        for (uint32_t i = 0; i < pairs; ++i) {
            MapEntryRead e;
            uint32_t index = 0;
            uintptr_t key = 0, chars = 0;
            if (!read_u32(mem, bucket + kBucketPairs + 8ull * i, e.hash) ||
                !read_u32(mem, bucket + kBucketPairs + 8ull * i + 4, index) || index >= kMaxEntryIndex ||
                !read_ptr(mem, entries + 8ull * index, e.entry) || !e.entry ||
                !read_ptr(mem, e.entry + kEntryKey, key) || !key || !read_ptr(mem, key + kKeyChars, chars) ||
                !read_u32(mem, key + kKeyLength, e.key_length) || !read_ptr(mem, e.entry + kEntryValue, e.value)) {
                v.error = "a map entry is unreadable";
                return v;
            }
            e.name = read_name(mem, chars);
            if (!e.name.readable) {
                v.error = "a map entry's name is unreadable";
                v.map_list.push_back(e);
                return v;
            }
            v.map_list.push_back(e);
            if (!e.value) continue;
            count_name(v.mapped, e.name.kind);
            if (e.name.kind == FileName::Lobby) v.lobby = e.value;
            if (e.name.kind == FileName::Save) v.save = e.value;
        }
    }
    v.ok = true;
    return v;
}

// 0x235B100 does not look its name up first: a second call for a name already there pushes it into the name vector
// again (0x235B3E6..42F). So it is called only for a name that is missing from BOTH structures, a slot holding a
// name twice is refused, and so is one whose two structures disagree. Other files are left alone (vf7 adds every
// *.save of a folder).
enum class FilesPlan : uint8_t { Ready, AddBoth, AddLobby, AddSave, Duplicate, Inconsistent };
inline FilesPlan plan_slot_files(const SlotFiles& names, const SlotFiles& mapped) {
    if (names.lobby > 1 || names.save > 1 || mapped.lobby > 1 || mapped.save > 1) return FilesPlan::Duplicate;
    if ((names.lobby != 0) != (mapped.lobby != 0) || (names.save != 0) != (mapped.save != 0)) {
        return FilesPlan::Inconsistent;
    }
    if (names.lobby && names.save) return FilesPlan::Ready;
    if (!names.lobby && !names.save) return FilesPlan::AddBoth;
    return names.lobby ? FilesPlan::AddSave : FilesPlan::AddLobby;
}
inline const char* files_plan_name(FilesPlan p) {
    switch (p) {
        case FilesPlan::Ready: return "both files there";
        case FilesPlan::AddBoth: return "adds lobby and save";
        case FilesPlan::AddLobby: return "adds lobby";
        case FilesPlan::AddSave: return "adds save";
        case FilesPlan::Duplicate: return "a file name twice";
        case FilesPlan::Inconsistent: return "its name vector and its name map disagree";
    }
    return "?";
}

// For the log: every name read, raw ("'lobby'" or the bytes in hex), and every map entry with its hash and object.
inline std::string name_text(const NameRead& n) {
    if (!n.readable) return std::format("0x{:X}:unreadable", n.ptr);
    bool printable = true;
    for (uint8_t i = 0; i < n.length; ++i) printable = printable && n.chars[i] >= 0x20 && n.chars[i] < 0x7F;
    std::string s = std::format("0x{:X}:", n.ptr);
    if (printable) {
        s += "'" + std::string(n.chars, n.length) + (n.length == sizeof n.chars ? "...'" : "'");
    } else {
        for (uint8_t i = 0; i < n.length; ++i) s += std::format("{:02X}", static_cast<uint8_t>(n.chars[i]));
    }
    return s;
}
inline std::string describe_slot_files(const SlotFilesView& v) {
    std::string s = std::format("names {} [", v.name_list.size());
    for (size_t i = 0; i < v.name_list.size(); ++i) s += (i ? " " : "") + name_text(v.name_list[i]);
    s += std::format("] (lobby {} save {} other {}) | map {} [", v.names.lobby, v.names.save, v.names.other,
                     v.map_list.size());
    for (size_t i = 0; i < v.map_list.size(); ++i) {
        const MapEntryRead& e = v.map_list[i];
        s += std::format("{}{} hash 0x{:08X} len {} -> 0x{:X}", i ? " " : "", name_text(e.name), e.hash,
                         e.key_length == 0xFFFFFFFFu ? std::string("?") : std::to_string(e.key_length), e.value);
    }
    s += std::format("] (lobby {} save {} other {})", v.mapped.lobby, v.mapped.save, v.mapped.other);
    if (!v.ok) s += std::format(" | READ FAILED: {}", v.error);
    return s;
}

// CommitSlot (.xtls 0xEF51850) reads or writes nothing while slot+0x69C (in memory) or slot+0x699 (memory only)
// is set: a slot the guest loads from must have both 0.
inline bool slot_flags_disk(uint8_t memory_only_699, uint8_t in_memory_69c) { return !memory_only_699 && !in_memory_69c; }

// ---------------------------------------------------------------- whose actor, which caller

// Whose a load or a save is, from positive facts only (review of phase A, items 1 and 6). A server has the game's
// own session (ServerNet+0x18, the host's) and the mod's session 2 of each guest epoch, nothing else; a
// ServerUserActor points back to its session at +0x1F8 (server.cpp session_user).
enum class Owner : uint8_t { Host, User2, Unidentified };
inline const char* owner_name(Owner o) {
    switch (o) {
        case Owner::Host: return "the host";
        case Owner::User2: return "user 2";
        case Owner::Unidentified: return "UNIDENTIFIED (maybe user 2)";
    }
    return "?";
}

// What the game side read about the user (the actor itself when it is a user actor, else [character+0xA0]).
struct UserFacts {
    bool is_user2 = false;          // == session 2's validated user (server_user2)
    bool is_host = false;           // == the host's validated user (server_host_user)
    bool session_known = false;     // a ServerUserActor (vtable) whose +0x1F8 was read
    bool session_is_host = false;   // ... and it is ServerNet's session (the game's own)
    bool session_is_guest = false;  // ... and it is the mod's session 2 of some epoch
    bool character_is_user2 = false;  // a character whose key is user 2's character key of this epoch
    bool any_guest_session = false;   // a session 2 was built in this run (else no guest can exist)
};

// User 2 on any positive sign of it (first: fail closed); the host on a positive sign of it, or when no session 2
// was ever built (nobody else can be in); anything else (a freed or unreadable user while a guest may exist):
// Unidentified, which is treated as "maybe user 2".
inline Owner classify_owner(const UserFacts& f) {
    if (f.is_user2 || f.character_is_user2 || (f.session_known && f.session_is_guest)) return Owner::User2;
    if (f.is_host || (f.session_known && f.session_is_host)) return Owner::Host;
    if (!f.any_guest_session) return Owner::Host;
    return Owner::Unidentified;
}

// The three callers of LoadSlot 0x186F570 (all of them: xref scan of the exe), by return address.
inline constexpr uint64_t kRetUserLogin = 0x2C17886;      // UserLogin 2490 0x2C17520, call at 0x2C17881
inline constexpr uint64_t kRetLobby3093 = 0x2C3E1D3;      // 3093 0x2C3E030, the lobby reload, call at 0x2C3E1CE
inline constexpr uint64_t kRetCharacter3093 = 0x1042ABF8; // 3093, .xtls 0x1042AAF0 (SetupCharacter), call at 0x1042ABF3
enum class LoadCaller : uint8_t { UserLogin, Lobby3093, Character3093, Other };
inline LoadCaller load_caller(uint64_t return_rva) {
    if (return_rva == kRetUserLogin) return LoadCaller::UserLogin;
    if (return_rva == kRetLobby3093) return LoadCaller::Lobby3093;
    if (return_rva == kRetCharacter3093) return LoadCaller::Character3093;
    return LoadCaller::Other;
}
inline const char* load_caller_name(LoadCaller c) {
    switch (c) {
        case LoadCaller::UserLogin: return "UserLogin 2490";
        case LoadCaller::Lobby3093: return "3093 lobby";
        case LoadCaller::Character3093: return "3093 character";
        case LoadCaller::Other: return "OTHER";
    }
    return "?";
}

// The two callers of SavePlayerToSlot 0x186E450.
inline constexpr uint64_t kRetAutosave = 0xCBD86D3;   // .xtls 0xCBD84D0 (via 0x186E1A0, the 2077 timer), call at 0xCBD86CE
inline constexpr uint64_t kRetManual2910 = 0x103BE99F; // .xtls 0x103BE8E0 (SaveToFileCurrentPlayerReq 2910), call at 0x103BE99A
enum class SaveCaller : uint8_t { Autosave, Manual2910, Other };
inline SaveCaller save_caller(uint64_t return_rva) {
    if (return_rva == kRetAutosave) return SaveCaller::Autosave;
    if (return_rva == kRetManual2910) return SaveCaller::Manual2910;
    return SaveCaller::Other;
}
inline const char* save_caller_name(SaveCaller c) {
    switch (c) {
        case SaveCaller::Autosave: return "autosave";
        case SaveCaller::Manual2910: return "manual 2910";
        case SaveCaller::Other: return "OTHER";
    }
    return "?";
}

// The two callers of FlushPendingSaveData 0x1871930 (probe only).
inline constexpr uint64_t kRetFlushLogout = 0x28F273F;  // the character's logout phase 0x28F26C0, call at 0x28F273A
inline constexpr uint64_t kRetFlushTimer = 0x108393CD;  // .xtls 0x10839370, call at 0x108393C8
inline const char* flush_caller_name(uint64_t return_rva) {
    if (return_rva == kRetFlushLogout) return "logout phase";
    if (return_rva == kRetFlushTimer) return "flush timer";
    return "OTHER";
}

// ---------------------------------------------------------------- the guest slot's state

struct SlotState {
    bool staged = false;    // slot<N>.incoming accepted: moved in at user 2's next load of the slot
    bool live = false;      // moved into the sandbox: slot<N> holds the guest's files in this run
    bool disabled = false;  // a failure (creation, rename, thread, an exception) turned the mode off for this run
    bool poisoned = false;  // a load did not read the guest's bytes: no more loads or saves of user 2 in this run
    bool lobby_ok = false;  // verified in user 2's current login: the lobby read was the slot's
    bool save_ok = false;   // ... and the character's
};

// A load of the guest slot may go ahead.
inline bool slot_ready(const SlotState& s) { return (s.staged || s.live) && !s.disabled && !s.poisoned; }
// A save of user 2 may go to the guest slot: both files of this login verified.
inline bool slot_healthy(const SlotState& s) {
    return s.live && s.lobby_ok && s.save_ok && !s.disabled && !s.poisoned;
}

// Review item 4: the verification never outlives the load it was made for. At every LoadSlot entry of user 2, a
// lobby load (a new login, or the 3093's reload) drops both marks and a character load drops the character's; any
// refused or failed load (result != 0) drops both.
struct VerifyDrop {
    bool lobby = false, save = false;
};
inline VerifyDrop drop_on_load_entry(bool lobby_load) { return {lobby_load, true}; }
inline VerifyDrop drop_on_load_result(bool refused, int32_t result) {
    const bool drop = refused || result != 0;
    return {drop, drop};
}

// ---------------------------------------------------------------- LoadSlot 0x186F570

enum class Action : uint8_t { Pass, Remap, Refuse };
inline const char* action_name(Action a) {
    switch (a) {
        case Action::Pass: return "pass";
        case Action::Remap: return "REMAP";
        case Action::Refuse: return "REFUSE";
    }
    return "?";
}

struct Decision {
    Action action = Action::Pass;
    uint32_t slot = 0;       // the slot the original gets (Pass: the asked one)
    const char* why = "";
};

// The decision mode 1 makes; probe and off only pass (the probe logs this decision as "would").
// User 2 never gets the host's slot, on any path, its UserLogin included (review item 2: with nothing ready its
// login fails natively, the guest goes back to its title). The host's own loads are never refused (except a load
// of the guest's slot). Unidentified: refused, except at UserLogin, whose actor is the user actor being logged in
// (live, so its session is read: it is Unidentified only if that session is neither the game's nor any session 2,
// i.e. not user 2's either).
inline Decision decide_load_on(Owner owner, LoadCaller caller, const SlotState& s, uint32_t asked, uint32_t guest) {
    switch (owner) {
        case Owner::Host:
            if (asked == guest) return {Action::Refuse, asked, "the guest's slot is never loaded for another user"};
            return {Action::Pass, asked, "the host"};
        case Owner::Unidentified:
            if (asked == guest) return {Action::Refuse, asked, "the guest's slot is never loaded for another user"};
            if (caller != LoadCaller::UserLogin) {
                return {Action::Refuse, asked, "fail closed: this actor's user is not identified (maybe user 2)"};
            }
            return {Action::Pass, asked, "UserLogin of a user actor on no known session: not user 2's"};
        case Owner::User2:
            break;
    }
    if (slot_ready(s)) return {Action::Remap, guest, "the guest's slot is ready"};
    if (s.poisoned) return {Action::Refuse, asked, "fail closed: the guest's save is poisoned in this run"};
    if (s.disabled) return {Action::Refuse, asked, "fail closed: own-save mode failed in this run"};
    return {Action::Refuse, asked, "fail closed: no guest slot ready (never the host's slot for user 2)"};
}

inline Decision decide_load(Mode mode, Owner owner, LoadCaller caller, const SlotState& s, uint32_t asked,
                            uint32_t guest) {
    if (mode != Mode::On) return {Action::Pass, asked, mode == Mode::Probe ? "probe" : "off"};
    return decide_load_on(owner, caller, s, asked, guest);
}

// ---------------------------------------------------------------- SavePlayerToSlot 0x186E450

// The host's own saves always pass (review item 6; only a save of the guest's slot is refused). User 2's goes to
// the guest slot only when both files of ITS current login were verified (`same_login`: the character's user is
// the one whose lobby was verified and is still session 2's user); otherwise nothing is written. Unidentified
// (maybe user 2, e.g. a queued autosave of its character after its logout): refused.
inline Decision decide_save_on(Owner owner, const SlotState& s, bool same_login, uint32_t asked, uint32_t guest) {
    switch (owner) {
        case Owner::Host:
            if (asked == guest) return {Action::Refuse, asked, "the guest's slot is never written for another user"};
            return {Action::Pass, asked, "the host"};
        case Owner::Unidentified:
            return {Action::Refuse, asked, "fail closed: this character's user is not identified (maybe user 2)"};
        case Owner::User2:
            break;
    }
    if (slot_healthy(s) && same_login) return {Action::Remap, guest, "user 2's own slot, both files verified"};
    if (s.poisoned) return {Action::Refuse, asked, "user 2's save is poisoned in this run: nothing is written"};
    if (s.disabled) return {Action::Refuse, asked, "own-save mode failed in this run: nothing is written"};
    if (!s.live) return {Action::Refuse, asked, "user 2 plays a clone (its own save is not loaded): nothing is written"};
    if (slot_healthy(s)) {
        return {Action::Refuse, asked, "not the login whose files were verified (or user 2 left): nothing is written"};
    }
    return {Action::Refuse, asked, "user 2's load is not verified (lobby and save): nothing is written"};
}

inline Decision decide_save(Mode mode, Owner owner, const SlotState& s, bool same_login, uint32_t asked,
                            uint32_t guest) {
    if (mode != Mode::On) return {Action::Pass, asked, mode == Mode::Probe ? "probe" : "off"};
    return decide_save_on(owner, s, same_login, asked, guest);
}

// ---------------------------------------------------------------- the mailbox

// %LOCALAPPDATA%\cdmp\guest_saves\<guest>\gen<n>\: one folder per exported save, the last kKeepGenerations kept.
inline constexpr size_t kKeepGenerations = 5;

// "gen12" -> 12. Not "gen", "gen0", "gen01", "gen12.partial" or anything else.
inline std::optional<uint32_t> parse_generation(std::wstring_view name) {
    if (name.size() < 4 || name.size() > 13 || name.substr(0, 3) != L"gen" || name[3] == L'0') return std::nullopt;
    uint64_t n = 0;
    for (size_t i = 3; i < name.size(); ++i) {
        if (name[i] < L'0' || name[i] > L'9') return std::nullopt;
        n = n * 10 + static_cast<uint64_t>(name[i] - L'0');
    }
    if (n == 0 || n > 0xFFFFFFFFull) return std::nullopt;
    return static_cast<uint32_t>(n);
}

inline uint32_t next_generation(std::span<const uint32_t> existing) {
    uint32_t top = 0;
    for (const uint32_t g : existing) top = std::max(top, g);
    return top == 0xFFFFFFFFu ? 0 : top + 1;  // 0: no number left (never in practice)
}

// Phase B review: the numbering never goes back below what the guest already has (its base_gen, and what it
// acknowledged), also after the mailbox folder was emptied: above both the folder's newest and `floor`.
inline uint32_t next_generation_above(std::span<const uint32_t> existing, uint32_t floor) {
    const uint32_t next = next_generation(existing);
    if (!next || floor == 0xFFFFFFFFu) return 0;
    return std::max(next, floor + 1);
}

// The generations beyond the newest `keep` (oldest first).
inline std::vector<uint32_t> generations_to_drop(std::span<const uint32_t> existing, size_t keep = kKeepGenerations) {
    std::vector<uint32_t> g(existing.begin(), existing.end());
    std::sort(g.begin(), g.end());
    g.erase(std::unique(g.begin(), g.end()), g.end());
    if (g.size() <= keep) return {};
    g.resize(g.size() - keep);
    return g;
}

// A folder name for a guest id: [0-9A-Za-z_-] kept, anything else '_', at most 64; empty -> "unknown".
inline std::string guest_folder_name(std::string_view id) {
    std::string s;
    for (const char c : id.substr(0, 64)) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
                        c == '-';
        s += ok ? c : '_';
    }
    return s.empty() ? std::string("unknown") : s;
}

struct ManifestFile {
    std::string name;     // "lobby.save"
    uint64_t size = 0;
    std::string sha256;   // 64 hex digits (lower case)
    HeaderInfo header;
};

struct Manifest {
    uint32_t generation = 0;
    std::string guest;
    std::string reason;   // "autosave", "manual 2910", ...
    uint32_t host_slot = 0;
    uint32_t asked_slot = 0;
    std::string time_utc; // ISO 8601
    std::vector<ManifestFile> files;
    // Phase B (guest_save_wire.h): the guest's own slot N the save belongs to (none: phase A's debug folder; slot 0
    // is a real slot, so it is written whenever set) and the time as Unix seconds (SAVE_RESULT's), written only when
    // set.
    std::optional<uint32_t> guest_slot;
    uint64_t time_unix = 0;
    // Session 25: the line (gsw::find_continuation) and the save the guest offered for this session, as 64 hex digits;
    // empty = none (phase A, or no upload yet).
    std::string lineage_root, lineage_base;
};

// manifest.txt: "key=value" lines, one "file=" line per file.
inline std::string manifest_text(const Manifest& m) {
    std::string s = "cdmp guest save (P8 session 25, phase A; build 1.0.0.2976)\n";
    s += "generation=" + std::to_string(m.generation) + "\n";
    s += "guest=" + m.guest + "\n";
    s += "reason=" + m.reason + "\n";
    s += "host_slot=" + std::to_string(m.host_slot) + "\n";
    s += "asked_slot=" + std::to_string(m.asked_slot) + "\n";
    s += "time_utc=" + m.time_utc + "\n";
    if (m.guest_slot) s += "guest_slot=" + std::to_string(*m.guest_slot) + "\n";
    if (m.time_unix) s += "time_unix=" + std::to_string(m.time_unix) + "\n";
    if (!m.lineage_root.empty()) s += "lineage_root=" + m.lineage_root + "\n";
    if (!m.lineage_base.empty()) s += "lineage_base=" + m.lineage_base + "\n";
    for (const ManifestFile& f : m.files) {
        s += "file=" + f.name + " size=" + std::to_string(f.size) + " sha256=" + f.sha256 +
             " version=" + std::to_string(f.header.version) + " raw=" + std::to_string(f.header.raw_size) +
             " data=" + std::to_string(f.header.data_size) + "\n";
    }
    return s;
}

inline std::string hex(std::span<const uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (const uint8_t b : bytes) {
        s += kDigits[b >> 4];
        s += kDigits[b & 15];
    }
    return s;
}

}  // namespace cdmp::session2::gsv
