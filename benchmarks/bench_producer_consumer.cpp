#include <benchmark/benchmark.h>
#include "conduit/core.hpp"
#include <atomic>
#include <thread>

#include "conduit/core.hpp"

using namespace cre;

struct flux_event : allocated_event<flux_event, 1> {
    uint64_t seq;
};

static void BM_Conduit_SPSC_Throughput(benchmark::State& state) {
    const int total = static_cast<int>(state.range(0));

    for (auto _ : state) {
        pool<flux_event> p(total);
        conduit<flux_event, 4096> c;

        std::atomic<bool> done{ false };
        std::atomic<int> consumed{ 0 };

        std::thread producer([&]() {
            for (int i = 0; i < total; ++i) {
                auto ev = p.make();
                ev->seq = i;
                while (!c.push(ev.get())) std::this_thread::yield();
                ev.release();
            }
            done = true;
            });

        std::thread consumer([&]() {
            uint64_t expected = 0;
            while (!done || consumed < total) {
                if (auto ev = c.pop(p)) {
                    if (ev->seq != expected) {
                        state.SkipWithError("FIFO violation");
                        return;
                    }
                    expected++;
                    consumed++;
                }
            }
            });

        producer.join();
        consumer.join();

        state.SetItemsProcessed(total);
    }
}


BENCHMARK(BM_Conduit_SPSC_Throughput)
    ->Arg(1000000)
    ->Unit(benchmark::kMillisecond);
