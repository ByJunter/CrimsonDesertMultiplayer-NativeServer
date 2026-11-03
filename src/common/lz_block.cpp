#include "common/lz_block.h"

#include <algorithm>
#include <cstring>

namespace cdmp::lz {
namespace {

constexpr int kHashBits = 14;

uint32_t read32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

uint32_t hash32(uint32_t v) { return (v * 2654435761u) >> (32 - kHashBits); }

// A length of 15 + `extra`: the bytes after the nibble.
void put_length(std::vector<uint8_t>& out, size_t extra) {
    while (extra >= 255) {
        out.push_back(255);
        extra -= 255;
    }
    out.push_back(static_cast<uint8_t>(extra));
}

// One sequence: literals, then (match_len > 0) a match `offset` bytes back.
void emit(std::vector<uint8_t>& out, const uint8_t* lit, size_t lit_len, size_t offset, size_t match_len) {
    const size_t ml = match_len ? match_len - kMinMatch : 0;
    out.push_back(static_cast<uint8_t>((std::min<size_t>(lit_len, 15) << 4) | std::min<size_t>(ml, 15)));
    if (lit_len >= 15) put_length(out, lit_len - 15);
    out.insert(out.end(), lit, lit + lit_len);
    if (!match_len) return;
    out.push_back(static_cast<uint8_t>(offset));
    out.push_back(static_cast<uint8_t>(offset >> 8));
    if (ml >= 15) put_length(out, ml - 15);
}

// The bytes after a nibble of 15, added to `len`; false past `cap` or the input.
bool read_length(const uint8_t*& ip, const uint8_t* end, size_t& len, size_t cap) {
    for (;;) {
        if (ip >= end) return false;
        const uint8_t b = *ip++;
        len += b;
        if (len > cap) return false;
        if (b != 255) return true;
    }
}

}  // namespace

std::vector<uint8_t> compress(std::span<const uint8_t> in) {
    std::vector<uint8_t> out;
    const size_t n = in.size();
    const uint8_t* p = in.data();
    out.reserve(n + n / 255 + 16);
    if (n <= kMinMatch) {
        emit(out, p, n, 0, 0);
        return out;
    }
    // Positions by the hash of their 4 bytes (-1 = none). Per thread, reused.
    thread_local std::vector<int32_t> table;
    table.assign(size_t{1} << kHashBits, -1);
    size_t anchor = 0, i = 0;
    const size_t last = n - kMinMatch;  // the last position with 4 bytes to read
    while (i <= last) {
        const uint32_t v = read32(p + i);
        const uint32_t h = hash32(v);
        const int32_t cand = table[h];
        table[h] = static_cast<int32_t>(i);
        if (cand >= 0 && i - static_cast<size_t>(cand) <= kMaxOffset && read32(p + cand) == v) {
            size_t c = static_cast<size_t>(cand);
            size_t len = kMinMatch;
            while (i + len < n && p[c + len] == p[i + len]) ++len;
            while (i > anchor && c > 0 && p[i - 1] == p[c - 1]) {  // the literals before may match too
                --i;
                --c;
                ++len;
            }
            emit(out, p + anchor, i - anchor, i - c, len);
            i += len;
            anchor = i;
            if (i - 2 <= last) table[hash32(read32(p + i - 2))] = static_cast<int32_t>(i - 2);
            continue;
        }
        i += 1 + ((i - anchor) >> 6);  // data without repeats is skipped faster
    }
    emit(out, p + anchor, n - anchor, 0, 0);
    return out;
}

bool decompress(std::span<const uint8_t> in, size_t raw_size, std::vector<uint8_t>& out) {
    out.assign(raw_size, 0);
    const uint8_t* ip = in.data();
    const uint8_t* const end = ip + in.size();
    size_t op = 0;
    for (;;) {
        if (ip >= end) return false;
        const uint8_t token = *ip++;
        size_t lit = token >> 4;
        if (lit == 15 && !read_length(ip, end, lit, raw_size)) return false;
        if (lit > static_cast<size_t>(end - ip) || lit > raw_size - op) return false;
        if (lit) std::memcpy(out.data() + op, ip, lit);
        ip += lit;
        op += lit;
        if (ip == end) return (token & 15) == 0 && op == raw_size;  // the last sequence: literals only
        if (end - ip < 2) return false;
        const size_t offset = static_cast<size_t>(ip[0]) | (static_cast<size_t>(ip[1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) return false;
        size_t ml = token & 15;
        if (ml == 15 && !read_length(ip, end, ml, raw_size)) return false;
        ml += kMinMatch;
        if (ml > raw_size - op) return false;
        uint8_t* dst = out.data() + op;
        const uint8_t* src = dst - offset;
        for (size_t k = 0; k < ml; ++k) dst[k] = src[k];  // byte by byte: a match may overlap itself
        op += ml;
    }
}

}  // namespace cdmp::lz
