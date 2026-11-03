// P8 session 35, phase E: the host's save sandbox exported before each rebuild
// (src/mod/session2/host_save_logic.h, host_save_export.{h,cpp};
// docs/research/s25/s35-fase-e-guardado.md).
//
// SCOPE: the pure logic (host_save modes with `real` refused, the slot
// whitelist, generation names, retention that never prunes the newest valid
// generation, the rebuild plan when an export fails, the manifest and journal
// round trips, the per-slot probe verdicts) and host_save_archive itself on
// fake folders under %TEMP% (copies verified, nothing new = no generation,
// failures leave the sources alone, retention, folders set aside, the run
// journal). NOT the save hooks (optd_hooks_redirect runs prepare_sandbox with
// an export), the game, its real save folder or Steam Cloud.

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "common/save_guard.h"
#include "common/sha256.h"
#include "mod/log.h"
#include "mod/session2/host_save_export.h"
#include "mod/session2/host_save_logic.h"

using namespace cdmp::session2;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// ---------------------------------------------------------------- the option

void test_parse_mode() {
    struct Case {
        const char* raw;
        hsv::Mode mode;
        bool known, real;
    };
    const Case cases[] = {
        {"", hsv::Mode::Sandbox, true, false},          {"sandbox", hsv::Mode::Sandbox, true, false},
        {" SANDBOX ; x", hsv::Mode::Sandbox, true, false}, {"0", hsv::Mode::Sandbox, true, false},
        {"export", hsv::Mode::Export, true, false},     {"Export\r", hsv::Mode::Export, true, false},
        {"probe", hsv::Mode::Probe, true, false},       {"probe # S35", hsv::Mode::Probe, true, false},
        // `real` (and what reads like "on") is refused: sandbox, with the error line.
        {"real", hsv::Mode::Sandbox, true, true},       {"REAL", hsv::Mode::Sandbox, true, true},
        {"1", hsv::Mode::Sandbox, true, true},          {"on", hsv::Mode::Sandbox, true, true},
        {"publish", hsv::Mode::Sandbox, false, false},  {"exports", hsv::Mode::Sandbox, false, false},
    };
    for (const Case& c : cases) {
        const hsv::ParsedMode p = hsv::parse_mode(c.raw);
        CHECK(p.mode == c.mode);
        CHECK(p.known == c.known);
        CHECK(p.refused_real == c.real);
    }
    CHECK(!hsv::archives(hsv::Mode::Sandbox) && hsv::archives(hsv::Mode::Export) && hsv::archives(hsv::Mode::Probe));
    CHECK(std::string(hsv::mode_name(hsv::Mode::Probe)) == "probe");
    CHECK(hsv::clamp_keep(0) == 1 && hsv::clamp_keep(20) == 20 && hsv::clamp_keep(100000) == hsv::kMaxKeep);
}

// ---------------------------------------------------------------- slots

void test_slots() {
    for (uint32_t n : {0u, 1u, 2u, 100u, 104u, 108u}) CHECK(hsv::slot_whitelisted(n));
    CHECK(hsv::slot_class(3) == hsv::SlotClass::CrossSave);
    for (uint32_t n = 200; n <= 207; ++n) {
        CHECK(hsv::slot_class(n) == hsv::SlotClass::GuestPool);
        CHECK(!hsv::slot_whitelisted(n));
    }
    for (uint32_t n : {4u, 99u, 109u, 199u, 208u, 999u, 0xFFFFFFFFu}) CHECK(hsv::slot_class(n) == hsv::SlotClass::Outside);
    CHECK(!hsv::slot_whitelisted(3) && !hsv::slot_whitelisted(204) && !hsv::slot_whitelisted(206));

    const auto slot = [](const wchar_t* n) { return hsv::parse_slot_folder(n); };
    CHECK(slot(L"slot0") && slot(L"slot0")->kind == hsv::FolderKind::Slot && slot(L"slot0")->number == 0);
    CHECK(slot(L"slot204") && slot(L"slot204")->number == 204);
    CHECK(slot(L"SLOT1") && slot(L"SLOT1")->number == 1);
    CHECK(!slot(L"slot01") && !slot(L"slot") && !slot(L"slot1234") && !slot(L"slot1x") && !slot(L"lobby.save"));
    CHECK(!slot(L"slo1") && !slot(L"xslot1"));
    CHECK(slot(L"slot0_new") && slot(L"slot0_new")->kind == hsv::FolderKind::New);
    CHECK(slot(L"slot2_backup") && slot(L"slot2_backup")->kind == hsv::FolderKind::Backup);
    CHECK(slot(L"slot0_backup_213942844459_delete") &&
          slot(L"slot0_backup_213942844459_delete")->kind == hsv::FolderKind::BackupDelete);
    CHECK(slot(L"slot0_whatever") && slot(L"slot0_whatever")->kind == hsv::FolderKind::Other);
    CHECK(hsv::account_folder(L"1234567890") && !hsv::account_folder(L"") && !hsv::account_folder(L"10597a"));
}

// ---------------------------------------------------------------- names, retention, rebuild

