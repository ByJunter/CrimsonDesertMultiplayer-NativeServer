#pragma once
// Text for exception filters and crash paths without allocating (P8 review of
// the night of 10-10, I1/M9: docs/research/pendientes/p8-revision-noche.md).
// Pure: no Windows, no game memory; tests/optd_inject_tests.cpp checks it.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace cdmp::game {

// A line built on the stack: appends are cut at the capacity, never allocate.
class FixedLine {
public:
    FixedLine& str(std::string_view s) noexcept {
        for (const char c : s) {
            if (n_ + 1 >= sizeof buf_) break;
            buf_[n_++] = c;
        }
        return *this;
    }
    FixedLine& hex(uint64_t v) noexcept {  // 0x1A2B (upper case, no leading zeros)
        char d[16];
        int k = 0;
        do {
            d[k++] = "0123456789ABCDEF"[v & 0xF];
            v >>= 4;
        } while (v && k < 16);
        str("0x");
        while (k) {
            const char c[1] = {d[--k]};
            str(std::string_view(c, 1));
        }
        return *this;
    }
    FixedLine& hex32(uint32_t v) noexcept {  // always 8 hex digits: 0x80000001
        str("0x");
        for (int s = 28; s >= 0; s -= 4) {
            const char c[1] = {"0123456789ABCDEF"[(v >> s) & 0xF]};
            str(std::string_view(c, 1));
        }
        return *this;
    }
    FixedLine& dec(uint64_t v) noexcept {
        char d[20];
        int k = 0;
        do {
            d[k++] = static_cast<char>('0' + v % 10);
            v /= 10;
        } while (v && k < 20);
        while (k) {
            const char c[1] = {d[--k]};
            str(std::string_view(c, 1));
        }
        return *this;
    }
    std::string_view view() const noexcept { return {buf_, n_}; }
    size_t size() const noexcept { return n_; }

private:
    char buf_[768] = {};
    size_t n_ = 0;
};

// What an exception record says, as plain numbers (filled by the filter).
struct FaultFacts {
    uint32_t code = 0;
    uint64_t address = 0;
    uint64_t base = 0;        // the exe's base (0: unknown)
    uint64_t image_size = 0;  // addresses in [base, base + image_size) are printed as exe+RVA
    uint32_t nparams = 0;
    uint64_t p0 = 0, p1 = 0;  // an access violation: read (0) / write (1) / execute (8), and the address
    uint32_t thread = 0;
};

// "<who>: exception 0x... at exe+0x... (write of 0x...) on thread N while <where>"
inline void describe_fault(FixedLine& l, std::string_view who, std::string_view where, const FaultFacts& f) noexcept {
    l.str(who).str(": exception ").hex32(f.code).str(" at ");
    if (f.base && f.address >= f.base && f.address - f.base < f.image_size) {
        l.str("exe+").hex(f.address - f.base);
    } else {
        l.hex(f.address);
    }
    if ((f.code == 0xC0000005u || f.code == 0x80000001u) && f.nparams >= 2) {
        l.str(" (").str(f.p0 == 0 ? "read" : f.p0 == 1 ? "write" : f.p0 == 8 ? "execute" : "access").str(" of ");
        l.hex(f.p1).str(")");
    }
    l.str(" on thread ").dec(f.thread).str(" while ").str(where);
}

}  // namespace cdmp::game
