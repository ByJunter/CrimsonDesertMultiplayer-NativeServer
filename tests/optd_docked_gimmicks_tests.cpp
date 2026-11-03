// P8 session 28, the gimmicks docked to a player character (docs/research/s25/gimmicks-dueno.md section 6): the pure
// logic of docked_gimmicks (src/mod/session2/docked_gimmicks_logic.h) on S27's real 2005 records (their first bytes:
// everything the logic reads), the withheld requests, the probe tables and the verified build-2976 bytes.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <span>
#include <vector>

#include "mod/session2/docked_gimmicks_logic.h"

using namespace cdmp::session2;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// 0x90100376: S27 guest step 2 (cdmp_packets_20261012-213205) t 64.378, record length 9, first 9 bytes
const uint8_t kUser90100376[9] = {
    0x00, 0x76, 0x03, 0x10, 0x90, 0x00, 0x00, 0x00, 0x00,
};
// 0xA0100377: S27 guest step 2 (cdmp_packets_20261012-213205) t 64.378, record length 14744, first 64 bytes
const uint8_t kOwnChar[64] = {
    0x01, 0x77, 0x03, 0x10, 0xa0, 0x76, 0x03, 0x10, 0x90, 0x00, 0x02, 0xf0, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x7f, 0x76, 0x03, 0x10, 0x90, 0x01, 0x00, 0x00, 0x00, 0x40, 0x42, 0x0f, 0x00, 0x05, 0x00,
    0x00, 0x00, 0x4b, 0x00, 0x6c, 0x00, 0x69, 0x00, 0x66, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x02, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x01, 0x00,
};
// 0xB010006E: S27 guest step 2 (cdmp_packets_20261012-213205) t 64.378, record length 6412, first 272 bytes
const uint8_t kNpcDocked[272] = {
    0x07, 0x6e, 0x00, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x10, 0x90, 0x71, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00,
    0x00, 0x00, 0x47, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x20, 0x00, 0x4d, 0x00, 0x65, 0x00,
    0x63, 0x00, 0x68, 0x00, 0x61, 0x00, 0x6e, 0x00, 0x69, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x00, 0x10, 0xb0, 0x00, 0x00, 0x40, 0x9b,
    0xe8, 0x9f, 0x00, 0x00, 0x00, 0x00, 0xe6, 0x94, 0x39, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x6d, 0x00, 0x10, 0xb0, 0x6c, 0x42, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6d, 0x00,
};
// 0xB0100156: S27 guest step 2 (cdmp_packets_20261012-213205) t 64.399, record length 6426, first 272 bytes
const uint8_t kHostDevice[272] = {
    0x07, 0x56, 0x01, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x10, 0x90, 0x71, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00,
    0x00, 0x00, 0x47, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x20, 0x00, 0x4d, 0x00, 0x65, 0x00,
    0x63, 0x00, 0x68, 0x00, 0x61, 0x00, 0x6e, 0x00, 0x69, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0xa0, 0x00, 0x00, 0x40, 0x9b,
    0xe8, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x24, 0xe3, 0x48, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x10, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// 0xB01004A1: S27 guest step 2 (cdmp_packets_20261012-213205) t 65.403, record length 6426, first 272 bytes
const uint8_t kGuestDevice[272] = {
    0x07, 0xa1, 0x04, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x10, 0x90, 0x71, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00,
    0x00, 0x00, 0x47, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x20, 0x00, 0x4d, 0x00, 0x65, 0x00,
    0x63, 0x00, 0x68, 0x00, 0x61, 0x00, 0x6e, 0x00, 0x69, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x03, 0x10, 0xa0, 0x00, 0x00, 0x40, 0x9b,
    0xe8, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x06, 0x8f, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x77, 0x03, 0x10, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77, 0x03,
};
// 0xB010006C: S27 guest step 2 (cdmp_packets_20261012-213205) t 64.378, record length 6176, name Game Mechanic, first 0x110 bytes
const uint8_t kWorldGimmick[272] = {
    0x07, 0x6c, 0x00, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x10, 0x90, 0x71, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00,
    0x00, 0x00, 0x47, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x20, 0x00, 0x4d, 0x00, 0x65, 0x00,
    0x63, 0x00, 0x68, 0x00, 0x61, 0x00, 0x6e, 0x00, 0x69, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x9b,
    0xe8, 0x9f, 0x00, 0x00, 0x00, 0x00, 0x46, 0x7f, 0x51, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// P8 S29 (docked_gimmicks_other): S28's records as the guest received them.
// 0xA01002D2: S28 guest step 2 (cdmp_packets_20261013-050005) t 18.649, record length 14881, first 64 bytes
const uint8_t kS28OwnChar[64] = {
    0x01, 0xd2, 0x02, 0x10, 0xa0, 0xd1, 0x02, 0x10, 0x90, 0x00, 0x02, 0xf0, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x7f, 0xd1, 0x02, 0x10, 0x90, 0x01, 0x00, 0x00, 0x00, 0x40, 0x42, 0x0f, 0x00, 0x05, 0x00,
    0x00, 0x00, 0x4b, 0x00, 0x6c, 0x00, 0x69, 0x00, 0x66, 0x00, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0x02, 0xf0, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x01, 0x00,
};
// The host's grappling device in its world, docked to 0xA0100001, controller 0x90100000 (unchanged in S28).
// 0xB010014E: S28 guest step 2 (cdmp_packets_20261013-050005) t 49.780, record length 6436, first 272 bytes
const uint8_t kS28HostDevice[272] = {
    0x07, 0x4e, 0x01, 0x10, 0xb0, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x00, 0x10, 0x90, 0x71, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x00,
    0x00, 0x00, 0x47, 0x00, 0x61, 0x00, 0x6d, 0x00, 0x65, 0x00, 0x20, 0x00, 0x4d, 0x00, 0x65, 0x00,
    0x63, 0x00, 0x68, 0x00, 0x61, 0x00, 0x6e, 0x00, 0x69, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0xa0, 0x00, 0x00, 0x40, 0x9b,
    0xe8, 0x9f, 0x00, 0x00, 0x00, 0x00, 0xa7, 0xb3, 0x69, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x10, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// A 2005 payload of the given records (each [u16 length][record]).
std::vector<uint8_t> payload(std::initializer_list<std::span<const uint8_t>> recs) {
    std::vector<uint8_t> p;
    for (const auto& r : recs) {
        p.push_back(static_cast<uint8_t>(r.size() & 0xFF));
        p.push_back(static_cast<uint8_t>(r.size() >> 8));
        p.insert(p.end(), r.begin(), r.end());
    }
    return p;
}
// Header + payload.
std::vector<uint8_t> packet(uint16_t op, const std::vector<uint8_t>& pl) {
    std::vector<uint8_t> p = {static_cast<uint8_t>(op & 0xFF), static_cast<uint8_t>(op >> 8), 0xFF,
                              static_cast<uint8_t>(pl.size() & 0xFF), static_cast<uint8_t>(pl.size() >> 8)};
    p.insert(p.end(), pl.begin(), pl.end());
    return p;
}
// The controller (+0x12) of the record whose key is `key` in a payload; 0 when absent.
uint32_t controller_of(std::span<const uint8_t> pl, uint32_t key) {
    uint32_t c = 0;
    (void)dgl::for_each_record(pl, [&](size_t, std::span<const uint8_t> rec) {
        if (rec.size() >= dgl::kRecController + 4 && dgl::le32(&rec[dgl::kRecKey]) == key) {
            c = dgl::le32(&rec[dgl::kRecController]);
        }
    });
    return c;
}
std::vector<uint8_t> s27_payload() {
    return payload({kUser90100376, kOwnChar, kNpcDocked, kWorldGimmick, kHostDevice, kGuestDevice});
}

void test_records() {
    CHECK(dgl::docked_parent(kGuestDevice) == 0xA0100377u);
    CHECK(dgl::docked_parent(kHostDevice) == 0xA0100001u);
    CHECK(dgl::docked_parent(kNpcDocked) == 0xB010006Du);
    CHECK(dgl::docked_parent(kWorldGimmick) == 0);
    CHECK(dgl::docked_parent(kOwnChar) == 0);  // a player record, not a gimmick
    // "Game Mechanic" (13 characters): the two copies at +0xE8 and +0x102.
    CHECK(dgl::le32(&kGuestDevice[dgl::kRecNameLen]) == 13);
    CHECK(dgl::le32(&kGuestDevice[0xE8]) == 0xA0100377u && dgl::le32(&kGuestDevice[0x102]) == 0xA0100377u);
    CHECK(dgl::le32(&kGuestDevice[dgl::kRecController]) == 0x90100000u);
    CHECK(dgl::le32(&kOwnChar[dgl::kRecController]) == 0x90100376u);
    // The copies must agree, and the record must be long enough.
    std::vector<uint8_t> bad(std::begin(kGuestDevice), std::end(kGuestDevice));
    dgl::put32(&bad[0x102], 0xA0100001u);
    CHECK(dgl::docked_parent(bad) == 0);
    CHECK(dgl::docked_parent(std::span<const uint8_t>(kGuestDevice, 0x105)) == 0);
    std::vector<uint8_t> long_name(std::begin(kGuestDevice), std::end(kGuestDevice));
    dgl::put32(&long_name[dgl::kRecNameLen], 64);
    CHECK(dgl::docked_parent(long_name) == 0);
}

void test_guest_on() {
    auto pl = s27_payload();
    dgl::Self self{0xA0100377u, 0};
    dgl::LocalKeys keys;
    const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys);
    CHECK(!r.malformed);
    CHECK(r.records == 6);
    CHECK(r.gimmicks == 4);
    CHECK(r.learned_user == 0x90100376u && self.own_user == 0x90100376u);
    CHECK(r.rewritten == 1 && r.would_rewrite == 0);
    CHECK(r.other_player == 1);  // the host's device: never taken
    CHECK(r.docked_world == 1 && r.not_docked == 1 && r.docked_player == 2);
    CHECK(controller_of(pl, 0xB01004A1u) == 0x90100376u);  // rewritten in place
    CHECK(controller_of(pl, 0xB0100156u) == 0x90100000u);  // the host's device unchanged
    CHECK(controller_of(pl, 0xB010006Eu) == 0x90100000u);  // the NPC's unchanged
    CHECK(controller_of(pl, 0xB010006Cu) == 0x90100000u);  // the world gimmick unchanged
    CHECK(pl.size() == s27_payload().size());
    CHECK(keys.contains(0xB01004A1u) && !keys.contains(0xB0100156u) && keys.used() == 1);
    CHECK(r.docked_n == 2);
    bool saw_rewrite = false, saw_other = false;
    for (size_t i = 0; i < r.docked_n; ++i) {
        if (r.docked[i].key == 0xB01004A1u) {
            saw_rewrite = r.docked[i].action == dgl::Action::Rewritten && r.docked[i].written == 0x90100376u &&
                          r.docked[i].controller == 0x90100000u;
        }
        if (r.docked[i].key == 0xB0100156u) saw_other = r.docked[i].action == dgl::Action::OtherPlayer;
    }
    CHECK(saw_rewrite && saw_other);
    // Everything else byte for byte the same.
    const auto orig = s27_payload();
    size_t diff = 0;
    for (size_t i = 0; i < pl.size(); ++i) diff += pl[i] != orig[i];
    CHECK(diff >= 1 && diff <= 4);
    // A second delivery of the same record: already this client's.
    auto again = payload({kGuestDevice});
    dgl::put32(again.data() + 2 + dgl::kRecController, 0x90100376u);
    const auto r2 = dgl::process_2005(again, self, dgl::Mode::On, keys);
    CHECK(r2.already_mine == 1 && r2.rewritten == 0);
}

void test_guest_probe_off() {
    auto pl = s27_payload();
    const auto orig = pl;
    dgl::Self self{0xA0100377u, 0};
    dgl::LocalKeys keys;
    const auto r = dgl::process_2005(pl, self, dgl::Mode::Probe, keys);
    CHECK(r.would_rewrite == 1 && r.rewritten == 0);
    CHECK(pl == orig);
    CHECK(keys.contains(0xB01004A1u));  // so the send path counts "would withhold"
    dgl::Self s2{0xA0100377u, 0};
    dgl::LocalKeys k2;
    const auto off = dgl::process_2005(pl, s2, dgl::Mode::Off, k2);
    CHECK(off.records == 0 && k2.used() == 0 && s2.own_user == 0);
}

void test_unknowns() {
    // The own character not known yet: both player-docked records unchanged.
    auto pl = s27_payload();
    const auto orig = pl;
    dgl::Self none{};
    dgl::LocalKeys keys;
    const auto r = dgl::process_2005(pl, none, dgl::Mode::On, keys);
    CHECK(r.own_key_unknown == 2 && r.rewritten == 0 && pl == orig && keys.used() == 0);
    // The own character known, its user not (no record of it in this packet): unchanged.
    auto only = payload({kGuestDevice});
    const auto only_orig = only;
    dgl::Self self{0xA0100377u, 0};
    const auto r2 = dgl::process_2005(only, self, dgl::Mode::On, keys);
    CHECK(r2.own_user_unknown == 1 && r2.rewritten == 0 && only == only_orig);
    // The host's view: its own device is already its own, the guest's is never taken.
    auto host = payload({kHostDevice, kGuestDevice});
    const auto host_orig = host;
    dgl::Self h{0xA0100001u, 0x90100000u};
    const auto r3 = dgl::process_2005(host, h, dgl::Mode::On, keys);
    CHECK(r3.already_mine == 1 && r3.other_player == 1 && r3.rewritten == 0 && host == host_orig);
}

void test_malformed() {
    auto pl = s27_payload();
    pl[0] = 0xFF;  // the first record claims more than there is
    pl[1] = 0xFF;
    dgl::Self self{0xA0100377u, 0x90100376u};
    dgl::LocalKeys keys;
    const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys);
    CHECK(r.malformed && r.records == 0 && r.rewritten == 0);
    // A good record then a bad length: classified, but nothing is written in a malformed packet (it passes whole).
    auto two = payload({kGuestDevice});
    two.push_back(0x40);
    two.push_back(0x00);
    two.push_back(0x07);
    const auto two_orig = two;
    const auto r2 = dgl::process_2005(two, self, dgl::Mode::On, keys);
    CHECK(r2.malformed && r2.rewritten == 0 && r2.malformed_not_written == 1 && two == two_orig);
    CHECK(!keys.contains(0xB01004A1u));
    // Probe: the same "would" decision as for a good packet.
    dgl::LocalKeys pk;
    const auto r3 = dgl::process_2005(two, self, dgl::Mode::Probe, pk);
    CHECK(r3.would_rewrite == 1 && two == two_orig);
}

void test_filters() {
    dgl::Self self{0xA0100377u, 0x90100376u};
    dgl::LocalKeys keys;
    // Not a "Game Mechanic": docked to the own character, a user controller, but not rewritten.
    std::vector<uint8_t> other_name(std::begin(kGuestDevice), std::end(kGuestDevice));
    other_name[0x22] = 'X';  // "Xame Mechanic"
    auto pl = payload({other_name});
    const auto orig = pl;
    const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys);
    CHECK(r.not_game_mechanic == 1 && r.rewritten == 0 && pl == orig && keys.used() == 0);
    CHECK(r.docked_n == 1 && r.docked[0].action == dgl::Action::NotGameMechanic);
    CHECK(std::strcmp(r.docked[0].name, "Xame Mechanic") == 0);
    // A controller that is not a user key (0): unchanged.
    std::vector<uint8_t> no_user(std::begin(kGuestDevice), std::end(kGuestDevice));
    dgl::put32(&no_user[dgl::kRecController], 0);
    auto pl2 = payload({no_user});
    const auto orig2 = pl2;
    const auto r2 = dgl::process_2005(pl2, self, dgl::Mode::On, keys);
    CHECK(r2.not_user_controller == 1 && r2.rewritten == 0 && pl2 == orig2);
    // The real record: rewritten, its name logged.
    auto pl3 = payload({kGuestDevice});
    const auto r3 = dgl::process_2005(pl3, self, dgl::Mode::On, keys);
    CHECK(r3.rewritten == 1 && std::strcmp(r3.docked[0].name, "Game Mechanic") == 0);
    CHECK(dgl::is_game_mechanic(kHostDevice) && !dgl::is_game_mechanic(kOwnChar) && !dgl::is_game_mechanic(other_name));
    char nm[dgl::kNameChars] = {};
    CHECK(dgl::record_name(kOwnChar, nm, sizeof nm) && std::strcmp(nm, "Kliff") == 0);
}

