/* KallistiOS ##version##

   Verify the installed C++ clock implementation against KOS clock domains.
   Copyright (C) 2026 Joseph Black
*/

#include <kos.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

/* These declarations alone cannot fix an old archive: the runtime checks
   below also exercise the clock implementation actually linked into the ELF. */
#if !defined(_GLIBCXX_USE_CLOCK_MONOTONIC) || \
    !defined(_GLIBCXX_USE_CLOCK_REALTIME) || \
    !defined(_GLIBCXX_USE_NANOSLEEP) || !defined(_GLIBCXX_USE_SCHED_YIELD) || \
    !defined(_GLIBCXX_HAS_GTHREADS)
#error Rebuild libstdc++ with the KOS clock patches and Newlib header fixup
#endif

static bool clock_ns(clockid_t id, int64_t &value) {
    timespec time{};
    if(clock_gettime(id, &time) != 0) {
        std::perror("clock_gettime");
        return false;
    }
    value = static_cast<int64_t>(time.tv_sec) * 1000000000 + time.tv_nsec;
    return true;
}

template<typename Clock>
static bool check_clock(clockid_t id, const char *name) {
    using namespace std::chrono;
    unsigned submicrosecond_samples = 0;
    int64_t previous = INT64_MIN;

    for(unsigned i = 0; i < 256; ++i) {
        int64_t before, after;
        if(!clock_ns(id, before))
            return false;
        const auto now = duration_cast<nanoseconds>(
            Clock::now().time_since_epoch()).count();
        if(!clock_ns(id, after))
            return false;

        /* A finer representation is not enough. Both clocks must use the
           same epoch and the C++ reading must fall between the C readings. */
        if(now < before || now > after || now < previous) {
            std::printf("FAIL %s: before=%lld chrono=%lld after=%lld\n", name,
                        static_cast<long long>(before),
                        static_cast<long long>(now),
                        static_cast<long long>(after));
            return false;
        }
        previous = now;
        submicrosecond_samples += now % 1000 != 0;
    }
    std::printf("%s: domain/range PASS; non-microsecond samples=%u/256\n",
                name, submicrosecond_samples);
    /* Do not infer physical resolution from an emulator or require a timer
       sample distribution that depends on instruction timing and workload. */
    return true;
}

int main() {
    using namespace std::chrono;
    static_assert(steady_clock::is_steady);
    if(!check_clock<system_clock>(CLOCK_REALTIME, "system_clock") ||
       !check_clock<steady_clock>(CLOCK_MONOTONIC, "steady_clock"))
        return 1;

    const auto start = steady_clock::now();
    std::this_thread::sleep_for(20ms);
    const auto elapsed = steady_clock::now() - start;
    if(elapsed < 20ms) {
        std::puts("FAIL sleep_for returned before its deadline");
        return 1;
    }
    std::this_thread::yield();
    std::atomic<unsigned> completed{0};
    std::thread worker([&completed] {
        completed.fetch_add(1, std::memory_order_relaxed);
    });
    worker.join();
    if(completed.load(std::memory_order_relaxed) != 1) {
        std::puts("FAIL C++ thread/join");
        return 1;
    }
    std::printf("sleep_for: %lld us\n",
                static_cast<long long>(duration_cast<microseconds>(elapsed).count()));
    std::puts("RESULT: PASS (C++ clocks, sleep, yield and threads; not hardware precision)");
    return 0;
}
