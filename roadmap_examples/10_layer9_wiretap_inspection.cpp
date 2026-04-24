#include <iostream>

#include "conduit/core.hpp"

using namespace cre;

struct finance_order : allocated_event<finance_order, 88> {
    int order_id;
    finance_order(int id) : order_id(id) {}
};

// Dummy sink simulating the real execution endpoint.
struct order_executor : handler_base<order_executor> {
    void on(event_ptr<finance_order>& ev) {
        if (ev) std::cout << "[Real Executor] Order " << ev->order_id << " executed.\n";
    }
};

int main() {
    runtime_domain<finance_order> domain;
    auto& pool = domain.get_pool<finance_order>();

    // 1. Production topology: real executor pipeline and its bound sink.
    order_executor real_executor;
    pipeline<order_executor> pipe(real_executor);
    bound_sink<decltype(pipe), finance_order> real_sink(pipe);

    conduit<finance_order, 1024> real_pipe;
    real_pipe.bind(&real_sink);

    // 2. Layer 9: wiretap conduit attached transparently to the physical pipe.
    //    Intercepts all traffic with effectively zero overhead on the hot path.
    wiretap_conduit<finance_order, 1024> tapped_pipe(real_pipe);

    // 3. Topology wiring: the switch sees the tapped conduit, not the original.
    //    The production path stays untouched; the wiretap rides along.
    round_robin_switch<finance_order, 1, 1024> router;
    router.bind_track(0, tapped_pipe);

    cluster<256> c;
    c.bind<finance_order>(router);

    std::cout << "--- Sending orders through wiretapped topology ---\n";
    c.send(domain.make<finance_order>(1001));
    c.send(domain.make<finance_order>(1002));

    // Production flush (actual execution).
    real_sink.handle(tapped_pipe.pop(pool));
    real_sink.handle(tapped_pipe.pop(pool));

    // 4. Extract audit results from the wiretap.
    std::cout << "\n--- Wiretap Audit Results ---\n";
    std::cout << "Total intercepted: " << tapped_pipe.get_intercept_count() << "\n";
    std::cout << "History[0] order_id: " << tapped_pipe.get_history()[0].order_id << "\n";
    std::cout << "History[1] order_id: " << tapped_pipe.get_history()[1].order_id << "\n";

    return 0;
}
