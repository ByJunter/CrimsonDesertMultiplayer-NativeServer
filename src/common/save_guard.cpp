#include "common/save_guard.h"

#include <format>
#include <span>

namespace cdmp::saveguard {
namespace {

// Win32 access and creation constants (no <windows.h> in common code).
constexpr uint32_t kGenericAll = 0x10000000;
constexpr uint32_t kGenericWrite = 0x40000000;
constexpr uint32_t kMaximumAllowed = 0x02000000;
constexpr uint32_t kDelete = 0x00010000;
constexpr uint32_t kWriteDac = 0x00040000;
constexpr uint32_t kWriteOwner = 0x00080000;
constexpr uint32_t kFileWriteData = 0x0002;
constexpr uint32_t kFileAppendData = 0x0004;
constexpr uint32_t kFileWriteEa = 0x0010;
constexpr uint32_t kFileWriteAttributes = 0x0100;
constexpr uint32_t kWriteMask = kGenericAll | kGenericWrite | kMaximumAllowed | kDelete | kWriteDac |
                                kWriteOwner | kFileWriteData | kFileAppendData | kFileWriteEa |
                                kFileWriteAttributes;
constexpr uint32_t kCreateNew = 1, kCreateAlways = 2, kOpenAlways = 4, kTruncateExisting = 5;
constexpr uint32_t kFlagDeleteOnClose = 0x04000000;

wchar_t fold(wchar_t c) {
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + 32);
    // Latin-1 capitals (except the multiplication sign).
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return static_cast<wchar_t>(c + 32);
    return c;
}

struct BitName {
    uint32_t bit;
    const char* name;
};

constexpr BitName kAccessBits[] = {
    {0x80000000, "GENERIC_READ"},     {0x40000000, "GENERIC_WRITE"},     {0x20000000, "GENERIC_EXECUTE"},
    {0x10000000, "GENERIC_ALL"},      {0x02000000, "MAXIMUM_ALLOWED"},   {0x00100000, "SYNCHRONIZE"},
    {0x00080000, "WRITE_OWNER"},      {0x00040000, "WRITE_DAC"},         {0x00020000, "READ_CONTROL"},
    {0x00010000, "DELETE"},           {0x0100, "FILE_WRITE_ATTRIBUTES"}, {0x0080, "FILE_READ_ATTRIBUTES"},
    {0x0040, "FILE_DELETE_CHILD"},    {0x0020, "FILE_EXECUTE"},          {0x0010, "FILE_WRITE_EA"},
    {0x0008, "FILE_READ_EA"},         {0x0004, "FILE_APPEND_DATA"},      {0x0002, "FILE_WRITE_DATA"},
    {0x0001, "FILE_READ_DATA"},
};
constexpr BitName kShareBits[] = {
    {0x1, "FILE_SHARE_READ"}, {0x2, "FILE_SHARE_WRITE"}, {0x4, "FILE_SHARE_DELETE"},
};
// 0x00100000 is both FILE_FLAG_OPEN_NO_RECALL and SECURITY_SQOS_PRESENT: left as a number.
constexpr BitName kFileFlagBits[] = {
    {0x80000000, "FILE_FLAG_WRITE_THROUGH"},     {0x40000000, "FILE_FLAG_OVERLAPPED"},
    {0x20000000, "FILE_FLAG_NO_BUFFERING"},      {0x10000000, "FILE_FLAG_RANDOM_ACCESS"},
    {0x08000000, "FILE_FLAG_SEQUENTIAL_SCAN"},   {0x04000000, "FILE_FLAG_DELETE_ON_CLOSE"},
    {0x02000000, "FILE_FLAG_BACKUP_SEMANTICS"},  {0x01000000, "FILE_FLAG_POSIX_SEMANTICS"},
    {0x00200000, "FILE_FLAG_OPEN_REPARSE_POINT"}, {0x0100, "FILE_ATTRIBUTE_TEMPORARY"},
    {0x0080, "FILE_ATTRIBUTE_NORMAL"},           {0x0020, "FILE_ATTRIBUTE_ARCHIVE"},
    {0x0010, "FILE_ATTRIBUTE_DIRECTORY"},        {0x0004, "FILE_ATTRIBUTE_SYSTEM"},
    {0x0002, "FILE_ATTRIBUTE_HIDDEN"},           {0x0001, "FILE_ATTRIBUTE_READONLY"},
};
constexpr BitName kMoveBits[] = {
    {0x01, "MOVEFILE_REPLACE_EXISTING"}, {0x02, "MOVEFILE_COPY_ALLOWED"},
    {0x04, "MOVEFILE_DELAY_UNTIL_REBOOT"}, {0x08, "MOVEFILE_WRITE_THROUGH"},
    {0x10, "MOVEFILE_CREATE_HARDLINK"},   {0x20, "MOVEFILE_FAIL_IF_NOT_TRACKABLE"},
};

std::string bits_text(uint32_t value, std::span<const BitName> names) {
    std::string out = std::format("0x{:X}", value);
    if (value == 0) return out;
    std::string parts;
    uint32_t rest = value;
    for (const BitName& n : names) {
        if (!(rest & n.bit)) continue;
        parts += parts.empty() ? "" : "|";
        parts += n.name;
        rest &= ~n.bit;
    }
    if (rest) parts += std::format("{}+0x{:X}", parts.empty() ? "" : "|", rest);
    return out + " (" + parts + ")";
}

// Length of a "\\?\" or "\\.\" prefix (either separator), else 0.
size_t device_prefix(std::wstring_view path) {
    if (path.size() < 4) return 0;
    const std::wstring_view head = path.substr(0, 4);
    return head == L"\\\\?\\" || head == L"\\\\.\\" || head == L"//?/" || head == L"//./" ? 4 : 0;
}

}  // namespace