void test_retire() {
    dgl::Self self{0xA0100377u, 0x90100376u};
    dgl::LocalKeys keys;
    auto pl = payload({kGuestDevice});
    (void)dgl::process_2005(pl, self, dgl::Mode::On, keys);
    CHECK(keys.contains(0xB01004A1u));
    // The same key announced docked to another character: no longer local-only.
    std::vector<uint8_t> moved(std::begin(kGuestDevice), std::end(kGuestDevice));
    dgl::put32(&moved[0xE8], 0xA0100001u);
    dgl::put32(&moved[0x102], 0xA0100001u);
    auto pl2 = payload({moved});
    const auto r = dgl::process_2005(pl2, self, dgl::Mode::On, keys);
    CHECK(r.retired == 1 && !keys.contains(0xB01004A1u) && r.other_player == 1);
    // RemoveActorAck 2516 (G27-2 t 65.03): u8 4 | u32 | u8 reason 0x19 | key at +6.
    const uint8_t rm[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x19, 0x44, 0x04, 0x10, 0xb0};
    uint32_t ks[4] = {};
    CHECK(dgl::removed_keys(rm, ks, 4) == 1 && ks[0] == 0xB0100444u);
    CHECK(dgl::removed_keys(std::span<const uint8_t>(rm, 9), ks, 4) == 0);
    keys.add(0xB0100444u);
    CHECK(keys.remove(0xB0100444u) && !keys.contains(0xB0100444u) && !keys.remove(0xB0100444u));
    CHECK(keys.removed() == 2);
}

