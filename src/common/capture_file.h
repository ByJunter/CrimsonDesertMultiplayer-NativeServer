#pragma once
// Reader of the sniffer's packet captures (cdmp_packets_*.bin, written by
// src/mod/net/troc_sniffer.cpp):
//   header: "CDPK" | u32 version (1 or 2) | u32 PE timestamp of the game
//   records: u64 t_us | u8 dir | u8 pad | u16 size | u16 copied | u16 pad |
//            u32 session | u32 thread | data[copied]
// dir 1 = client -> server (Req), 2 = server -> client (Ack). Version 1 kept
// at most 1024 bytes of each packet; version 2 whole packets (<= 32 KB), so
// `copied < size` means truncated.
//
// Used by cdmp_peer's replay mode (option D) and by the tests; no game.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace cdmp::capture {

inline constexpr size_t kFileHeaderSize = 12;
inline constexpr size_t kRecordHeaderSize = 24;

enum class Dir : uint8_t { ClientToServer = 1, ServerToClient = 2 };

struct Record {
    uint64_t t_us = 0;
    Dir dir = Dir::ClientToServer;
    uint16_t size = 0;    // what the game reported
    uint32_t session = 0;
    uint32_t thread = 0;
    std::vector<uint8_t> data;  // `copied` bytes

    bool whole() const { return data.size() == size; }
    uint16_t opcode() const { return data.size() >= 2 ? static_cast<uint16_t>(data[0] | (data[1] << 8)) : 0; }
    bool is_gap() const;
};

// A gap (p8-impl-log-async.md §1.3): records the sniffer's disk queue could not keep because the disk was
// too slow. Written where they were lost, as an EMPTY server->client record (dir 2, size 0, copied 0), so
// every existing reader takes it: tools/scripts/analyze_capture.py skips records shorter than 5 bytes and
// cdmp_peer's replay skips S>C data that does not parse. session = kGapSession, thread = how many records
// were lost since the previous kept record.
inline constexpr uint32_t kGapSession = 0xFFFFFFFF;
Record gap_record(uint64_t t_us, uint32_t lost);
inline bool Record::is_gap() const { return session == kGapSession && size == 0 && data.empty(); }

// Streams records from a file (captures are hundreds of MB).
class Reader {
public:
    Reader() = default;
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    // False if the file cannot be opened or is not a CDPK capture.
    bool open(const std::wstring& path);
    // The next record, or nullopt at the end or at a broken record (see error()).
    std::optional<Record> next();

    uint32_t version() const { return version_; }
    uint32_t pe_timestamp() const { return pe_timestamp_; }
    const std::string& error() const { return error_; }

private:
    FILE* f_ = nullptr;
    uint32_t version_ = 0;
    uint32_t pe_timestamp_ = 0;
    std::string error_;
};

// The same format from memory (tests).
bool parse_header(const uint8_t* data, size_t size, uint32_t& version, uint32_t& pe_timestamp);
std::vector<uint8_t> encode_header(uint32_t version, uint32_t pe_timestamp);
std::vector<uint8_t> encode_record(const Record& r);

}  // namespace cdmp::capture
