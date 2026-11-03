#pragma once
// P8 session 35, phase E (docs/research/s25/s35-fase-e-guardado.md): [session2] host_save = sandbox | probe |
// export. With save_guard=redirect the sandbox is rebuilt from the real save folder at every start
// (save_hooks.cpp prepare_sandbox), so what the host saved in a run used to be lost two starts later. In export and
// probe the previous sandbox, its _prev and any folder set aside earlier are first copied into an immutable,
// verified generation (sizes + SHA-256 + manifest) OUTSIDE the real save folder and outside
// %LOCALAPPDATA%\Pearl Abyss (default <bin64>\cdmp_save_exports\<yyyymmdd-hhmmss>\); only then does the rebuild
// delete or move anything. If the export fails, nothing is deleted: the old sandbox is set aside under a new name
// and archived at a later start. probe also classifies every archived slot (host_save_logic.h classify_slot) and
// the guest_save_host hooks log each Save/Flush they see into the run's journal.
//
// NOTHING here writes the real save folder (it is only read, to compare): publishing to it (`real`) is blocked by
// the audit (S34 §5) and refused here. Restoring a generation is a manual, offline step with the game closed
// (scripts/host_save_restore.ps1).
//
// Threads: configure / archive / run_started run on the late-init thread before the save hooks exist (plain file
// APIs); observe_* run on the game's server thread inside the Save/Flush detours (one short append each).

#include <cstdint>
#include <string>
#include <vector>

#include "mod/session2/host_save_logic.h"

namespace cdmp::session2 {

struct HostSaveConfig {
    std::string mode = "sandbox";  // the raw ini value
    std::wstring dir;              // empty: <folder of the game's exe>\cdmp_save_exports; relative: against that folder
    uint32_t keep = hsv::kDefaultKeep;
};

// Before save_hooks_install (session2.cpp). Logs the mode once; `real` is refused (error line) and sandbox used.
void host_save_configure(const HostSaveConfig& cfg);
hsv::Mode host_save_mode();
std::wstring host_save_dir();  // the resolved export folder (empty in sandbox mode)

struct HostSaveArchive {
    hsv::Archive outcome = hsv::Archive::Off;
    std::wstring generation;              // the generation made now (empty: none, e.g. nothing new)
    std::vector<std::wstring> archived_aside;  // folders set aside earlier, now in a verified generation (deletable)
    std::wstring aside_name;              // where the sandbox goes if the plan sets it aside (a sibling's leaf name)
    size_t asides_existing = 0;           // folders already set aside (hsv::kMaxAside: then block, not one more)
};

// save_hooks.cpp prepare_sandbox, BEFORE anything is deleted or moved, with the plain file APIs (no hooks yet).
// Mode sandbox: {Off}. Never throws.
HostSaveArchive host_save_archive(const std::wstring& save_dir, const std::wstring& sandbox) noexcept;

// prepare_sandbox, after the new sandbox was made: this run's journal (keyed by the new sandbox's folder id).
void host_save_run_started(const std::wstring& sandbox) noexcept;

// guest_save_host.cpp: which of its hooks are on (written into the journal), and one event per Save / Flush entry.
// Cheap no-ops in sandbox mode or without a journal.
void host_save_note_hooks(bool save, bool flush) noexcept;
void host_save_observe_save(const hsv::SaveEvent& event) noexcept;
void host_save_observe_flush(const hsv::FlushEvent& event) noexcept;

// For the periodic line: "" in sandbox mode, else "host save <mode>: <last export>, journal N events".
std::string host_save_describe();

// TESTS ONLY (tests/optd_host_save_tests.cpp): 1 = the manifest write fails, 2 = the final rename fails, 0 = none.
// Nothing in the mod calls it.
void host_save_test_inject(int step) noexcept;

}  // namespace cdmp::session2
