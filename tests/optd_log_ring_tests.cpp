// P8 session 8, the asynchronous logger (src/mod/log_ring.h, src/mod/log.cpp;
// docs/research/pendientes/p8-impl-log-async.md).
//
// SCOPE: the ring's logic offline (order, whole lines, wrap, bounds, drop and count, the "dropped here" note,
// cut lines, raw records, the exit drain) and, with concurrent producers, that a writer blocked in its "write"
// does not hold the producers; then log.cpp itself on a file in %TEMP% (rotation, order across threads, flush,
// the emergency line after what was queued, close). Not the game, not Shadow's disk.

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "mod/log.h"
#include "mod/log_names.h"
#include "mod/log_ring.h"

using cdmp::log::LineRing;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

template <class R>
std::string take_all(R& r) {
    const auto rd = r.readable();
    std::string s(rd.a.p ? rd.a.p : "", rd.a.n);
    if (rd.b.n) s.append(rd.b.p, rd.b.n);
    r.consume(rd.end);
    return s;
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < s.size()) {
        const size_t e = s.find("\r\n", at);
        if (e == std::string::npos) {
            out.push_back(s.substr(at));
            break;
        }
        out.push_back(s.substr(at, e - at));
        at = e + 2;
    }
    return out;
}

using Small = LineRing<4096, std::mutex>;

void test_order_and_whole_lines() {
    auto r = std::make_unique<Small>();
    CHECK(r->push("[p] ", "one"));
    CHECK(r->push("[p] ", "two"));
    CHECK(r->push("[p] ", "three"));
    CHECK(r->lines() == 3);
    CHECK(r->queued_bytes() == (4 + 3 + 2) + (4 + 3 + 2) + (4 + 5 + 2));
    const std::string s = take_all(*r);
    CHECK(s == "[p] one\r\n[p] two\r\n[p] three\r\n");
    CHECK(r->queued_bytes() == 0);
    CHECK(take_all(*r).empty());
}

void test_wrap() {
    auto r = std::make_unique<Small>();
    // Many rounds of a 4 KB ring: every line comes back whole and in order, across the wrap.
    int next_expected = 0, pushed = 0;
    for (int round = 0; round < 200; ++round) {
        for (int i = 0; i < 7; ++i) {
            const std::string text = "line " + std::to_string(pushed) + std::string(static_cast<size_t>(pushed % 97), 'x');
            CHECK(r->push("P ", text));
            ++pushed;
        }
        for (const std::string& l : split_lines(take_all(*r))) {
            const std::string want = "P line " + std::to_string(next_expected) +
                                     std::string(static_cast<size_t>(next_expected % 97), 'x');
            CHECK(l == want);
            ++next_expected;
        }
    }
    CHECK(next_expected == pushed);
    CHECK(r->pushed_bytes() > 4096 * 10);  // it did wrap many times
    CHECK(r->dropped_total() == 0);
}

void test_full_drops_and_note() {
    auto r = std::make_unique<Small>();
    const std::string big(400, 'a');
    int ok = 0;
    for (int i = 0; i < 20; ++i) ok += r->push("L ", big) ? 1 : 0;
    CHECK(ok == 10);  // 10 x 404 bytes fit in 4096, the 11th does not
    CHECK(r->dropped_total() == 10);
    CHECK(r->dropped_pending() == 10);
    CHECK(r->note_due());
    // Still full: a line with a note prefix waits (dropped too), never half written.
    CHECK(!r->push("L ", "after", "W "));
    CHECK(r->dropped_total() == 11);
    const auto before = split_lines(take_all(*r));
    CHECK(before.size() == 10);
    // Room again: the note goes first, exactly where the lines were lost, then the line.
    CHECK(r->push("L ", "after", "W "));
    CHECK(!r->note_due());
    const auto after = split_lines(take_all(*r));
    CHECK(after.size() == 2);
    CHECK(after.size() == 2 && after[0] == "W Log: 11 line(s) dropped here (the log ring was full; 11 in all)");
    CHECK(after.size() == 2 && after[1] == "L after");
    // Without a note prefix the note waits for the next line that has one (or the writer's push_drop_note).
    for (int i = 0; i < 11; ++i) r->push("L ", big);
    CHECK(r->dropped_pending() == 1);
    take_all(*r);
    CHECK(r->push("L ", "no prefix given"));
    CHECK(r->note_due());
    CHECK(r->push_drop_note("N "));
    CHECK(!r->note_due());
    const auto last = split_lines(take_all(*r));
    CHECK(last.size() == 2 && last[0] == "L no prefix given");
    CHECK(last.size() == 2 && last[1] == "N Log: 1 line(s) dropped here (the log ring was full; 12 in all)");
    CHECK(!r->push_drop_note("N "));  // nothing pending: nothing written
    CHECK(take_all(*r).empty());
}