void test_packets() {
    auto p = packet(dgl::kOpActorLoad, payload({kGuestDevice}));
    CHECK(dgl::payload_2005(p).size() == p.size() - dgl::kHeader);
    auto other = packet(2516, payload({kGuestDevice}));
    CHECK(dgl::payload_2005(other).empty());
    std::vector<uint8_t> tiny = {0xD5, 0x07, 0xFF};
    CHECK(dgl::payload_2005(tiny).empty());

    dgl::LocalKeys keys;
    keys.add(0xB01004A1u);
    // H27-2 t 309.536: the device's UpdateGimmickReq at its 2763 (first 0x20 bytes).
    const std::vector<uint8_t> upd = {0xa1, 0x04, 0x10, 0xb0, 0xc9, 0x7c, 0x66, 0xda, 0xc7, 0x13, 0xfb,
                                      0x85, 0x86, 0xd7, 0x74, 0x3f, 0x9f, 0xe1, 0x3b, 0xc4, 0x77, 0x03,
                                      0x10, 0xa0, 0x77, 0x03, 0x10, 0xa0, 0x03, 0x25, 0x05, 0x00};
    for (size_t i = 0; i < dgl::kWithheldOpCount; ++i) {
        CHECK(dgl::withhold_index(packet(dgl::kWithheldOps[i], upd), keys) == static_cast<int>(i));
    }
    CHECK(dgl::withhold_index(packet(2556, upd), keys) == -1);  // MoveActorReq: never withheld
    CHECK(dgl::withhold_index(packet(3067, upd), keys) == -1);  // RegionInOutReq
    auto host_dev = upd;
    dgl::put32(host_dev.data(), 0xB0100156u);
    CHECK(dgl::withhold_index(packet(3082, host_dev), keys) == -1);  // not local-only
    const std::vector<uint8_t> short_req = {0x0A, 0x0C, 0xFF, 0x02, 0x00, 0xA1, 0x04};
    CHECK(dgl::withhold_index(short_req, keys) == -1);

    // G27-2 t 86.925: SceneLevelOnDemandGimmickUnspawnReq 2221.
    const uint8_t u[] = {0x04, 0x01, 0x00, 0x00, 0x00, 0x35, 0x05, 0x10, 0xb0};
    uint32_t ks[4] = {};
    uint32_t total = 0;
    CHECK(dgl::unspawn_keys(u, ks, 4, &total) == 1 && ks[0] == 0xB0100535u && total == 1);
    CHECK(dgl::unspawn_keys(std::span<const uint8_t>(u, 8), ks, 4, &total) == 0 && total == 1);
    // A 2221 with more keys than the probe reads: the ones that fit, and how many there were.
    std::vector<uint8_t> many = {0x04, 20, 0, 0, 0};
    for (uint32_t i = 0; i < 20; ++i) {
        const uint32_t k = 0xB0100600u + i;
        for (int b = 0; b < 4; ++b) many.push_back(static_cast<uint8_t>(k >> (8 * b)));
    }
    CHECK(dgl::unspawn_keys(std::span<const uint8_t>(many.data(), 5 + 16 * 4), ks, 4, &total) == 4 && total == 20);
    uint32_t sixteen[16] = {};
    CHECK(dgl::unspawn_keys(std::span<const uint8_t>(many.data(), 5 + 16 * 4), sixteen, 16, &total) == 16);
    CHECK(sixteen[15] == 0xB010060Fu);
    const uint8_t not4[] = {0x05, 0x01, 0x00, 0x00, 0x00, 0x35, 0x05, 0x10, 0xb0};
    CHECK(dgl::unspawn_keys(not4, ks, 4) == 0);
}

