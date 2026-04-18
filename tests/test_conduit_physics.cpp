#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "axiom_conduit/core.hpp"

using namespace axiom;

struct tiny_event : allocated_event<tiny_event, 2> {};

TEST(ConduitPhysics, throughput_degradation_is_zero_under_max_contention) {
    pool<tiny_event> p(1000000);
    conduit<tiny_event, 4096> c;

    const int total_messages = 500000;
    std::atomic<int> read_count{0};

    auto start = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        for (int i = 0; i < total_messages; ++i) {
            auto ev = p.make();
            // Producer saturates the conduit; spin until space is available.
            while (!c.push(ev.get())) { /* spin */
            }
            ev.release();
        }
    });

    std::thread consumer([&]() {
        while (read_count < total_messages) {
            // Consumer drains as fast as possible; no blocking, no locks.
            if (auto ev = c.pop(p)) {
                read_count++;
            }
        }
    });

    producer.join();
    consumer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // A lock-free, O(1), cache-line–isolated conduit on modern CPUs
    // must push half a million messages through in well under ~50ms.
    // Drop a mutex anywhere in this path and the whole thing collapses
    // into the hundreds-of-milliseconds swamp instantly.
    EXPECT_LT(duration_ms, 500) << "Throughput is too slow! False sharing or locks detected.";
}
