#include <benchmark/benchmark.h>
#include <atomic>
#include <thread>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <immintrin.h>

#include "conduit/core.hpp"

using namespace cre;

struct ev : allocated_event<ev, 1> { int x = 0; };

struct A : handler_base<A> { void on(event_ptr<ev>& e) { e->x += 1; } };
struct B : handler_base<B> { void on(event_ptr<ev>& e) { e->x += 2; } };
struct C : handler_base<C> { void on(event_ptr<ev>& e) { e->x += 3; } };

static void BM_Conduit_Pipeline(benchmark::State& state) {
    pool<ev> p(1024);
    A a; B b; C c;
    pipeline<A,B,C> pipe(a,b,c);

    for (auto _ : state) {
        auto e = p.make();
        pipe.dispatch(e);
        benchmark::DoNotOptimize(e);
    }
}

BENCHMARK(BM_Conduit_Pipeline);