void test_names() {
    const hsv::Stamp s{2026, 10, 14, 19, 2, 32};
    CHECK(hsv::generation_name(s) == L"20261014-190232");
    CHECK(hsv::generation_name(s, 2) == L"20261014-190232-2");
    CHECK(hsv::generation_name(s, 1) == hsv::generation_name(s, 0));
    const auto g = hsv::parse_generation_name(L"20261014-190232");
    CHECK(g && g->stamp == L"20261014-190232" && g->seq == 1);
    const auto g12 = hsv::parse_generation_name(L"20261014-190232-12");
    CHECK(g12 && g12->seq == 12);
    for (const wchar_t* bad : {L"20261014-190232.partial", L"runs", L"20261014-19023", L"20261014-190232-1",
                               L"20261014-190232-02", L"2026101a-190232", L"20261014_190232", L"20261014-190232-",
                               L"20261014-190232-99999", L"20261014-190232x"}) {
        CHECK(!hsv::parse_generation_name(bad));
    }
    // Order: the stamp, then the number ("-10" after "-9").
    CHECK(hsv::generation_before(*hsv::parse_generation_name(L"20261014-190232-9"),
                                 *hsv::parse_generation_name(L"20261014-190232-10")));
    CHECK(hsv::generation_before(*hsv::parse_generation_name(L"20261014-190232-10"),
                                 *hsv::parse_generation_name(L"20261014-190233")));
    // A partial holds its name too.
    const std::vector<std::wstring> taken{L"20261014-190232", L"20261014-190232-2.partial"};
    CHECK(hsv::unique_generation_name(s, taken) == L"20261014-190232-3");
    CHECK(hsv::unique_generation_name(s, {}) == L"20261014-190232");
    // A number freed by pruning is not reused: the new one sorts last within its second. Other seconds do not count.
    const std::vector<std::wstring> pruned{L"20261014-190232-5", L"20261014-190231-9", L"runs"};
    CHECK(hsv::unique_generation_name(s, pruned) == L"20261014-190232-6");
    // The sandbox set aside.
    const std::wstring aside = hsv::aside_name(L"cdmp_save_sandbox", s, {});
    CHECK(aside == L"cdmp_save_sandbox_unexported_20261014-190232");
    const std::vector<std::wstring> sibs{aside};
    CHECK(hsv::aside_name(L"cdmp_save_sandbox", s, sibs) == L"cdmp_save_sandbox_unexported_20261014-190232-2");
    CHECK(hsv::parse_aside_name(L"cdmp_save_sandbox", aside) == std::optional<std::wstring>(L"20261014-190232"));
    CHECK(!hsv::parse_aside_name(L"cdmp_save_sandbox", L"cdmp_save_sandbox_prev"));
    CHECK(!hsv::parse_aside_name(L"cdmp_save_sandbox", L"cdmp_save_sandbox_unexported_x"));
}

std::vector<std::wstring> prune(std::vector<hsv::GenerationEntry> e, uint32_t keep, std::vector<std::wstring> protect = {}) {
    return hsv::plan_prune(e, keep, protect);
}

void test_retention() {
    const std::vector<hsv::GenerationEntry> five{{L"20261010-000000", true}, {L"20261011-000000", true},
                                                 {L"20261012-000000", true}, {L"20261013-000000", true},
                                                 {L"20261014-000000", true}};
    auto d = prune(five, 3);
    CHECK(d.size() == 2 && d[0] == L"20261010-000000" && d[1] == L"20261011-000000");
    CHECK(prune(five, 5).empty() && prune(five, 9).empty());
    d = prune(five, 0);  // clamped to 1: everything but the newest
    CHECK(d.size() == 4 && std::find(d.begin(), d.end(), L"20261014-000000") == d.end());
    // The newest VALID one is kept even when a newer folder is broken; invalid and partial ones are never pruned.
    const std::vector<hsv::GenerationEntry> mixed{{L"20261010-000000", true},  {L"20261011-000000", true},
                                                  {L"20261012-000000", false}, {L"20261013-000000.partial", false},
                                                  {L"runs", false},            {L"notes", true}};
    d = prune(mixed, 1);
    CHECK(d.size() == 1 && d[0] == L"20261010-000000");
    // Protected (the one just made, one a source matched) stay, even above `keep`.
    const std::vector<hsv::GenerationEntry> three{{L"20261010-000000", true}, {L"20261011-000000", true},
                                                  {L"20261012-000000", true}};
    d = prune(three, 1, {L"20261010-000000"});
    CHECK(d.size() == 1 && d[0] == L"20261011-000000");
    d = prune(three, 1, {L"20261010-000000", L"20261011-000000"});
    CHECK(d.empty());
    // Order by number within a second.
    d = prune({{L"20261014-000000-10", true}, {L"20261014-000000-9", true}}, 1);
    CHECK(d.size() == 1 && d[0] == L"20261014-000000-9");
    // A single valid generation is never pruned.
    CHECK(prune({{L"20261014-000000", true}}, 1).empty());

    // Review L2: "newest" is the manifest's sequence, not the local-time name (the clock went back an hour).
    d = prune({{L"20261014-200000", true, 1}, {L"20261014-190000", true, 2}}, 1);
    CHECK(d.size() == 1 && d[0] == L"20261014-200000");
    std::vector<hsv::GenerationEntry> seqs{{L"20261014-200000", true, 5}, {L"20261014-100000", false, 9}};
    CHECK(hsv::next_sequence(seqs) == 10);  // above invalid ones too
    CHECK(hsv::next_sequence({}) == 1);
    // Review L3: a generation a kept one is identical_to stays, transitively.
    const std::vector<hsv::GenerationEntry> chain{{L"20261010-000000", true, 1},
                                                  {L"20261011-000000", true, 2, {L"20261010-000000"}},
                                                  {L"20261012-000000", true, 3, {L"20261011-000000"}},
                                                  {L"20261013-000000", true, 4}};
    d = prune(chain, 1);
    CHECK(d.size() == 3);  // only 13 (no references) is kept: 10-12 may go together
    d = prune(chain, 2);  // 12 stays, so 11 (its reference) and then 10 stay too
    CHECK(d.empty());
    const std::vector<hsv::GenerationEntry> loose{{L"20261010-000000", true, 1},
                                                  {L"20261011-000000", true, 2, {L"20261010-000000"}},
                                                  {L"20261012-000000", true, 3},
                                                  {L"20261013-000000", true, 4}};
    d = prune(loose, 2);  // 10 and 11 go together: nothing kept refers to them
    CHECK(d.size() == 2);
    d = prune(loose, 3);  // only 10 would go, but 11 (kept) refers to it
    CHECK(d.empty());
}

