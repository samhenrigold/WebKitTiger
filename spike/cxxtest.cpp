// C++23 runtime on Tiger: libc++ / libc++abi / libunwind, threads, TLS, exceptions, iostream.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <cstdio>
#include <format>
#include <iostream>
#include <mutex>
#include <numeric>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "throwlib.h"

static int failures = 0;
static void T(const char *what, bool ok) { std::printf("%-46s %s\n", what, ok ? "PASS" : (++failures, "FAIL")); }

static int tlsDtors = 0;
struct Tracked { int id; ~Tracked() { ++tlsDtors; } };
static thread_local Tracked tracked{7};

static void deepThrow(int depth) { if (depth == 0) throw std::runtime_error("deep"); deepThrow(depth - 1); }
static void throwLogic() { throw std::logic_error("from exe"); }

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    // --- containers, strings, formatting ---
    std::vector<int> v(10);
    std::iota(v.begin(), v.end(), 1);
    std::unordered_map<std::string, int> m;
    for (int x : v) m[std::format("k{}", x)] = x * x;
    T("std::format + unordered_map", m.size() == 10 && m["k7"] == 49);
    T("std::format widths", std::format("{:>6.2f}|{:#x}", 3.14159, 255) == "  3.14|0xff");

    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof buf, 1234567);
    T("std::to_chars", ec == std::errc{} && std::string(buf, ptr) == "1234567");

    std::span<int> s{v};
    T("std::span", s.size() == 10 && s[3] == 4 && s.subspan(5).front() == 6);

    auto evens = v | std::views::filter([](int x) { return x % 2 == 0; }) | std::views::transform([](int x) { return x * 10; });
    std::vector<int> collected(evens.begin(), evens.end());
    T("ranges views", collected == std::vector<int>({20, 40, 60, 80, 100}));

    std::string joined;
    for (auto &&part : std::views::iota(1, 4)) joined += std::to_string(part);
    T("views::iota", joined == "123");

    // --- iostream ---
    std::ostringstream oss;
    oss << "n=" << 42 << ' ' << std::boolalpha << true;
    T("iostream formatting", oss.str() == "n=42 true");
    std::cout << "  (cout works)" << std::endl;

    // --- atomics, threads, condvar, TLS ---
    std::atomic<uint64_t> counter{0};
    T("atomic<uint64_t> is lock free or works", (counter.fetch_add(1ull << 33), counter.load() == (1ull << 33)));

    std::mutex mtx;
    std::condition_variable cv;
    bool ready = false;
    int observedTls = 0;
    std::thread worker([&] {
        tracked.id = 99;
        observedTls = tracked.id;
        for (int i = 0; i < 1000; ++i) counter.fetch_add(1, std::memory_order_relaxed);
        { std::lock_guard<std::mutex> lk(mtx); ready = true; }
        cv.notify_one();
    });
    {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&] { return ready; });
    }
    worker.join();
    T("thread + mutex + condition_variable", ready && counter.load() == (1ull << 33) + 1000);
    T("thread_local is per thread", observedTls == 99 && tracked.id == 7);
    T("thread_local destructor ran on join", tlsDtors == 1);

    // --- exceptions ---
    bool caught = false;
    try { deepThrow(8); } catch (const std::runtime_error &e) { caught = std::string(e.what()) == "deep"; }
    T("throw/catch across 9 frames", caught);

    try { throw 5; } catch (const std::exception &) { T("exception type matching", false); } catch (int i) { T("exception type matching", i == 5); }

    int rethrown = 0;
    try {
        try { throw std::out_of_range("inner"); } catch (...) { rethrown = 1; throw; }
    } catch (const std::out_of_range &) { rethrown = 2; }
    T("rethrow", rethrown == 2);

    caught = false;
    try { throwFromLib(1); } catch (const LibError &e) { caught = std::string(e.what()) == "thrown in dylib"; }
    T("exception thrown in a dylib, caught in exe", caught);
    T("exception thrown in exe, caught in dylib", catchInLib(&throwLogic) == 1);

    // Unwinding must run destructors in both images.
    static int unwound = 0;
    struct Guard { ~Guard() { ++unwound; } };
    try { Guard g; throwFromLib(1); } catch (...) {}
    T("destructors run while unwinding", unwound == 1);

    std::printf(failures ? "\nFAILURES: %d\n" : "\nALL PASS\n", failures);
    return failures != 0;
}
