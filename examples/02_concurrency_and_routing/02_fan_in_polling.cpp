#include <iostream>
#include <thread>
#include <atomic>
#include "conduit/core.hpp"

using namespace cre;

struct task_event : allocated_event<task_event, 42> {
    int job_id;
    task_event(int id) : job_id(id) {}
};

// Worker thread: Has its own conduit, no mutex, no lock!
void shard_worker(int shard_id, conduit<task_event, 1024>& in_pipe, pool<task_event>& p) {
    int processed = 0;
    while (true) {
        if (auto ev = in_pipe.pop(p)) {
            if (ev->job_id == -1) break; // Poison pill (Shutdown)
            processed++;
        }
    }
    std::cout << "[Shard " << shard_id << "] Processed " << processed << " tasks.\n";
}

int main() {
    pool<task_event> memory(4096);

    // Hardware-isolated conduits (one for each thread)
    conduit<task_event, 1024> track_A;
    conduit<task_event, 1024> track_B;

    // Deterministic Load Balancer (Zero-cost Fan-out)
    round_robin_switch<task_event, 2, 1024> load_balancer;
    load_balancer.bind_track(0, track_A);
    load_balancer.bind_track(1, track_B);

    // Launching threads
    std::thread t1(shard_worker, 0, std::ref(track_A), std::ref(memory));
    std::thread t2(shard_worker, 1, std::ref(track_B), std::ref(memory));

    std::cout << "Dispatching 100,000 tasks across shards...\n";
    for (int i = 0; i < 100000; ++i) {
        auto task = memory.make(i);
        load_balancer.on(task); // The switch automatically distributes between A and B!
    }

    // Shutdown (Poison pills)
    auto stop1 = memory.make(-1);
    auto stop2 = memory.make(-1);
    load_balancer.on(stop1);
    load_balancer.on(stop2);

    t1.join();
    t2.join();

    return 0;
}
