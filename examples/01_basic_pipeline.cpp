#include <iostream>

#include "conduit/core.hpp"

using namespace cre;

// 1. Explicit event type definition.
//    No RTTI, no dynamic type inspection. The event carries a compile-time fixed ID (10),
//    ensuring static identity and deterministic, zero-overhead dispatch.
struct data_packet : allocated_event<data_packet, 10> {
    int payload;
    data_packet(int p) : payload(p) {}
};

// 2. Processing stages implemented via CRTP with zero runtime overhead.
//    No virtual calls; the compiler fully inlines the handler chain.
struct multiplier_stage : handler_base<multiplier_stage> {
    void on(event_ptr<data_packet>& ev) {
        if (!ev) return;
        ev->payload *= 2;
        std::cout << "[Stage 1] Multiplied to: " << ev->payload << "\n";
    }
};

struct logger_stage : handler_base<logger_stage> {
    void on(event_ptr<data_packet>& ev) {
        if (!ev) return;
        std::cout << "[Stage 2] Final payload logged: " << ev->payload << "\n";
    }
};

int main() {
    // 3. Initialize the dedicated memory domain.
    //    All allocations and reclamation for this event type are isolated within this domain.
    runtime_domain<data_packet> domain;

    // 4. Construct the logical pipeline.
    //    Stage order is fixed at compile time; execution is deterministic and fully inlined.
    multiplier_stage stage1;
    logger_stage stage2;
    pipeline<multiplier_stage, logger_stage> pipe(stage1, stage2);

    // 5. O(1) allocation and execution.
    //    The event is created from the domain pool, dispatched through the pipeline,
    //    and its memory is deterministically reclaimed once processing completes.
    auto ev = domain.make<data_packet>(21);

    std::cout << "Starting dispatch...\n";
    pipe.dispatch(ev);
    std::cout << "Dispatch finished. Memory safely reclaimed.\n";

    return 0;
}