void test_rebuild_plan() {
    for (hsv::Archive a : {hsv::Archive::Off, hsv::Archive::Nothing, hsv::Archive::Done, hsv::Archive::Failed}) {
        const hsv::RebuildPlan none = hsv::plan_rebuild(false, a);
        CHECK(!none.delete_prev && !none.rotate_sandbox_to_prev && !none.set_sandbox_aside);
    }
    for (hsv::Archive a : {hsv::Archive::Off, hsv::Archive::Nothing, hsv::Archive::Done}) {
        const hsv::RebuildPlan p = hsv::plan_rebuild(true, a);
        CHECK(p.delete_prev && p.rotate_sandbox_to_prev && !p.set_sandbox_aside);  // today's behaviour
    }
    // The archive failed: nothing deleted, the old sandbox set aside, _prev left alone.
    const hsv::RebuildPlan f = hsv::plan_rebuild(true, hsv::Archive::Failed);
    CHECK(!f.delete_prev && !f.rotate_sandbox_to_prev && f.set_sandbox_aside && !f.fall_back_to_block);
    CHECK(hsv::plan_rebuild(true, hsv::Archive::Failed, hsv::kMaxAside - 1).set_sandbox_aside);
    // Review L1: with kMaxAside already set aside, nothing is touched and the run blocks.
    const hsv::RebuildPlan b = hsv::plan_rebuild(true, hsv::Archive::Failed, hsv::kMaxAside);
    CHECK(b.fall_back_to_block && !b.set_sandbox_aside && !b.delete_prev && !b.rotate_sandbox_to_prev);
    CHECK(!hsv::plan_rebuild(true, hsv::Archive::Done, 10).fall_back_to_block);  // only after a failed export
}

void test_resolved() {
    const hsv::NodeId save{1, 10}, game{1, 5}, local{1, 4}, other{2, 7};
    hsv::ResolvedDir r;
    r.readable = true;
    r.forbidden = {save, game};
    r.save_ancestors = {save, {1, 6}, game, local, {1, 1}};
    r.root_ancestors = {other, {2, 1}};
    CHECK(!hsv::resolved_dir_problem(r));
    hsv::ResolvedDir j = r;  // a junction that leads into Pearl Abyss (or SUBST, 8.3, \\localhost\C$)
    j.root_ancestors = {{1, 6}, game, local};
    CHECK(hsv::resolved_dir_problem(j));
    hsv::ResolvedDir c = r;  // the export folder resolves to an ancestor of the save folder
    c.root = local;
    c.root_ancestors = {local, {1, 1}};
    CHECK(hsv::resolved_dir_problem(c));
    hsv::ResolvedDir p = r;
    p.reparse = true;
    CHECK(hsv::resolved_dir_problem(p));
    hsv::ResolvedDir u = r;
    u.readable = false;
    CHECK(hsv::resolved_dir_problem(u));
}

void test_export_dir() {
    const std::wstring save = L"C:\\Users\\user\\AppData\\Local\\Pearl Abyss\\CD\\save";
    const std::wstring game = L"C:\\Users\\user\\AppData\\Local\\Pearl Abyss";
    const auto under = [](std::wstring_view a, std::wstring_view b) { return cdmp::saveguard::is_under(a, b); };
    const auto problem = [&](const wchar_t* d) { return hsv::export_dir_problem(d, save, game, under); };
    CHECK(!problem(L"D:\\SteamLibrary\\steamapps\\common\\Crimson Desert\\bin64\\cdmp_save_exports"));
    CHECK(!problem(L"C:\\Emulador CD\\backups\\host_save_exports"));
    CHECK(!problem(L"\\\\nas\\share\\cdmp"));
    CHECK(problem(L""));
    CHECK(problem(L"cdmp_save_exports"));
    CHECK(problem(L"C:\\Users\\user\\AppData\\Local\\Pearl Abyss\\CD\\cdmp_exports"));
    CHECK(problem(L"c:/users/USER/appdata/local/pearl abyss/cd/save/x"));
    CHECK(problem(L"C:\\Users\\user\\AppData\\Local"));  // contains the save folder
    CHECK(problem(L"C:\\"));
    CHECK(problem(L"C:\\Program Files (x86)\\Steam\\userdata\\12345\\3321460"));
    CHECK(problem(L"D:\\Steam\\USERDATA"));
}

// ---------------------------------------------------------------- manifest, trees, journal

hsv::Manifest sample_manifest() {
    hsv::Manifest m;
    m.mod_build = "0x1d99ed0a dirty";
    m.generation = "20261014-190232";
    m.created = "2026-10-14 19:02:32.125";
    m.run = "20261014-190230-p1234";
    m.run_start = "2026-10-14 19:02:30.000";
    m.mode = "probe";
    m.created_utc = "2026-10-14T17:02:32Z";
    m.seq = 42;
    m.sources.push_back({"sandbox", "C:\\Users\\x\\AppData\\Local\\Pearl Abyss\\CD\\cdmp_save_sandbox",
                         "20261014-180308-p99", "1234ABCD:0001000000002A3B", ""});
    m.sources.push_back({"prev", "C:\\...\\cdmp_save_sandbox_prev", "unknown", "", "20261013-100000/sandbox"});
    m.accounts.push_back("sandbox|1234567890");
    m.files.push_back({"sandbox", "1234567890\\slot0\\lobby.save", 506, std::string(64, 'a'), 134000000000000000ull});
    m.files.push_back({"sandbox", "1234567890\\slot0\\save.save", 1944571, std::string(64, 'b'), 134000000000000001ull});
    m.files.push_back({"journal", "sandbox.txt", 900, std::string(64, 'c'), 1});
    m.slots.push_back({"sandbox", "1234567890", "slot0", "host"});
    m.slots.push_back({"sandbox", "1234567890", "slot0_new", "leftover _new"});
    m.probes.push_back({"sandbox", "1234567890", "slot0", "candidate", "1 host save(s) ..."});
    m.notes.push_back("sandbox: 1 steam_autocloud.vdf not copied");
    m.complete = true;
    return m;
}