void test_tables() {
    dgl::KeySet<4> s;
    CHECK(s.add(1) && s.add(2) && s.add(3) && s.add(4));
    CHECK(!s.add(3));
    CHECK(s.add(5));  // overwrites the oldest
    CHECK(s.contains(5) && !s.contains(1) && s.overwritten() == 1);
    CHECK(!s.add(0) && !s.contains(0));
    s.clear();
    CHECK(s.used() == 0);

    static dgl::Table<dgl::ClientEntry, 8> t;
    for (uint32_t k = 1; k <= 8; ++k) CHECK(t.get(0xB0100000u + k) != nullptr);
    CHECK(t.get(0xB0100100u) == nullptr && t.full() == 1 && t.used() == 8);
    CHECK(t.find(0xB0100003u) != nullptr && t.find(0xB0100200u) == nullptr);
    CHECK(t.get(0) == nullptr);
    CHECK(t.find(0xB0100003u)->parent.load() == 0xFFFFFFFFu);
    // Bounded probing: a big table looks at most kMaxProbe slots per key.
    static dgl::Table<dgl::TouchEntry, 256> big;
    uint64_t got = 0;
    for (uint32_t k = 1; k <= 400; ++k) got += big.get(0xB0100000u + k) != nullptr;
    CHECK(got == big.used() && got <= 256 && big.full() == 400 - got && big.full() > 0);
    CHECK(big.find(0xB0109999u) == nullptr);

    const auto own = [](uint32_t k) { return k == 0xA0100377u; };
    CHECK(dgl::classify(0xA0100377u, own) == dgl::Class::DockedOwn);
    CHECK(dgl::classify(0xA0100001u, own) == dgl::Class::DockedOtherPlayer);
    CHECK(dgl::classify(0xB010006Du, own) == dgl::Class::DockedNpc);
    CHECK(dgl::classify(0, own) == dgl::Class::World);
    CHECK(dgl::classify(0xFFFFFFFFu, own) == dgl::Class::Unknown);

    CHECK(dgl::age_bucket(10000, 9900) == 0);
    CHECK(dgl::age_bucket(10000, 9000) == 1);
    CHECK(dgl::age_bucket(20000, 12000) == 2);
    CHECK(dgl::age_bucket(100000, 50000) == 3);
    CHECK(dgl::age_bucket(200000, 50000) == 4);
    CHECK(dgl::age_bucket(200000, 0) == 5);
    CHECK(dgl::age_bucket(100, 200) == 5);
    CHECK(dgl::touch_op(3067) && dgl::touch_op(3082) && !dgl::touch_op(2221) && !dgl::touch_op(2005));
}

