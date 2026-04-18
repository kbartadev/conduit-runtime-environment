#include <benchmark/benchmark.h>

#include "axiom_conduit/core.hpp"

using namespace axiom;

struct bench_event : allocated_event<bench_event, 1> {
    int payload[16];  // 64 bytes (Exactly one cache line)
};

// Measures how long a single O(1) allocation and push into the ring buffer takes
static void BM_Conduit_Push(benchmark::State& state) {
    pool<bench_event> p(state.range(0));
    conduit<bench_event, 8192> c;

    // Pre-allocate everything so we measure only PUSH speed, not allocation
    std::vector<bench_event*> pre_allocated;
    for (int i = 0; i < state.range(0) - 1; ++i) {
        pre_allocated.push_back(p.make().release());
    }

    int idx = 0;
    for (auto _ : state) {
        c.push(pre_allocated[idx++ % pre_allocated.size()]);
    }
}
// Scale the test from 1000 to 8000 elements
BENCHMARK(BM_Conduit_Push)->Range(1024, 8192);

// Measures the full lifecycle: O(1) Allocation -> Push -> Pop -> O(1) Deallocation
static void BM_Conduit_FullFlux(benchmark::State& state) {
    pool<bench_event> p(1024);
    conduit<bench_event, 1024> c;

    for (auto _ : state) {
        c.push(p.make().release());
        auto ev = c.pop(p);
        benchmark::DoNotOptimize(ev);
    }
}
BENCHMARK(BM_Conduit_FullFlux);

BENCHMARK_MAIN();