const char* mode_name(Mode mode) {
    switch (mode) {
        case Mode::Off: return "off";
        case Mode::Block: return "block";
        case Mode::Trace: return "trace";
        case Mode::Redirect: return "redirect";
    }
    return "?";
}

std::optional<Mode> parse_mode(std::string_view text) {
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    // The first word only (an inline "; comment" or anything after a space is not part of it).
    size_t end = 0;
    while (end < text.size() && !blank(text[end]) && text[end] != ';') ++end;
    const std::string_view word = text.substr(0, end);
    if (word.size() == 5 || word.size() == 8) {
        std::string lower(word);
        for (char& c : lower) c = (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
        if (lower == "trace") return Mode::Trace;
        if (lower == "redirect") return Mode::Redirect;
    }
    if (text.empty() || text.front() < '0' || text.front() > '9') return std::nullopt;
    for (const char c : text) {
        if (c < '0' || c > '9') break;
        if (c != '0') return Mode::Block;
    }
    return Mode::Off;
}

std::string access_text(uint32_t desired_access) { return bits_text(desired_access, kAccessBits); }
std::string share_text(uint32_t share_mode) { return bits_text(share_mode, kShareBits); }
std::string file_flags_text(uint32_t flags_and_attrs) { return bits_text(flags_and_attrs, kFileFlagBits); }
std::string move_flags_text(uint32_t flags) { return bits_text(flags, kMoveBits); }

std::string disposition_text(uint32_t creation_disposition) {
    static constexpr const char* kNames[] = {"CREATE_NEW", "CREATE_ALWAYS", "OPEN_EXISTING", "OPEN_ALWAYS",
                                             "TRUNCATE_EXISTING"};
    if (creation_disposition >= 1 && creation_disposition <= 5) {
        return std::format("{} ({})", creation_disposition, kNames[creation_disposition - 1]);
    }
    return std::to_string(creation_disposition);
}

TraceBudget::Line TraceBudget::take(bool write) {
    Line line;
    if (!write) {
        const uint64_t n = non_writes_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n > non_write_cap_) {
            skipped_.fetch_add(1, std::memory_order_relaxed);
            return {};
        }
        line.last_non_write = n == non_write_cap_;
    }
    const uint64_t n = lines_.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > total_) {
        skipped_.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    line.seq = n;
    line.last = n == total_;
    return line;
}

uint64_t TraceBudget::logged() const {
    const uint64_t n = lines_.load(std::memory_order_relaxed);
    return n < total_ ? n : total_;
}