void test_mode_and_bytes() {
    CHECK(dgl::parse_mode("probe").mode == dgl::Mode::Probe);
    CHECK(dgl::parse_mode(" 1 ; S28 step 2").mode == dgl::Mode::On);
    CHECK(dgl::parse_mode("0").mode == dgl::Mode::Off && dgl::parse_mode("").ok);
    CHECK(!dgl::parse_mode("2").ok && dgl::parse_mode("2").mode == dgl::Mode::Off);  // phase 2 is not in this build
    // Every entry patched with one whole 5-byte instruction: mov [rsp+x],reg (48/4C 89 ?? 24 ??).
    const std::span<const uint8_t> heads[] = {dgl::kClientRequestHead, dgl::kClientRequest2Head, dgl::kDockHead,
                                              dgl::kServerExecuteHead, dgl::kServerUpdateHead};
    for (const auto& h : heads) {
        CHECK(h.size() >= dgl::kEntryPatch);
        CHECK((h[0] == 0x48 || h[0] == 0x4C) && h[1] == 0x89 && h[3] == 0x24);
    }
    // 0x2792A7B: mov [r14+0x90],eax (7 bytes), then mov rax,[rsi]; 0x2792A70 reads the field owner +0x180.
    const uint8_t mov[7] = {0x41, 0x89, 0x86, 0x90, 0x00, 0x00, 0x00};
    static_assert(dgl::kControllerWritePatch == 7);
    CHECK(std::memcmp(dgl::kControllerWriteBytes, mov, 7) == 0);
    static_assert(dgl::kFieldOwnerReadBytes[3] == 0x80 && dgl::kFieldOwnerReadBytes[4] == 0x01);
    static_assert(dgl::kFieldOwnerRead + 11 == dgl::kControllerWrite);
    // 0x2970ADF: E8 rel32 -> 0x28AD170.
    const int32_t rel = static_cast<int32_t>(dgl::le32(&dgl::kDockEquipCall[1]));
    CHECK(dgl::kDockEquipCall[0] == 0xE8 &&
          static_cast<uintptr_t>(static_cast<intptr_t>(dgl::kDockEquipCallSite) + 5 + rel) == dgl::kDockEquip);
    static_assert(dgl::kClientGimmickRequestSlot == dgl::kClientGimmickVtbl + 0x740);
    static_assert(dgl::kErrNotController == 0xD65F8D70u);
}

