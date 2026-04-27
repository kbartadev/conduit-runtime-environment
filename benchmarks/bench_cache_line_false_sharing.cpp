#include <benchmark/benchmark.h>
#include <atomic>
#include <thread>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <immintrin.h>

#include "conduit/core.hpp"

static void BM_Conduit_FalseSharing(benchmark::State& state) {
    struct alignas(64) slot {
        std::atomic<uint64_t> v{0};
    };

    slot s[2];

    for (auto _ : state) {
        std::thread t1([&]() {
            for (int i = 0; i < 5'000'000; ++i)
                s[0].v.fetch_add(1, std::memory_order_relaxed);
        });
        std::thread t2([&]() {
            for (int i = 0; i < 5'000'000; ++i)
                s[1].v.fetch_add(1, std::memory_order_relaxed);
        });

        t1.join();
        t2.join();
    }
}

BENCHMARK(BM_Conduit_FalseSharing);