void test_long_line_cut() {
    auto r = std::make_unique<Small>();
    const std::string huge(5000, 'z');
    CHECK(r->push("H ", huge));
    const std::string s = take_all(*r);
    CHECK(s.size() <= Small::kMaxLine);
    CHECK(s.rfind("H zzz", 0) == 0);
    CHECK(s.find(" [line cut: ") != std::string::npos);
    CHECK(s.size() >= 2 && s.substr(s.size() - 2) == "\r\n");
    // The cut count is exact: kept + cut = 5000.
    const size_t kept = s.find(" [line cut: ") - 2;
    const size_t cut = std::stoul(s.substr(s.find(" [line cut: ") + 12));
    CHECK(kept + cut == huge.size());
}

void test_raw_records() {
    auto r = std::make_unique<Small>();
    const char hdr[4] = {1, 2, 3, 4};
    const std::string body(100, 'b');
    CHECK(r->push_bytes(std::string_view(hdr, 4), body));
    const std::string s = take_all(*r);
    CHECK(s.size() == 104 && std::memcmp(s.data(), hdr, 4) == 0 && s.substr(4) == body);
    int ok = 0;
    for (int i = 0; i < 100; ++i) ok += r->push_bytes(std::string(1000, 'r')) ? 1 : 0;
    CHECK(ok == 4);
    CHECK(r->dropped_total() == 96);
    CHECK(!r->note_due());  // raw records never get a note in their file
}

// A lock that fails the test if a thread marked "consumer" takes it: the consumer side (writer, crash paths,
// the exit drain) must never wait for the producers' lock (review of 10-10: a faulting producer may hold it).
thread_local bool t_consumer = false;
std::atomic<int> g_consumer_locks{0};
struct WatchedLock {
    std::mutex m;
    void lock() {
        if (t_consumer) g_consumer_locks.fetch_add(1);
        m.lock();
    }
    void unlock() { m.unlock(); }
};

void test_consumer_takes_no_lock() {
    using R = LineRing<4096, WatchedLock>;
    auto r = std::make_unique<R>();
    r->push("E ", "queued at exit");
    r->push("E ", "second");
    t_consumer = true;
    const auto rd = r->readable();
    CHECK(std::string(rd.a.p, rd.a.n) == "E queued at exit\r\nE second\r\n");
    r->consume(rd.end);
    CHECK(r->queued_bytes() == 0);
    CHECK(r->try_begin_drain());
    CHECK(r->draining() && !r->try_begin_drain());
    r->end_drain();
    t_consumer = false;
    CHECK(g_consumer_locks.load() == 0);
}

// Two consumers that raced (the exit drain and a writer still finishing): the tail never goes backwards.
void test_consume_monotonic() {
    auto r = std::make_unique<Small>();
    for (int i = 0; i < 5; ++i) r->push("M ", "line " + std::to_string(i));
    const auto all = r->readable();
    const auto first = all.begin + 10;
    r->consume(all.end);
    CHECK(r->consumed_bytes() == all.end);
    r->consume(first);  // the slower consumer finishing later
    CHECK(r->consumed_bytes() == all.end);
    CHECK(r->queued_bytes() == 0);
    CHECK(r->readable().size() == 0);
}

// A bounded pass ends after a whole line; the rest follows in the next pass; raw records may keep a reserve.
void test_bounded_pass_and_reserve() {
    auto r = std::make_unique<Small>();
    for (int i = 0; i < 10; ++i) r->push("B ", "line number " + std::to_string(i));  // 2 + 13/14 + 2 bytes each
    std::string out;
    int passes = 0;
    while (r->queued_bytes()) {
        const auto rd = r->readable(40, /*whole_lines=*/true);
        CHECK(rd.size() > 0 && rd.size() <= 40);
        std::string chunk(rd.a.p, rd.a.n);
        if (rd.b.n) chunk.append(rd.b.p, rd.b.n);
        CHECK(chunk.size() >= 2 && chunk.substr(chunk.size() - 2) == "\r\n");  // whole lines only
        out += chunk;
        r->consume(rd.end);
        ++passes;
    }
    CHECK(passes >= 5);
    CHECK(split_lines(out).size() == 10);
    // A line longer than the pass limit: the pass is cut at the limit (no '\n' to stop at).
    r->push("X ", std::string(100, 'x'));
    CHECK(r->readable(40, true).size() == 40);
    take_all(*r);
    // Reserve: ordinary records leave it free, a note may use it.
    auto q = std::make_unique<Small>();
    int ok = 0;
    for (int i = 0; i < 10; ++i) ok += q->push_bytes(std::string(1000, 'r'), {}, 1024) ? 1 : 0;
    CHECK(ok == 3);  // 4 x 1000 would fit, but not with 1024 kept free
    CHECK(q->push_bytes(std::string(1000, 'n')));  // the note uses the reserve
    CHECK(q->queued_bytes() == 4000);
}