std::wstring normalize(std::wstring_view path) {
    std::wstring out;
    out.reserve(path.size());
    path.remove_prefix(device_prefix(path));
    for (const wchar_t raw : path) {
        const wchar_t c = raw == L'/' ? L'\\' : fold(raw);
        if (c == L'\\' && !out.empty() && out.back() == L'\\') continue;
        out.push_back(c);
    }
    while (out.size() > 1 && out.back() == L'\\') out.pop_back();
    return out;
}

bool is_under(std::wstring_view path, std::wstring_view dir) {
    const std::wstring p = normalize(path);
    const std::wstring d = normalize(dir);
    if (d.empty() || p.size() < d.size() || p.compare(0, d.size(), d) != 0) return false;
    return p.size() == d.size() || p[d.size()] == L'\\';
}

Redirector::Redirector(std::wstring_view save_dir, std::wstring_view sandbox_dir) : dir_(normalize(save_dir)) {
    sandbox_dir.remove_prefix(device_prefix(sandbox_dir));
    sandbox_.assign(sandbox_dir);
    for (wchar_t& c : sandbox_) c = c == L'/' ? L'\\' : c;
    while (sandbox_.size() > 1 && sandbox_.back() == L'\\') sandbox_.pop_back();
    valid_ = !dir_.empty() && !sandbox_.empty() && !is_under(sandbox_, dir_) && !is_under(dir_, sandbox_);
}

// normalize(full_path) compared with dir_ on the fly, without building it.
size_t Redirector::match(std::wstring_view p) const {
    if (!valid_) return kNoMatch;
    size_t i = device_prefix(p);
    size_t j = 0;
    wchar_t prev = 0;
    while (j < dir_.size()) {
        if (i >= p.size()) return kNoMatch;
        const wchar_t c = p[i] == L'/' ? L'\\' : fold(p[i]);
        ++i;
        if (c == L'\\' && prev == L'\\') continue;  // repeated separator
        if (c != dir_[j]) return kNoMatch;
        prev = c;
        ++j;
    }
    // On a component boundary: "save" itself or "save\...", never "savegame".
    if (i < p.size() && p[i] != L'\\' && p[i] != L'/') return kNoMatch;
    return i;
}

std::optional<std::wstring> Redirector::map(std::wstring_view full_path) const {
    const size_t at = match(full_path);
    if (at == kNoMatch) return std::nullopt;
    const std::wstring_view prefix = full_path.substr(0, device_prefix(full_path));
    const std::wstring_view rest = full_path.substr(at);
    std::wstring out;
    out.reserve(prefix.size() + sandbox_.size() + rest.size());
    out.append(prefix).append(sandbox_).append(rest);
    return out;
}

std::optional<std::wstring> redirect_path(std::wstring_view full_path, std::wstring_view save_dir,
                                          std::wstring_view sandbox_dir) {
    return Redirector(save_dir, sandbox_dir).map(full_path);
}

bool create_may_write(uint32_t access, uint32_t disposition, uint32_t flags, bool exists) {
    if (access & kWriteMask) return true;
    if (disposition == kCreateNew || disposition == kCreateAlways || (disposition == kOpenAlways && !exists) ||
        disposition == kTruncateExisting) {
        return true;
    }
    return (flags & kFlagDeleteOnClose) != 0;
}

const char* op_name(Op op) {
    switch (op) {
        case Op::Create: return "create";
        case Op::Delete: return "delete";
        case Op::MoveFrom: return "move from";
        case Op::MoveTo: return "move to";
        case Op::Copy: return "copy to";
        case Op::Attributes: return "set attributes";
        case Op::RemoveDir: return "remove directory";
        case Op::CreateDir: return "create directory";
    }
    return "?";
}

bool Guard::block_create(std::wstring_view full_path, uint32_t access, uint32_t disposition, uint32_t flags,
                         bool exists) {
    if (!active() || !is_under(full_path, dir_)) return false;
    if (!create_may_write(access, disposition, flags, exists)) {
        reads_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    blocked_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool Guard::block_path(Op /*op*/, std::wstring_view full_path) {
    if (!active() || !is_under(full_path, dir_)) return false;
    blocked_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

}  // namespace cdmp::saveguard
