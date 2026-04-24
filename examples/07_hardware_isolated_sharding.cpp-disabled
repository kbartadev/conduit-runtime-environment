#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "conduit/core.hpp"

using namespace cre;

struct order_event : allocated_event<order_event, 55> {
    int order_id;
    order_event(int id) : order_id(id) {}
};

// Worker thread logic for a single shard.
// Each shard owns its conduit; no cross-shard contention.
void trading_shard(int shard_id, conduit<order_event, 4096>& in_pipe, pool<order_event>& p,
                   std::atomic<int>& counter) {
    while (true) {
        if (auto ev = in_pipe.pop(p)) {
            if (ev->order_id == -1) break;  // Termination sentinel.
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    }
    std::cout << "[Shard " << shard_id << "] Shutting down.\n";
}

int main() {
    runtime_domain<order_event> domain;
    auto& order_pool = domain.get_pool<order_event>();

    // 1. Physical hardware-isolated conduits with L1 cache locality.
    // Each shard gets its own hot path; no false sharing, no shared queues.
    conduit<order_event, 4096> shard_0_pipe;
    conduit<order_event, 4096> shard_1_pipe;

    // 2. Deterministic O(1) load balancer.
    // No modulo, no hashing, no collisions — just pure round-robin physics.
    round_robin_switch<order_event, 2, 4096> balancer;
    balancer.bind_track(0, shard_0_pipe);
    balancer.bind_track(1, shard_1_pipe);

    cluster<256> core_router;
    core_router.bind<order_event>(balancer);

    std::atomic<int> processed_count{0};

    // 3. Launch shard worker threads.
    std::thread t0(trading_shard, 0, std::ref(shard_0_pipe), std::ref(order_pool),
                   std::ref(processed_count));
    std::thread t1(trading_shard, 1, std::ref(shard_1_pipe), std::ref(order_pool),
                   std::ref(processed_count));

    // 4. Flux generation: pump 1,000,000 events with zero dynamic allocation.
    std::cout << "[Main] Pumping 1,000,000 orders into the lock-free cluster...\n";
    auto start = std::chrono::high_resolution_clock::now();

    // Since the runtime_domain pool is fixed at 1024 entries, the benchmark
    // uses a dedicated large pool that can sustain the required load.
    pool<order_event> massive_pool(1048576);

    // Fire-and-forget dispatch. If the worker threads fall behind,
    // the switch deterministically drops packets (Backpressure).
    for (int i = 0; i < 1000000; ++i) {
        core_router.send(massive_pool.make(i));
    }

    // Send termination sentinels.
    while (!shard_0_pipe.push(domain.make<order_event>(-1).release())) std::this_thread::yield();
    while (!shard_1_pipe.push(domain.make<order_event>(-1).release())) std::this_thread::yield();

    t0.join();
    t1.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "[Main] Processed " << processed_count.load() << " orders in " << ms << " ms.\n";

    return 0;
}