// A writer stuck 1 s in its "WriteFile" (P8 session 8: 8.8 s) does not hold the producers: they only wait for
// the lock, which the writer does not hold while it writes. Lines that do not fit are dropped and counted; the
// kept ones are whole and in each thread's order; the notes account for every drop.
void test_blocked_writer_does_not_hold_producers() {
    using Mid = LineRing<64 * 1024, std::mutex>;
    auto r = std::make_unique<Mid>();
    constexpr int kThreads = 4, kLines = 20000;
    std::atomic<bool> go{false};
    std::atomic<int> done{0};
    std::atomic<long long> worst_us{0};
    std::string out;
    std::thread writer([&] {
        while (!go.load()) std::this_thread::yield();
        bool slept = false;
        for (;;) {
            const bool finished = done.load() == kThreads;
            CHECK(r->try_begin_drain());
            const auto rd = r->readable();
            if (rd.size() && !slept) {
                slept = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // the slow disk, holding NO lock
            }
            out.append(rd.a.p ? rd.a.p : "", rd.a.n);
            if (rd.b.n) out.append(rd.b.p, rd.b.n);
            r->consume(rd.end);
            if (r->note_due()) r->push_drop_note("W ");
            r->end_drain();
            if (finished && r->queued_bytes() == 0 && !r->note_due()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&, t] {
            while (!go.load()) std::this_thread::yield();
            const std::string prefix = "T" + std::to_string(t) + " ";
            for (int i = 0; i < kLines; ++i) {
                const std::string text = std::to_string(i);
                const auto t0 = std::chrono::steady_clock::now();
                r->push(prefix, text, "W ");
                const auto us =
                    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
                long long cur = worst_us.load();
                while (us > cur && !worst_us.compare_exchange_weak(cur, us)) {
                }
            }
            done.fetch_add(1);
        });
    }
    go.store(true);
    for (auto& p : producers) p.join();
    writer.join();

    // The writer slept 1 s with a full ring: no producer waited for it.
    std::printf("  log ring: worst push %lld us with the writer blocked 1 s; dropped %llu of %d\n", worst_us.load(),
                static_cast<unsigned long long>(r->dropped_total()), kThreads * kLines);
    CHECK(worst_us.load() < 100000);  // 100 ms: scheduling noise only, far below the 1 s block
    CHECK(r->dropped_total() > 0);    // the ring (64 KB) did fill while the writer slept
    int last[kThreads];
    std::fill(std::begin(last), std::end(last), -1);
    uint64_t kept = 0, noted = 0;
    for (const std::string& l : split_lines(out)) {
        if (l.rfind("W Log: ", 0) == 0) {
            noted += std::stoull(l.substr(7));
            continue;
        }
        CHECK(l.size() >= 3 && l[0] == 'T');
        const int t = l[1] - '0';
        const int i = std::stoi(l.substr(3));
        CHECK(t >= 0 && t < kThreads && i > last[t]);  // each thread's order kept (gaps only where dropped)
        if (t >= 0 && t < kThreads) last[t] = i;
        ++kept;
    }
    CHECK(kept + r->dropped_total() == static_cast<uint64_t>(kThreads) * kLines);
    CHECK(noted == r->dropped_total());
}

// ---------------------------------------------------------------- log.cpp on a real file

std::string read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void test_logger_file() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("cdmp_log_test_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path path = dir / "cdmp.log";
    {
        std::ofstream old(path, std::ios::binary);  // a plain cdmp.log of a mod before 10-10
        old << "previous session\r\n";
    }
    // Eleven older runs, a sniffer capture and an old numbered log: only the dated logs are pruned.
    for (int i = 0; i < 11; ++i) {
        std::ofstream o(dir / std::format("cdmp_20000101-0000{:02}.log", i), std::ios::binary);
        o << "run " << i;
    }
    { std::ofstream o(dir / "cdmp_packets_20000101-000000.log", std::ios::binary); o << "packets"; }
    { std::ofstream o(dir / "cdmp.1.log", std::ios::binary); o << "legacy"; }
    CHECK(!cdmp::log::is_open());
    CHECK(cdmp::log::current_path().empty());
    cdmp::log::info("not open: {}", 1);  // dropped silently, nothing to write to
    // The oldest run is still open in another process (an earlier game still writing it): never deleted.
    const HANDLE held = CreateFileW((dir / "cdmp_20000101-000000.log").c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    CHECK(cdmp::log::open(path.wstring()));
    CHECK(cdmp::log::is_open());
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    const fs::path first_dated = cdmp::log::current_path();
    CHECK(first_dated.parent_path() == dir);
    CHECK(cdmp::log::names::is_dated(first_dated.filename().wstring(), L"cdmp", L".log"));
    {
        // The old plain cdmp.log kept its content under a dated name; the three oldest of 13 are pruned except the
        // one still open; the rest untouched.
        size_t dated = 0;
        bool previous_kept = false, run0 = false, run1 = false, run2 = false, run3 = false;
        for (const auto& e : fs::directory_iterator(dir)) {
            const std::wstring n = e.path().filename().wstring();
            if (!cdmp::log::names::is_dated(n, L"cdmp", L".log")) continue;
            ++dated;
            previous_kept = previous_kept || read_file(e.path()) == "previous session\r\n";
            run0 = run0 || n == L"cdmp_20000101-000000.log";
            run1 = run1 || n == L"cdmp_20000101-000001.log";
            run2 = run2 || n == L"cdmp_20000101-000002.log";
            run3 = run3 || n == L"cdmp_20000101-000003.log";
        }
        CHECK(dated == cdmp::log::names::kKeepLogs + 1);
        CHECK(previous_kept);
        CHECK(run0 && !run1 && !run2 && run3);  // 11 old + the retired cdmp.log + this run = 13; 000000 was open
        CHECK(read_file(dir / "cdmp_20000101-000000.log") == "run 0");
        CHECK(read_file(dir / "cdmp_packets_20000101-000000.log") == "packets");
        CHECK(read_file(dir / "cdmp.1.log") == "legacy");
    }

    constexpr int kThreads = 4, kLines = 2000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([t] {
            for (int i = 0; i < kLines; ++i) cdmp::log::info("thread {} line {}", t, i);
        });
    }
    for (auto& t : ts) t.join();
    // A line longer than the 1 KB stack buffer takes the std::format path.
    cdmp::log::warn("long {}", std::string(3000, 'q'));
    CHECK(cdmp::log::flush(5000));
    cdmp::log::emergency(cdmp::log::Level::Error, "emergency after the queued lines");
    cdmp::log::close();
    CHECK(!cdmp::log::is_open());

    const auto lines = split_lines(read_file(path));
    int last[kThreads];
    std::fill(std::begin(last), std::end(last), -1);
    int seen = 0;
    bool long_seen = false;
    size_t emergency_at = 0, last_info_at = 0;
    for (size_t n = 0; n < lines.size(); ++n) {
        const std::string& l = lines[n];
        CHECK(l.size() > 16 && l[0] == '[' && l[13] == ']');  // [hh:mm:ss.mmm] TAG [Ttid] ...
        if (const size_t at = l.find("] thread "); at != std::string::npos) {
            int t = -1, i = -1;
            sscanf_s(l.c_str() + at + 9, "%d line %d", &t, &i);
            CHECK(t >= 0 && t < kThreads && i == last[t] + 1);  // nothing dropped, each thread in order
            if (t >= 0 && t < kThreads) last[t] = i;
            ++seen;
            last_info_at = n;
        }
        if (l.find(" WRN ") != std::string::npos && l.find("long qqq") != std::string::npos) {
            long_seen = l.size() > 3000;
        }
        if (l.find("emergency after the queued lines") != std::string::npos) emergency_at = n;
    }
    CHECK(seen == kThreads * kLines);
    CHECK(long_seen);
    CHECK(emergency_at > last_info_at);

    // cdmp.log is the same file (a hard link), not a copy.
    const std::string first_log = read_file(path);
    CHECK(!first_log.empty() && read_file(first_dated) == first_log);

    // Reopen after a close (review of 10-10: open() is not one-shot), as a second launch would: a NEW dated file
    // (the same second gets _2), cdmp.log moves to it, the first run's log is untouched.
    CHECK(cdmp::log::open(path.wstring()));
    CHECK(cdmp::log::open(path.wstring()));  // already open: true, nothing new
    const fs::path second_dated = cdmp::log::current_path();
    CHECK(second_dated != first_dated);
    CHECK(read_file(first_dated) == first_log);
    cdmp::log::info("second session {}", 2);
    CHECK(cdmp::log::flush(5000));
    cdmp::log::close();
    const auto second = split_lines(read_file(path));
    CHECK(second.size() == 1 && second[0].find("] second session 2") != std::string::npos);
    CHECK(read_file(second_dated) == read_file(path));
    // Names: a second run in the same second, sort by number, other files never touched.
    namespace nm = cdmp::log::names;
    CHECK(nm::stamp(2026, 10, 10, 18, 39, 2) == L"20261010-183902");
    CHECK(nm::dated(L"cdmp", L"20261010-183902", 1, L".log") == L"cdmp_20261010-183902.log");
    CHECK(nm::dated(L"cdmp", L"20261010-183902", 2, L".log") == L"cdmp_20261010-183902_2.log");
    CHECK(nm::is_dated(L"cdmp_20261010-183902_12.log", L"cdmp", L".log"));
    CHECK(!nm::is_dated(L"cdmp_packets_20261010-183902.log", L"cdmp", L".log"));
    CHECK(!nm::is_dated(L"cdmp_20261010-183902_.log", L"cdmp", L".log"));
    CHECK(!nm::is_dated(L"cdmp_20261010_183902.log", L"cdmp", L".log"));
    CHECK(!nm::is_dated(L"cdmp.log", L"cdmp", L".log"));
    const auto del = nm::to_delete({L"cdmp_20261010-183902_10.log", L"cdmp_20261010-183902_9.log", L"x.log",
                                    L"cdmp_20261010-183901.log", L"cdmp_20261010-183902.log"},
                                   L"cdmp", L".log", 2);
    CHECK(del.size() == 2 && del[0] == L"cdmp_20261010-183901.log" && del[1] == L"cdmp_20261010-183902.log");
    // This run's file is never returned, even when its stamp sorts first (the clock was set back).
    const auto del2 = nm::to_delete({L"cdmp_20261010-183902.log", L"cdmp_20991231-000000.log",
                                     L"cdmp_20991231-000001.log", L"cdmp_20991231-000002.log"},
                                    L"cdmp", L".log", 2, L"cdmp_20261010-183902.log");
    CHECK(del2.size() == 1 && del2[0] == L"cdmp_20991231-000000.log");

    // The exit drain with the writer ALIVE (review of 10-10: not assumed dead): every line written, whole
    // (a line may appear twice if both wrote it), the tail never backwards; the writer starts no new pass.
    CHECK(cdmp::log::open(path.wstring()));
    std::thread busy([] {
        for (int i = 0; i < 3000; ++i) cdmp::log::info("exit line {}", i);
    });
    for (int i = 0; i < 3000; ++i) cdmp::log::info("main line {}", i);
    busy.join();
    cdmp::log::flush_at_exit();
    const auto third = split_lines(read_file(path));
    std::vector<int> exit_seen(3000, 0), main_seen(3000, 0);
    for (const std::string& l : third) {
        CHECK(l.size() > 16 && l[0] == '[' && l[13] == ']');  // nothing torn
        int i = -1;
        if (const size_t at = l.find("] exit line "); at != std::string::npos) {
            i = std::stoi(l.substr(at + 12));
            if (i >= 0 && i < 3000) ++exit_seen[static_cast<size_t>(i)];
        } else if (const size_t at2 = l.find("] main line "); at2 != std::string::npos) {
            i = std::stoi(l.substr(at2 + 12));
            if (i >= 0 && i < 3000) ++main_seen[static_cast<size_t>(i)];
        }
    }
    CHECK(std::all_of(exit_seen.begin(), exit_seen.end(), [](int n) { return n >= 1; }));
    CHECK(std::all_of(main_seen.begin(), main_seen.end(), [](int n) { return n >= 1; }));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace

int main() {
    test_order_and_whole_lines();
    test_wrap();
    test_full_drops_and_note();
    test_long_line_cut();
    test_raw_records();
    test_consumer_takes_no_lock();
    test_consume_monotonic();
    test_bounded_pass_and_reserve();
    test_blocked_writer_does_not_hold_producers();
    test_logger_file();
    if (g_failures) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("log ring tests passed\n");
    return 0;
}
