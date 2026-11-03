#pragma once
// Option D, RPT-0 containment K1 (docs/research/opcion-d-rpt0.md §4): while a
// stage writes to the game (host), and on the P8 guest from its start
// (guest.cpp, whatever its mode), the game's Sentry reporter 0x1317D40 is cut. It is
// the function that attaches files to an error event (the failed slot's
// *.save and the game's text log on a failed save, and the second
// "sendMessageEvent" route) and hands the event to Sentry. The detour returns
// at once, as the game itself does when Sentry is off (0x1317D60: active
// byte 0 -> ret), and writes to cdmp.log what would have been sent.
// 0x1317D40 is in .rsrc (not .xtls), returns void and no caller reads rax.

#include <cstddef>
#include <cstdint>
#include <string>

namespace cdmp::session2 {

// The reporter's arguments (call site 0x2356434): rcx = pointer to the
// "reports active" byte, rdx = the reason (UTF-8, to
// sentry_value_new_message_event), r8 = the files: {data, count}, each
// element a pointer to an object whose first field is the path, a UTF-8
// char*: 0x1317E08 passes [[element]] to 0x44FD30, which measures it byte by
// byte (0x44FDB3) and converts it to UTF-16 (0x44FDC2) for
// sentry_scope_attach_filew.
struct ReportFiles {
    void** data;
    uint32_t count;
};
using ReporterFn = void(__fastcall*)(uint8_t* active, const char* reason, const ReportFiles* files);

// Hooks the game's reporter after checking its first bytes. False (nothing
// installed) on any mismatch: the caller must not go on with option D.
// Once on, it stays on until the game closes, also if a later stage of option D
// fails to install (the conservative side).
bool report_guard_install();
// The same hook without any log (K1 on the guest, which retries it until the
// game's code is there): true once it is on.
bool report_guard_try_install();
// The reporter's address (exe+rva), for the caller's log.
uintptr_t report_guard_target_rva();
// Hooks any function with the reporter's signature after comparing its first
// bytes with `head` (the tests use a fake). Calling it again is a no-op.
bool report_guard_install_at(void* target, const uint8_t* head, size_t head_size);
bool report_guard_active();
// Reports cut, and how many of them Sentry was active for (the game would
// have sent them; with crashpad_handler.exe moved out it is never active).
uint64_t report_guard_dropped();
uint64_t report_guard_dropped_live();

// What the reporter was asked to send, read defensively (any pointer may be
// bad): "reason '...', N files: a | b | ...". At most 8 paths of 260 chars.
std::string report_describe(const char* reason, const ReportFiles* files);

}  // namespace cdmp::session2