void test_manifest() {
    const hsv::Manifest m = sample_manifest();
    const std::string text = hsv::manifest_text(m);
    const auto back = hsv::parse_manifest(text);
    CHECK(back.has_value());
    if (back) {
        CHECK(hsv::manifest_text(*back) == text);  // round trip, byte for byte
        CHECK(back->complete && back->generation == m.generation && back->run == m.run);
        CHECK(back->seq == 42 && back->created_utc == "2026-10-14T17:02:32Z");
        CHECK(back->game_build == "1.0.0.2976" && back->pe_timestamp == "0x6AB28F00");
        CHECK(back->sources.size() == 2 && back->sources[1].identical_to == "20261013-100000/sandbox");
        CHECK(back->files.size() == 3 && back->files[1].size == 1944571 && back->files[1].mtime == 134000000000000001ull);
        CHECK(back->slots.size() == 2 && back->probes.size() == 1 && back->probes[0].verdict == "candidate");
        CHECK(back->accounts.size() == 1 && back->accounts[0] == "sandbox|1234567890");
        CHECK(hsv::tree_of(*back, "sandbox").size() == 2 && hsv::tree_of(*back, "journal").size() == 1);
    }
    // Not complete without the last line; nothing may follow it.
    hsv::Manifest open = m;
    open.complete = false;
    const auto o = hsv::parse_manifest(hsv::manifest_text(open));
    CHECK(o && !o->complete);
    CHECK(!hsv::parse_manifest(text + "file=sandbox|x|1|" + std::string(64, 'd') + "|1\n"));
    // Wrong header, bad hash, bad size, unknown format: refused. Unknown keys: skipped.
    CHECK(!hsv::parse_manifest("something else\nformat=1\n"));
    CHECK(!hsv::parse_manifest(std::string(hsv::kManifestHeader) + "\nfile=sandbox|x|1|ABC|1\n"));
    CHECK(!hsv::parse_manifest(std::string(hsv::kManifestHeader) + "\nfile=sandbox|x|-1|" + std::string(64, 'a') + "|1\n"));
    CHECK(!hsv::parse_manifest(std::string(hsv::kManifestHeader) + "\nformat=2\n"));
    CHECK(!hsv::parse_manifest(std::string(hsv::kManifestHeader) + "\nseq=x\n"));
    CHECK(hsv::parse_manifest(std::string(hsv::kManifestHeader) + "\nfuture_key=1\r\n").has_value());
    // A '|' or a line break inside a field cannot break the format.
    hsv::Manifest odd = m;
    odd.sources[0].from = "C:\\a|b\nc";
    const auto od = hsv::parse_manifest(hsv::manifest_text(odd));
    CHECK(od && od->sources[0].from == "C:\\a_b_c");
}

void test_trees() {
    const std::vector<hsv::TreeFile> a{{"1\\slot0\\save.save", 10, std::string(64, 'a')},
                                       {"1\\slot0\\lobby.save", 5, std::string(64, 'b')}};
    std::vector<hsv::TreeFile> b{{"1\\SLOT0\\LOBBY.SAVE", 5, std::string(64, 'b')},
                                 {"1\\slot0\\save.save", 10, std::string(64, 'a')}};
    CHECK(hsv::same_tree(a, b));
    CHECK(hsv::verify_tree(a, b).empty());
    b[1].sha256 = std::string(64, 'c');
    CHECK(!hsv::same_tree(a, b));
    auto p = hsv::verify_tree(a, b);
    CHECK(p.size() == 1 && p[0].find("SHA-256 differs") != std::string::npos);
    b[1] = {"1\\slot0\\save.save", 11, std::string(64, 'a')};
    p = hsv::verify_tree(a, b);
    CHECK(p.size() == 1 && p[0].find("size 11") != std::string::npos);
    b.push_back({"extra.txt", 1, std::string(64, 'e')});
    CHECK(hsv::verify_tree(a, b).size() == 2);
    CHECK(hsv::verify_tree(a, {}).size() == 2);  // both missing
    CHECK(!hsv::same_tree(a, {}));
    CHECK(hsv::same_tree({}, {}));
}

hsv::SaveEvent save_event(gsv::Owner o, bool positive, uint32_t asked, uint32_t written, int32_t result,
                          uint32_t depth = 0) {
    hsv::SaveEvent e;
    e.seq = 1;
    e.time = "2026-10-14 18:21:16.821";
    e.owner = o;
    e.positive_host = positive;
    e.caller = "autosave";
    e.asked = asked;
    e.written = written;
    e.action = written == hsv::kNoSlot ? "REFUSE" : asked == written ? "pass" : "REMAP";
    e.result_read = true;
    e.result = result;
    e.depth = depth;
    return e;
}

void test_journal() {
    const hsv::JournalHeader h{"20261014-180308-p42", "2026-10-14 18:03:08.502", "1234ABCD:0001000000002A3B",
                               "0x1d99ed0a", "probe"};
    std::string text = hsv::journal_header_text(h) + hsv::journal_hooks_line(true, true);
    text += hsv::journal_save_line(save_event(gsv::Owner::Host, true, 0, 0, 0));
    text += hsv::journal_save_line(save_event(gsv::Owner::User2, false, 100, 204, 0));
    text += hsv::journal_save_line(save_event(gsv::Owner::Unidentified, false, 2, hsv::kNoSlot, -5));
    hsv::FlushEvent f;
    f.seq = 4;
    f.time = "t";
    f.owner = gsv::Owner::Host;
    f.positive_host = true;
    f.caller = "logout phase";
    f.pending_read = true;
    f.pending = -1;
    f.result_read = false;
    text += hsv::journal_flush_line(f);
    auto j = hsv::parse_journal(text);
    CHECK(j.has_value());
    CHECK(j && j->saves.size() == 3 && j->flushes.size() == 1);
    if (j && j->saves.size() == 3 && j->flushes.size() == 1) {
        CHECK(j->header.run == h.run && j->header.sandbox_id == h.sandbox_id && j->header.mode == "probe");
        CHECK(j->hooks && j->hook_save && j->hook_flush && !j->truncated);
        CHECK(j->saves[0].owner == gsv::Owner::Host && j->saves[0].positive_host && j->saves[0].written == 0);
        CHECK(j->saves[1].owner == gsv::Owner::User2 && j->saves[1].asked == 100 && j->saves[1].written == 204);
        CHECK(j->saves[2].written == hsv::kNoSlot && j->saves[2].result == -5);
        CHECK(j->flushes[0].pending == -1 && !j->flushes[0].result_read && j->flushes[0].caller == "logout phase");
        CHECK(hsv::journal_save_line(j->saves[1]) == hsv::journal_save_line(save_event(gsv::Owner::User2, false, 100, 204, 0)));
    }
    // A line cut by a crash, a garbled line, the cap: not every event is known.
    auto cut = hsv::parse_journal(text + "save|9|t|host|1|autosave|0|0|pa");
    CHECK(cut && cut->truncated && cut->saves.size() == 3);
    auto garbled = hsv::parse_journal(text + "save|x|y\n");
    CHECK(garbled && garbled->truncated);
    auto capped = hsv::parse_journal(text + std::string(hsv::kJournalTruncated) + "\n");
    CHECK(capped && capped->truncated);
    CHECK(!hsv::parse_journal("not a journal\n"));
    // No hooks line: the run did not observe Save.
    auto bare = hsv::parse_journal(hsv::journal_header_text(h));
    CHECK(bare && !bare->hooks);
}

