#include <iostream>
#include <string>

#include "axiom/axiom.hpp"

using namespace axiom;

struct telemetry_ping : allocated_event<telemetry_ping, 11> {
    int seq_num;
    telemetry_ping(int s) : seq_num(s) {}
};

int main() {
    runtime_domain<telemetry_ping> domain;
    auto& pool = domain.get_pool<telemetry_ping>();

    // Intentionally tiny conduit with capacity 2.
    // Effective usable capacity is 1 element, forcing a hard bottleneck.
    conduit<telemetry_ping, 2> bottleneck_pipe;

    round_robin_switch<telemetry_ping, 1, 2> switch_node;
    switch_node.bind_track(0, bottleneck_pipe);

    cluster<256> router;
    router.bind<telemetry_ping>(switch_node);

    std::cout << "--- Testing Deterministic Backpressure ---\n";

    // 1. First event: successful enqueue.
    std::cout << "Sending Seq 1...\n";
    router.send(domain.make<telemetry_ping>(1));

    // 2. Second event: conduit is full.
    //    The switch attempts to push, the conduit rejects it,
    //    and the event_ptr immediately returns its memory to the pool in O(1).
    //    No leaks, no partial state, no "maybe it got in".
    std::cout << "Sending Seq 2 (Expected to drop)...\n";
    router.send(domain.make<telemetry_ping>(2));

    // Proof: only the first event made it through the bottleneck.
    auto ev1 = bottleneck_pipe.pop(pool);
    std::cout << "Popped: " << (ev1 ? std::to_string(ev1->seq_num) : "null") << "\n";

    auto ev2 = bottleneck_pipe.pop(pool);
    std::cout << "Popped: " << (ev2 ? std::to_string(ev2->seq_num) : "null")
              << " (Proves Seq 2 was safely dropped)\n";

    return 0;
}
