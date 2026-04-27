#include "conduit/core.hpp"
#include <benchmark/benchmark.h>
#include <cstdint>

using namespace cre;

struct wrap_event : allocated_event<wrap_event, 1> {
    uint64_t value;
};

static void BM_Conduit_WrapAround(benchmark::State& state) {
    const int N = 1'000'000;

    pool<wrap_event> p(N + 16);
    conduit<wrap_event, 1024> c;

    for (auto _ : state) {
        for (int i = 0; i < N; ++i) {
            auto e = p.make();
            e->value = i;

            // itt NEM szabad spinelni, ennek mindig sikerülnie kell
            bool ok = c.push(e.get());
            benchmark::DoNotOptimize(ok);
            e.release();

            auto ev = c.pop(p);
            // itt is elvárjuk, hogy mindig legyen mit kivenni
            benchmark::DoNotOptimize(ev);
        }
    }

    state.SetItemsProcessed(N);
}


BENCHMARK(BM_Conduit_WrapAround);