void test_positive_host() {
    gsv::UserFacts f;
    CHECK(gsv::classify_owner(f) == gsv::Owner::Host);  // the fallback: no session 2 ever built ...
    CHECK(!hsv::positive_host(f));                      // ... is not a positive identity (audit S34 §5)
    f.is_host = true;
    CHECK(hsv::positive_host(f));
    gsv::UserFacts s;
    s.session_known = true;
    s.session_is_host = true;
    s.any_guest_session = true;
    CHECK(hsv::positive_host(s));
    s.character_is_user2 = true;  // any sign of user 2 wins
    CHECK(!hsv::positive_host(s));
    gsv::UserFacts u;
    u.any_guest_session = true;
    CHECK(!hsv::positive_host(u));
}

// ---------------------------------------------------------------- probe verdicts

hsv::SlotFacts good_facts(uint32_t n = 0) {
    hsv::SlotFacts f;
    f.folder = hsv::SlotFolder{hsv::FolderKind::Slot, n};
    f.lobby = f.save = true;
    f.identical_to_real = false;
    f.pair_gap_ms = 108;
    f.obs.journal = f.obs.hooks = true;
    f.obs.host_positive_ok = 1;
    return f;
}

void test_classify() {
    using V = hsv::Verdict;
    const auto verdict = [](const hsv::SlotFacts& f) { return hsv::classify_slot(f).verdict; };
    CHECK(verdict(good_facts(0)) == V::Candidate);
    CHECK(verdict(good_facts(2)) == V::Candidate);
    CHECK(verdict(good_facts(108)) == V::Candidate);
    CHECK(hsv::classify_slot(good_facts()).reasons.find("NOT proven") != std::string::npos);
    // Excluded whatever else holds.
    for (uint32_t n : {3u, 200u, 204u, 206u, 207u, 4u, 109u}) CHECK(verdict(good_facts(n)) == V::NotCandidate);
    CHECK(hsv::classify_slot(good_facts(204)).reasons.find("guest's own slot") != std::string::npos);
    hsv::SlotFacts f = good_facts();
    f.folder.reset();
    CHECK(verdict(f) == V::NotCandidate);
    f = good_facts();
    f.folder->kind = hsv::FolderKind::Backup;
    CHECK(verdict(f) == V::NotCandidate);
    f = good_facts();
    f.lobby = false;
    CHECK(verdict(f) == V::NotCandidate);
    f = good_facts();
    f.save_check = gsv::HeaderCheck::Magic;
    CHECK(verdict(f) == V::NotCandidate);
    f = good_facts();
    f.identical_to_real = true;
    CHECK(verdict(f) == V::NotCandidate);
    f = good_facts();
    f.obs.other_owner = 1;  // user 2 / unidentified wrote this slot
    CHECK(verdict(f) == V::NotCandidate);
    // What makes it unknown.
    f = good_facts();
    f.obs.journal = false;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.hooks = false;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.truncated = true;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.identical_to_real.reset();
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.nested = 1;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.host_failed = 1;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.flush_pending_here = 1;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.flush_pending_unread = 1;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.redirect_failed = 2;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.pair_gap_ms = hsv::kPairGapMs + 1;
    CHECK(verdict(f) == V::Unknown);
    f = good_facts();
    f.obs.host_positive_ok = 0;
    f.obs.host_fallback = 1;  // host only by the fallback
    CHECK(verdict(f) == V::Unknown);
    CHECK(hsv::classify_slot(f).reasons.find("fallback") != std::string::npos);
    f.obs.host_fallback = 0;  // changed, no save observed
    CHECK(verdict(f) == V::Unknown);

    // From a journal: per slot.
    hsv::Journal j;
    j.hooks = j.hook_save = j.hook_flush = true;
    j.saves.push_back(save_event(gsv::Owner::Host, true, 0, 0, 0));
    j.saves.push_back(save_event(gsv::Owner::User2, false, 0, 204, 0));            // remapped: 204, not 0
    j.saves.push_back(save_event(gsv::Owner::Unidentified, false, 2, hsv::kNoSlot, -1));  // refused: nothing written
    j.saves.push_back(save_event(gsv::Owner::Host, false, 1, 1, 0));               // fallback only
    j.saves.push_back(save_event(gsv::Owner::Host, true, 100, 100, 7));            // failed
    j.saves.push_back(save_event(gsv::Owner::Host, true, 101, 101, 0, 1));         // nested
    j.saves.push_back(save_event(gsv::Owner::User2, false, 102, 102, 0));          // guest_own_save=probe: passed
    hsv::FlushEvent fl;
    fl.pending_read = true;
    fl.pending = 2;
    j.flushes.push_back(fl);
    const hsv::SlotObservation o0 = hsv::observe_slot(&j, 0);
    CHECK(o0.journal && o0.hooks && o0.host_positive_ok == 1 && o0.other_owner == 0 && o0.flush_pending_here == 0);
    CHECK(hsv::observe_slot(&j, 204).other_owner == 1);
    const hsv::SlotObservation o2 = hsv::observe_slot(&j, 2);
    CHECK(o2.other_owner == 0 && o2.host_positive_ok == 0 && o2.flush_pending_here == 1);
    CHECK(hsv::observe_slot(&j, 1).host_fallback == 1);
    CHECK(hsv::observe_slot(&j, 100).host_failed == 1);
    const hsv::SlotObservation o101 = hsv::observe_slot(&j, 101);
    CHECK(o101.nested == 1 && o101.host_positive_ok == 0);
    CHECK(hsv::observe_slot(&j, 102).other_owner == 1);
    CHECK(!hsv::observe_slot(nullptr, 0).journal);
}

// ---------------------------------------------------------------- the archive on fake folders

std::vector<uint8_t> save_bytes(uint32_t data, uint8_t fill) {
    std::vector<uint8_t> b(0x80 + data, fill);
    std::memset(b.data(), 0, 0x80);
    std::memcpy(b.data(), "SAVE", 4);
    b[4] = 1;  // version
    b[6] = 0x80;
    const uint32_t raw = data * 3;
    std::memcpy(b.data() + 0x12, &raw, 4);
    std::memcpy(b.data() + 0x16, &data, 4);
    return b;
}

