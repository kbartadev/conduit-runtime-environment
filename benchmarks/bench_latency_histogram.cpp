#include <benchmark/benchmark.h>
#include "conduit/core.hpp"
#include <vector>
#include <chrono>

#include "conduit/core.hpp"

using namespace cre;

struct hist_event : allocated_event<hist_event, 1> {
    uint64_t t0;
};

static void BM_Conduit_LatencyHistogram(benchmark::State& state) {
    pool<hist_event> p(1024);
    conduit<hist_event, 1024> c;

    std::vector<uint64_t> samples;
    samples.reserve(state.range(0));

    for (auto _ : state) {
        auto ev = p.make();
        ev->t0 = __rdtsc();
        c.push(ev.get());
        ev.release();

        auto out = c.pop(p);
        uint64_t dt = __rdtsc() - out->t0;
        samples.push_back(dt);
    }

    std::sort(samples.begin(), samples.end());
    auto pct = [&](double x) {
        return samples[size_t(samples.size() * x)];
    };

    state.counters["p50"]   = pct(0.50);
    state.counters["p90"]   = pct(0.90);
    state.counters["p99"]   = pct(0.99);
    state.counters["p999"]  = pct(0.999);
}

BENCHMARK(BM_Conduit_LatencyHistogram)->Arg(200000);