// P8 S29: docked_gimmicks_other on S28's real records (the guest 0xA01002D2 / 0x901002D1, the host's device
// 0xB010014E).
void test_other_player() {
    CHECK(dgl::docked_parent(kS28HostDevice) == 0xA0100001u);
    CHECK(dgl::le32(&kS28HostDevice[dgl::kRecController]) == 0x90100000u);
    const auto s28 = [] { return payload({kS28OwnChar, kS28HostDevice}); };
    // Off (S28 as it ran): the host's device unchanged, not local-only.
    {
        auto pl = s28();
        const auto orig = pl;
        dgl::Self self{0xA01002D2u, 0};
        dgl::LocalKeys keys;
        const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys);
        CHECK(self.own_user == 0x901002D1u && r.other_player == 1 && r.other_rewritten == 0 && r.other_unchanged == 0);
        CHECK(pl == orig && keys.used() == 0);
        CHECK(r.docked_n == 1 && !r.docked[0].other && r.docked[0].action == dgl::Action::OtherPlayer);
    }
    // 1: rewritten to the own user, local-only with its parent and the "other" flag; only +0x12 changes.
    {
        auto pl = s28();
        const auto orig = pl;
        dgl::Self self{0xA01002D2u, 0};
        dgl::LocalKeys keys;
        const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r.other_player == 1 && r.other_rewritten == 1 && r.other_would_rewrite == 0 && r.rewritten == 0);
        CHECK(controller_of(pl, 0xB010014Eu) == 0x901002D1u);
        size_t diff = 0;
        for (size_t i = 0; i < pl.size(); ++i) diff += pl[i] != orig[i];
        CHECK(diff >= 1 && diff <= 4 && pl.size() == orig.size());
        dgl::LocalKey lk;
        CHECK(keys.find(0xB010014Eu, lk) && lk.parent == 0xA0100001u && lk.flags == (dgl::kKeyOn | dgl::kKeyOther));
        CHECK(keys.used_with(dgl::kKeyOther) == 1);
        CHECK(r.docked_n == 1 && r.docked[0].other && r.docked[0].action == dgl::Action::Rewritten);
        CHECK(std::strcmp(dgl::docked_action_name(r.docked[0]), "REWRITTEN (other player, local drive)") == 0);
        // Its requests are withheld (the device's key at +0), like phase 1's.
        const std::vector<uint8_t> req = {0x4e, 0x01, 0x10, 0xb0, 0x59, 0xcc, 0x3e, 0x00};
        CHECK(dgl::withhold_index(packet(3082, req), keys) == 0);
        CHECK(dgl::withhold_index(packet(2774, req), keys) == 3);
        CHECK(dgl::withhold_index(packet(2556, req), keys) == -1);
        // Announced again with the same parent: kept. With another parent, or undocked: retired.
        auto again = payload({kS28HostDevice});
        const auto r2 = dgl::process_2005(again, self, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r2.retired == 0 && keys.contains(0xB010014Eu));
        std::vector<uint8_t> moved(std::begin(kS28HostDevice), std::end(kS28HostDevice));
        dgl::put32(&moved[0xE8], 0xA0100377u);
        dgl::put32(&moved[0x102], 0xA0100377u);
        auto pl3 = payload({moved});
        const auto r3 = dgl::process_2005(pl3, self, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r3.retired == 1);
        CHECK(keys.find(0xB010014Eu, lk) && lk.parent == 0xA0100377u);  // re-added for its new parent
        std::vector<uint8_t> undocked(std::begin(kS28HostDevice), std::end(kS28HostDevice));
        dgl::put32(&undocked[0xE8], 0);
        dgl::put32(&undocked[0x102], 0);
        auto pl4 = payload({undocked});
        const auto r4 = dgl::process_2005(pl4, self, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r4.retired == 1 && !keys.contains(0xB010014Eu) && r4.not_docked == 1);
        keys.add(0xB010014Eu, 0xA0100001u, dgl::kKeyOn | dgl::kKeyOther);
        keys.clear();  // a world change
        CHECK(keys.used() == 0);
    }
    // probe: nothing written, local-only without kKeyOn (counted as "would withhold" only).
    {
        auto pl = s28();
        const auto orig = pl;
        dgl::Self self{0xA01002D2u, 0};
        dgl::LocalKeys keys;
        const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys, dgl::Mode::Probe);
        CHECK(r.other_would_rewrite == 1 && r.other_rewritten == 0 && pl == orig);
        dgl::LocalKey lk;
        CHECK(keys.find(0xB010014Eu, lk) && lk.flags == dgl::kKeyOther);
        CHECK(std::strcmp(dgl::docked_action_name(r.docked[0]), "would rewrite (other player, probe)") == 0);
    }
    // The same checks as phase 1: own user unknown, not a user controller, not "Game Mechanic", malformed.
    {
        auto only = payload({kS28HostDevice});
        const auto orig = only;
        dgl::Self self{0xA01002D2u, 0};
        dgl::LocalKeys keys;
        const auto r = dgl::process_2005(only, self, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r.other_unchanged == 1 && only == orig && keys.used() == 0 &&
              r.docked[0].action == dgl::Action::OwnUserUnknown);
        dgl::Self known{0xA01002D2u, 0x901002D1u};
        std::vector<uint8_t> named(std::begin(kS28HostDevice), std::end(kS28HostDevice));
        named[0x22] = 'X';
        auto pl2 = payload({named});
        const auto r2 = dgl::process_2005(pl2, known, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r2.other_unchanged == 1 && r2.docked[0].action == dgl::Action::NotGameMechanic && keys.used() == 0);
        std::vector<uint8_t> npc_ctl(std::begin(kS28HostDevice), std::end(kS28HostDevice));
        dgl::put32(&npc_ctl[dgl::kRecController], 0xB0100001u);
        auto pl3 = payload({npc_ctl});
        const auto r3 = dgl::process_2005(pl3, known, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r3.other_unchanged == 1 && r3.docked[0].action == dgl::Action::NotUserController);
        auto bad = payload({kS28HostDevice});
        bad.push_back(0x40);
        bad.push_back(0x00);
        bad.push_back(0x07);
        const auto bad_orig = bad;
        const auto r4 = dgl::process_2005(bad, known, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r4.malformed && r4.other_unchanged == 1 && r4.docked[0].action == dgl::Action::MalformedPacket);
        CHECK(bad == bad_orig && keys.used() == 0);
        // docked_gimmicks Off makes everything off.
        auto pl5 = payload({kS28HostDevice});
        const auto r5 = dgl::process_2005(pl5, known, dgl::Mode::Off, keys, dgl::Mode::On);
        CHECK(r5.records == 0 && keys.used() == 0);
    }
    // The guest's own device and the host's in one packet, both rules at 1 (S27 bytes).
    {
        auto pl = payload({kUser90100376, kOwnChar, kHostDevice, kGuestDevice});
        dgl::Self self{0xA0100377u, 0};
        dgl::LocalKeys keys;
        const auto r = dgl::process_2005(pl, self, dgl::Mode::On, keys, dgl::Mode::On);
        CHECK(r.rewritten == 1 && r.other_rewritten == 1);
        CHECK(controller_of(pl, 0xB01004A1u) == 0x90100376u && controller_of(pl, 0xB0100156u) == 0x90100376u);
        dgl::LocalKey a, b;
        CHECK(keys.find(0xB01004A1u, a) && a.flags == dgl::kKeyOn && a.parent == 0xA0100377u);
        CHECK(keys.find(0xB0100156u, b) && b.flags == (dgl::kKeyOn | dgl::kKeyOther) && b.parent == 0xA0100001u);
    }
}

}  // namespace

int main() {
    test_records();
    test_guest_on();
    test_guest_probe_off();
    test_unknowns();
    test_malformed();
    test_filters();
    test_retire();
    test_packets();
    test_tables();
    test_mode_and_bytes();
    test_other_player();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_docked_gimmicks: all passed\n");
    return 0;
}