bool write_bytes(const fs::path& p, const std::vector<uint8_t>& b) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream o(p, std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return static_cast<bool>(o);
}
bool write_text(const fs::path& p, const std::string& s) { return write_bytes(p, std::vector<uint8_t>(s.begin(), s.end())); }

std::string ascii(const std::wstring& w) {  // generation names are ASCII
    std::string s;
    for (const wchar_t c : w) s += static_cast<char>(c);
    return s;
}

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::string sha_of(const fs::path& p) {
    const std::string s = read_all(p);
    cdmp::sha256::Hasher h;
    h.update(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    return gsv::hex(h.finish());
}

// Every entry under `dir`: path, kind, size, attributes, write time and SHA-256 (byte-for-byte comparison).
std::string tree_snapshot(const fs::path& dir) {
    std::vector<std::string> lines;
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        const fs::path p = e.path();
        WIN32_FILE_ATTRIBUTE_DATA a{};
        GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a);
        std::string line = ascii(fs::relative(p, dir, ec).wstring()) + " attrs " + std::to_string(a.dwFileAttributes) +
                           " write " + std::to_string((static_cast<uint64_t>(a.ftLastWriteTime.dwHighDateTime) << 32) |
                                                      a.ftLastWriteTime.dwLowDateTime);
        if (!e.is_directory()) line += " size " + std::to_string(a.nFileSizeLow) + " sha " + sha_of(p);
        lines.push_back(line);
    }
    std::sort(lines.begin(), lines.end());
    std::string s;
    for (const std::string& l : lines) s += l + "\n";
    return s;
}

bool make_junction(const fs::path& link, const fs::path& target) {
    const std::wstring cmd = L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\" >nul 2>nul";
    return _wsystem(cmd.c_str()) == 0 && fs::exists(link);
}

std::vector<std::wstring> generations(const fs::path& root) {
    std::vector<std::wstring> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (e.is_directory() && hsv::parse_generation_name(e.path().filename().wstring())) {
            out.push_back(e.path().filename().wstring());
        }
    }
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return hsv::generation_before(*hsv::parse_generation_name(a), *hsv::parse_generation_name(b));
    });
    return out;
}

std::optional<hsv::Manifest> manifest_of(const fs::path& gen) {
    return hsv::parse_manifest(read_all(gen / "manifest.txt"));
}

// Every file of the manifest read back from the generation folder: sizes and hashes.
bool generation_verifies(const fs::path& gen) {
    const auto m = manifest_of(gen);
    if (!m || !m->complete) return false;
    for (const hsv::FileRecord& f : m->files) {
        const fs::path p = f.source == "journal" ? gen / "journals" / f.path : gen / f.source / f.path;
        std::error_code ec;
        if (fs::file_size(p, ec) != f.size || ec || sha_of(p) != f.sha256) return false;
    }
    return true;
}

const hsv::ProbeRecord* probe_of(const hsv::Manifest& m, const std::string& folder) {
    for (const hsv::ProbeRecord& p : m.probes) {
        if (p.source == "sandbox" && p.folder == folder) return &p;
    }
    return nullptr;
}

