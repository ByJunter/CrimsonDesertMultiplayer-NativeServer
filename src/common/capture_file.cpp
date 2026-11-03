#include "common/capture_file.h"

#include <cstring>

namespace cdmp::capture {
namespace {

uint16_t u16_at(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32_at(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void put16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void put32(std::vector<uint8_t>& b, uint32_t v) {
    put16(b, static_cast<uint16_t>(v));
    put16(b, static_cast<uint16_t>(v >> 16));
}

}  // namespace

bool parse_header(const uint8_t* data, size_t size, uint32_t& version, uint32_t& pe_timestamp) {
    if (size < kFileHeaderSize || std::memcmp(data, "CDPK", 4) != 0) return false;
    version = u32_at(data + 4);
    pe_timestamp = u32_at(data + 8);
    return version == 1 || version == 2;
}

std::vector<uint8_t> encode_header(uint32_t version, uint32_t pe_timestamp) {
    std::vector<uint8_t> b = {'C', 'D', 'P', 'K'};
    put32(b, version);
    put32(b, pe_timestamp);
    return b;
}

std::vector<uint8_t> encode_record(const Record& r) {
    std::vector<uint8_t> b;
    b.reserve(kRecordHeaderSize + r.data.size());
    put32(b, static_cast<uint32_t>(r.t_us));
    put32(b, static_cast<uint32_t>(r.t_us >> 32));
    b.push_back(static_cast<uint8_t>(r.dir));
    b.push_back(0);
    put16(b, r.size);
    put16(b, static_cast<uint16_t>(r.data.size()));
    put16(b, 0);
    put32(b, r.session);
    put32(b, r.thread);
    b.insert(b.end(), r.data.begin(), r.data.end());
    return b;
}

Record gap_record(uint64_t t_us, uint32_t lost) {
    Record r;
    r.t_us = t_us;
    r.dir = Dir::ServerToClient;
    r.size = 0;
    r.session = kGapSession;
    r.thread = lost;
    return r;
}

Reader::~Reader() {
    if (f_) std::fclose(f_);
}

bool Reader::open(const std::wstring& path) {
    if (f_) std::fclose(f_);
    f_ = nullptr;
    error_.clear();
    if (_wfopen_s(&f_, path.c_str(), L"rb") != 0 || !f_) {
        f_ = nullptr;
        error_ = "cannot open the file";
        return false;
    }
    uint8_t h[kFileHeaderSize];
    if (std::fread(h, 1, sizeof h, f_) != sizeof h || !parse_header(h, sizeof h, version_, pe_timestamp_)) {
        error_ = "not a CDPK capture (version 1 or 2)";
        std::fclose(f_);
        f_ = nullptr;
        return false;
    }
    return true;
}

std::optional<Record> Reader::next() {
    if (!f_) return std::nullopt;
    uint8_t h[kRecordHeaderSize];
    const size_t got = std::fread(h, 1, sizeof h, f_);
    if (got == 0) return std::nullopt;  // clean end
    if (got != sizeof h) {
        error_ = "truncated record header (capture still being written?)";
        return std::nullopt;
    }
    Record r;
    r.t_us = static_cast<uint64_t>(u32_at(h)) | (static_cast<uint64_t>(u32_at(h + 4)) << 32);
    if (h[8] != 1 && h[8] != 2) {
        error_ = "bad direction byte";
        return std::nullopt;
    }
    r.dir = static_cast<Dir>(h[8]);
    r.size = u16_at(h + 10);
    const uint16_t copied = u16_at(h + 12);
    r.session = u32_at(h + 16);
    r.thread = u32_at(h + 20);
    if (copied > r.size) {
        error_ = "record copies more bytes than the packet has";
        return std::nullopt;
    }
    r.data.resize(copied);
    if (copied && std::fread(r.data.data(), 1, copied, f_) != copied) {
        error_ = "truncated record data";
        return std::nullopt;
    }
    return r;
}

}  // namespace cdmp::capture
