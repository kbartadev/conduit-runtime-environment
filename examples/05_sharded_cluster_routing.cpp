#include <iostream>

#include "axiom/axiom.hpp"

using namespace axiom;

struct task_event : allocated_event<task_event, 42> {
    int job_id;
    task_event(int id) : job_id(id) {}
};

struct task_worker : handler_base<task_worker> {
    int shard_id;
    explicit task_worker(int id) : shard_id(id) {}

    void on(event_ptr<task_event>& ev) {
        if (ev) {
            std::cout << "[Shard " << shard_id << "] Handled job " << ev->job_id << "\n";
        }
    }
};

int main() {
    runtime_domain<task_event> domain;
    auto& pool = domain.get_pool<task_event>();

    // 1. Two independent workers, each with its own pipeline.
    task_worker worker_A(0);
    task_worker worker_B(1);

    pipeline<task_worker> pipe_A(worker_A);
    pipeline<task_worker> pipe_B(worker_B);

    bound_sink<decltype(pipe_A), task_event> sink_A(pipe_A);
    bound_sink<decltype(pipe_B), task_event> sink_B(pipe_B);

    // 2. Two physical conduits (tracks), one per shard.
    conduit<task_event, 1024> track_A;
    conduit<task_event, 1024> track_B;

    // 3. Deterministic switch distributing events across tracks in round-robin order.
    round_robin_switch<task_event, 2, 1024> load_balancer;
    load_balancer.bind_track(0, track_A);
    load_balancer.bind_track(1, track_B);

    // 4. Central cluster router that forwards task_event instances to the switch.
    cluster<256> core_router;
    core_router.bind<task_event>(load_balancer);

    // 5. Generate load. The switch alternates between shard 0 and shard 1.
    core_router.send(domain.make<task_event>(100));  // -> Shard 0
    core_router.send(domain.make<task_event>(101));  // -> Shard 1
    core_router.send(domain.make<task_event>(102));  // -> Shard 0

    // Simulate worker threads by manually flushing the tracks.
    // In a real system, each shard would run its own thread calling pop().
    if (auto ev = track_A.pop(pool)) sink_A.handle(std::move(ev));
    if (auto ev = track_B.pop(pool)) sink_B.handle(std::move(ev));
    if (auto ev = track_A.pop(pool)) sink_A.handle(std::move(ev));

    return 0;
}
