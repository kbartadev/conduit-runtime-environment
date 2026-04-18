#include <iostream>
#include <thread>
#include <vector>

#include "axiom_conduit/core.hpp"

using namespace axiom;

struct result_event : allocated_event<result_event, 33> {
    int worker_id;
    int calculated_value;
    result_event(int w, int v) : worker_id(w), calculated_value(v) {}
};

void worker_thread(int id, conduit<result_event, 1024>& out_pipe, pool<result_event>& p) {
    for (int i = 1; i <= 5; ++i) {
        // Simulated heavy work; each result is pushed into the worker's SPSC conduit.
        // Spin until the conduit accepts the event. No cheating, no dropping.
        while (!out_pipe.push(p.make(id, id * 100 + i).release())) {
            std::this_thread::yield();
        }
    }
}

int main() {
    runtime_domain<result_event> domain;
    auto& pool = domain.get_pool<result_event>();

    // Two independent SPSC conduits, one per worker.
    // No producer contention, no hidden sharing.
    conduit<result_event, 1024> pipe_from_w1;
    conduit<result_event, 1024> pipe_from_w2;

    // Fan-in poller that merges both conduits in a fair round-robin pattern.
    // Every track gets a turn; no starvation, no bias.
    round_robin_poller<result_event, 2, 1024> aggregator;
    aggregator.bind_track(0, pipe_from_w1);
    aggregator.bind_track(1, pipe_from_w2);

    // Launch worker threads.
    std::thread t1(worker_thread, 1, std::ref(pipe_from_w1), std::ref(pool));
    std::thread t2(worker_thread, 2, std::ref(pipe_from_w2), std::ref(pool));

    // Main thread acts as the consumer, polling until all 10 results are received.
    int received = 0;
    std::cout << "--- Aggregating results deterministically ---\n";

    while (received < 10) {
        if (auto ev = aggregator.poll(pool)) {
            std::cout << "[Aggregator] Received from Worker " << ev->worker_id
                      << " | Value: " << ev->calculated_value << "\n";
            received++;
        }
    }

    t1.join();
    t2.join();
    return 0;
}