void test_archive() {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(tmp, ec)) {
        if (e.path().filename().wstring().rfind(L"cdmp_optd_hostsave_", 0) == 0) fs::remove_all(e.path(), ec);
    }
    const fs::path root = fs::path(tmp) / (L"cdmp_optd_hostsave_" + std::to_wstring(GetCurrentProcessId()));
    const fs::path cd = root / "Pearl Abyss" / "CD";
    const fs::path save = cd / "save", box = cd / "cdmp_save_sandbox", prev = cd / "cdmp_save_sandbox_prev";
    const fs::path exports = root / "exports";
    const std::wstring id = L"1234567890";
    cdmp::log::open((root / "hostsave.log").wstring());

    const auto lobby1 = save_bytes(0x186, 0x11), save1 = save_bytes(0x4000, 0x12);
    for (const fs::path& base : {save, box, prev}) {
        CHECK(write_bytes(base / id / "slot1" / "lobby.save", lobby1));
        CHECK(write_bytes(base / id / "slot1" / "save.save", save1));
        CHECK(write_text(base / id / "steam_autocloud.vdf", "vdf"));
    }
    // The host's autosave of the last run, the guest's 204, the options file.
    CHECK(write_bytes(box / id / "slot0" / "lobby.save", save_bytes(0x186, 0x21)));
    CHECK(write_bytes(box / id / "slot0" / "save.save", save_bytes(0x5000, 0x22)));
    CHECK(write_bytes(box / id / "slot204" / "lobby.save", save_bytes(0x180, 0x31)));
    CHECK(write_bytes(box / id / "slot204" / "save.save", save_bytes(0x4800, 0x32)));
    CHECK(write_text(box / "user_engine_option_save.xml", "<options/>"));
    const std::string sandbox_slot0 = sha_of(box / id / "slot0" / "save.save");
    const std::string save_before = tree_snapshot(save);
    CHECK(save_before.find("slot1\\save.save") != std::string::npos);

    // ---- sandbox mode: nothing at all
    host_save_configure({"sandbox", exports.wstring(), 3});
    CHECK(host_save_mode() == hsv::Mode::Sandbox && host_save_dir().empty());
    HostSaveArchive a = host_save_archive(save.wstring(), box.wstring());
    CHECK(a.outcome == hsv::Archive::Off);
    CHECK(!fs::exists(exports));
    host_save_configure({"real", exports.wstring(), 3});  // refused: sandbox
    CHECK(host_save_mode() == hsv::Mode::Sandbox);
    CHECK(host_save_archive(save.wstring(), box.wstring()).outcome == hsv::Archive::Off);

    // ---- export: the sandbox and its _prev, verified; the sources untouched
    host_save_configure({"export", exports.wstring(), 3});
    CHECK(host_save_mode() == hsv::Mode::Export);
    a = host_save_archive(save.wstring(), box.wstring());
    CHECK(a.outcome == hsv::Archive::Done && !a.generation.empty());
    CHECK(a.aside_name.rfind(L"cdmp_save_sandbox_unexported_", 0) == 0);
    auto gens = generations(exports);
    CHECK(gens.size() == 1);
    if (!gens.empty()) {
        const fs::path g = exports / gens[0];
        CHECK(generation_verifies(g));
        CHECK(sha_of(g / "sandbox" / id / "slot0" / "save.save") == sandbox_slot0);
        CHECK(fs::exists(g / "prev" / id / "slot1" / "save.save"));
        CHECK(!fs::exists(g / "sandbox" / id / "steam_autocloud.vdf"));  // Steam Cloud's marker left out
        CHECK(!fs::exists(exports / (gens[0] + L".partial")));
        const auto m = manifest_of(g);
        CHECK(m && m->mode == "export" && m->sources.size() == 2 && m->probes.empty());
        if (m) {
            CHECK(m->sources[0].label == "sandbox" && m->sources[0].identical_to.empty() &&
                  m->sources[0].produced_by == "unknown");
            CHECK(!m->sources[0].tree_id.empty());
            CHECK(std::any_of(m->slots.begin(), m->slots.end(),
                              [](const hsv::SlotRecord& s) { return s.folder == "slot204" && s.cls == "guest-pool"; }));
            CHECK(std::any_of(m->notes.begin(), m->notes.end(),
                              [](const std::string& n) { return n.find("steam_autocloud.vdf") != std::string::npos; }));
        }
    }
    CHECK(fs::exists(box / id / "slot0" / "save.save") && fs::exists(prev / id / "slot1"));  // nothing moved

    // ---- again, nothing changed: no new generation
    a = host_save_archive(save.wstring(), box.wstring());
    CHECK(a.outcome == hsv::Archive::Done && a.generation.empty());
    CHECK(generations(exports).size() == 1);

    // ---- probe, with this "run's" journal: the next start classifies what this run wrote
    host_save_configure({"probe", exports.wstring(), 3});
    CHECK(host_save_archive(save.wstring(), box.wstring()).outcome == hsv::Archive::Done);  // checks the folder
    host_save_run_started(box.wstring());
    host_save_note_hooks(true, true);
    host_save_observe_save(save_event(gsv::Owner::Host, true, 0, 0, 0));
    host_save_observe_save(save_event(gsv::Owner::User2, false, 100, 204, 0));
    hsv::FlushEvent fl;
    fl.owner = gsv::Owner::Host;
    fl.positive_host = true;
    fl.caller = "logout phase";
    fl.pending_read = true;
    fl.pending = -1;
    host_save_observe_flush(fl);
    CHECK(host_save_describe().find("journal 3 events") != std::string::npos);
    CHECK(write_bytes(box / id / "slot0" / "save.save", save_bytes(0x5000, 0x23)));  // a new autosave
    CHECK(write_bytes(box / id / "slot0" / "lobby.save", save_bytes(0x186, 0x24)));
    a = host_save_archive(save.wstring(), box.wstring());
    CHECK(a.outcome == hsv::Archive::Done && !a.generation.empty());
    gens = generations(exports);
    CHECK(gens.size() == 2);
    if (gens.size() == 2) {
        const fs::path g = exports / gens[1];
        CHECK(generation_verifies(g));
        CHECK(fs::exists(g / "journals" / "sandbox.txt"));
        CHECK(!fs::exists(g / "prev"));  // the _prev is the earlier generation's: not copied again
        const auto m = manifest_of(g);
        CHECK(m.has_value());
        if (m) {
            CHECK(m->mode == "probe" && m->sources.size() == 2);
            CHECK(m->sources[0].produced_by != "unknown");
            CHECK(m->sources[1].label == "prev" && m->sources[1].identical_to == ascii(gens[0]) + "/prev");
            const hsv::ProbeRecord* p0 = probe_of(*m, "slot0");
            const hsv::ProbeRecord* p1 = probe_of(*m, "slot1");
            const hsv::ProbeRecord* p204 = probe_of(*m, "slot204");
            CHECK(p0 && p0->verdict == "candidate");
            CHECK(p1 && p1->verdict == "not-candidate" && p1->reasons.find("identical to the real") != std::string::npos);
            CHECK(p204 && p204->verdict == "not-candidate");
        }
    }

    // ---- failures: nothing deleted, nothing half made left, the sources untouched
    host_save_configure({"export", (cd / "exports").wstring(), 3});  // inside %LOCALAPPDATA%\Pearl Abyss
    a = host_save_archive(save.wstring(), box.wstring());
    CHECK(a.outcome == hsv::Archive::Failed && a.generation.empty() && a.archived_aside.empty());
    CHECK(!fs::exists(cd / "exports"));
    CHECK(write_text(root / "cloudy" / "steam_autocloud.vdf", "vdf"));
    host_save_configure({"export", (root / "cloudy" / "exports").wstring(), 3});  // under an Auto-Cloud root
    CHECK(host_save_archive(save.wstring(), box.wstring()).outcome == hsv::Archive::Failed);
    CHECK(!fs::exists(root / "cloudy" / "exports"));
    host_save_configure({"export", exports.wstring(), 3});
    CHECK(write_bytes(box / id / "slot0" / "save.save", save_bytes(0x5000, 0x25)));
    {
        HANDLE lock = CreateFileW((box / id / "slot0" / "save.save").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                  0, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        a = host_save_archive(save.wstring(), box.wstring());
        CHECK(a.outcome == hsv::Archive::Failed);
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    }
    CHECK(generations(exports).size() == 2);
    for (const auto& e : fs::directory_iterator(exports, ec)) {
        CHECK(e.path().filename().wstring().find(L".partial") == std::wstring::npos);
    }
    CHECK(fs::exists(box / id / "slot0" / "save.save") && fs::exists(prev / id / "slot1" / "save.save"));

    // ---- a sandbox set aside earlier is archived (and reported as deletable)
    const fs::path aside = cd / "cdmp_save_sandbox_unexported_20261001-000000";
    CHECK(write_bytes(aside / id / "slot2" / "lobby.save", save_bytes(0x186, 0x41)));
    CHECK(write_bytes(aside / id / "slot2" / "save.save", save_bytes(0x4000, 0x42)));
    a = host_save_archive(save.wstring(), box.wstring());
    CHECK(a.outcome == hsv::Archive::Done && !a.generation.empty());
    CHECK(a.archived_aside.size() == 1 && fs::path(a.archived_aside[0]) == aside);
    if (!a.generation.empty()) {
        const fs::path g = exports / a.generation;
        CHECK(generation_verifies(g));
        CHECK(fs::exists(g / "aside-20261001-000000" / id / "slot2" / "save.save"));
    }
    CHECK(fs::exists(aside));  // deleting it is prepare_sandbox's job, after the rotation
    fs::remove_all(aside, ec);  // as prepare_sandbox does

    // ---- retention: keep 2, the newest always kept
    host_save_configure({"export", exports.wstring(), 2});
    std::wstring newest;
    for (uint8_t i = 0; i < 3; ++i) {
        CHECK(write_bytes(box / id / "slot0" / "save.save", save_bytes(0x5000, static_cast<uint8_t>(0x50 + i))));
        a = host_save_archive(save.wstring(), box.wstring());
        CHECK(a.outcome == hsv::Archive::Done && !a.generation.empty());
        newest = a.generation;
    }
    gens = generations(exports);
    CHECK(gens.size() == 2 && !gens.empty() && gens.back() == newest);
    for (const std::wstring& g : gens) CHECK(generation_verifies(exports / g));

    // ---- review M2: a damaged generation is not trusted for deduplication (the sandbox did not change)
    {
        const fs::path g = exports / newest;
        const fs::path victim = g / "sandbox" / id / "slot0" / "save.save";
        std::string bytes = read_all(victim);
        CHECK(bytes.size() > 0x90);
        if (bytes.size() > 0x90) bytes[0x90] = static_cast<char>(bytes[0x90] ^ 1);
        CHECK(write_text(victim, bytes));
        a = host_save_archive(save.wstring(), box.wstring());
        CHECK(a.outcome == hsv::Archive::Done && !a.generation.empty());  // copied again, not "identical"
        if (!a.generation.empty()) {
            CHECK(generation_verifies(exports / a.generation));
            const auto m = manifest_of(exports / a.generation);
            const auto old = manifest_of(g);
            CHECK(m && !m->sources.empty() && m->sources[0].identical_to.empty());
            CHECK(m && old && m->seq > old->seq);  // the order of "newest"
        }
        CHECK(fs::exists(g));  // an invalid generation is never pruned
    }

    // ---- injected failures (manifest write, final rename): nothing half made, the sources untouched
    for (int step : {1, 2}) {
        CHECK(write_bytes(box / id / "slot0" / "save.save", save_bytes(0x5000, static_cast<uint8_t>(0x60 + step))));
        const auto before = generations(exports);
        host_save_test_inject(step);
        a = host_save_archive(save.wstring(), box.wstring());
        host_save_test_inject(0);
        CHECK(a.outcome == hsv::Archive::Failed && a.generation.empty());
        CHECK(generations(exports) == before);
        for (const auto& e : fs::directory_iterator(exports, ec)) {
            CHECK(e.path().filename().wstring().find(L".partial") == std::wstring::npos);
        }
        CHECK(fs::exists(box / id / "slot0" / "save.save") && fs::exists(prev / id / "slot1" / "save.save"));
    }

    // ---- links (review M1): inside a source, in front of the export folder, as runs\, as a generation
    const fs::path outside = root / "outside";
    CHECK(write_text(outside / "keep.txt", "keep"));
    if (make_junction(box / id / "linked", outside)) {
        a = host_save_archive(save.wstring(), box.wstring());
        CHECK(a.outcome == hsv::Archive::Failed);  // never followed
        CHECK(RemoveDirectoryW((box / id / "linked").c_str()));
        CHECK(read_all(outside / "keep.txt") == "keep");
        if (make_junction(root / "jcd", cd)) {
            host_save_configure({"export", (root / "jcd" / "exports").wstring(), 2});
            CHECK(host_save_archive(save.wstring(), box.wstring()).outcome == hsv::Archive::Failed);
            CHECK(!fs::exists(cd / "exports"));
            CHECK(RemoveDirectoryW((root / "jcd").c_str()));
        }
        const fs::path ex2 = root / "exports2";
        fs::create_directories(ex2, ec);
        if (make_junction(ex2 / "runs", outside)) {
            host_save_configure({"export", ex2.wstring(), 2});
            CHECK(host_save_archive(save.wstring(), box.wstring()).outcome == hsv::Archive::Failed);
            CHECK(RemoveDirectoryW((ex2 / "runs").c_str()));
        }
        if (make_junction(exports / "20200101-000000", outside)) {
            host_save_configure({"export", exports.wstring(), 1});
            CHECK(write_bytes(box / id / "slot0" / "save.save", save_bytes(0x5000, 0x70)));
            a = host_save_archive(save.wstring(), box.wstring());
            CHECK(a.outcome == hsv::Archive::Done && !a.generation.empty());
            CHECK(fs::exists(exports / "20200101-000000"));  // not used, not pruned
            CHECK(RemoveDirectoryW((exports / "20200101-000000").c_str()));
        }
        CHECK(read_all(outside / "keep.txt") == "keep");
    } else {
        std::printf("note: mklink /J failed here; the junction cases were skipped\n");
    }
    // An 8.3 short name of the game's folder (when the volume makes them).
    wchar_t shortp[MAX_PATH];
    const DWORD sn = GetShortPathNameW(cd.c_str(), shortp, MAX_PATH);
    if (sn && sn < MAX_PATH && _wcsicmp(shortp, cd.c_str()) != 0) {
        host_save_configure({"export", std::wstring(shortp) + L"\\exports8", 2});
        CHECK(host_save_archive(save.wstring(), box.wstring()).outcome == hsv::Archive::Failed);
        CHECK(!fs::exists(cd / "exports8"));
    } else {
        std::printf("note: no 8.3 names on this volume; that case was skipped\n");
    }

    // ---- the fake save folder byte for byte as before: export and probe only read it
    CHECK(tree_snapshot(save) == save_before);
    cdmp::log::close();
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    test_parse_mode();
    test_slots();
    test_names();
    test_retention();
    test_rebuild_plan();
    test_resolved();
    test_export_dir();
    test_manifest();
    test_trees();
    test_journal();
    test_positive_host();
    test_classify();
    test_archive();
    if (g_failures == 0) std::printf("All host save (S35 phase E) tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
