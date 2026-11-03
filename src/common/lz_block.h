#pragma once
// A small LZ77 block codec for the Troc tunnel (docs/research/pendientes/
// p8-tunel-rendimiento.md). Pure logic, no dependencies, unit-tested.
//
// Why: entering a field, the host's server writes thousands of nearly equal
// packets for user 2 before its MoveFieldCompleteAck 2144 (P8 session 6B:
// 6363 x SyncSceneObjectAck 2665 of 615 B + 120 x 2005, 7.4 MB in all, in
// that native order). Over Steam at 1 MB/s they took ~7.4 s, and the 2144
// came out after them. Order must be kept (the server sends the field's
// objects first); what can shrink is the data: those packets differ in a few
// bytes each, so one batch of 64 KB compresses ~10x.
//
// Format (LZ4-like sequences, little endian):
//   token u8: high nibble = literal count, low nibble = match length - 4
//             (15 in either = more length bytes follow: each adds 0..255,
//              255 means another byte follows)
//   [literal length bytes] literals [u16 offset (1..65535) [match length bytes]]
// The last sequence has literals only: the input ends right after them.
// The decoder never writes past `raw_size` nor reads past the input, and
// fails on anything that does not decode to exactly `raw_size` bytes.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace cdmp::lz {

inline constexpr size_t kMinMatch = 4;
inline constexpr size_t kMaxOffset = 65535;

// Compresses `in` (any size). The result may be larger than the input for
// data without repeats (callers keep the raw form then).
std::vector<uint8_t> compress(std::span<const uint8_t> in);

// Decompresses `in` into `out` (replaced). False for anything malformed or
// that does not produce exactly `raw_size` bytes.
bool decompress(std::span<const uint8_t> in, size_t raw_size, std::vector<uint8_t>& out);

}  // namespace cdmp::lz
