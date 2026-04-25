/**
 * @file 01_backpressure_drops.cpp
 * @brief Short-circuiting and memory-safe dropping in the middle of the pipeline.
 */
#include <iostream>
#include "conduit/core.hpp"

using namespace cre;

struct order : allocated_event<order, 33> {
    double qty;
    order(double q) : qty(q) {}
};

// Gatekeeper that can stop the pipeline (bool return value)
struct risk_firewall : handler_base<risk_firewall> {
    bool on(const event_ptr<order>& ev) {
        if (ev->qty > 10000.0) {
            std::cout << "[FIREWALL] Order too large (" << ev->qty << "). DROPPED.\n";
            return false; // SHORT-CIRCUIT: Stops the pipeline!
        }
        return true;
    }
};

struct execution_engine : handler_base<execution_engine> {
    void on(const event_ptr<order>& ev) {
        std::cout << "[EXEC] Order executed for qty: " << ev->qty << "\n";
    }
};

int main() {
    pool<order> p(10);
    risk_firewall firewall;
    execution_engine engine;

    pipeline<risk_firewall, execution_engine> pipe(firewall, engine);

    std::cout << "Sending small order...\n";
    pipe.dispatch(p.make(150.0)); // Goes through

    std::cout << "\nSending toxic order...\n";
    pipe.dispatch(p.make(50000.0)); // Firewall stops it, NO memory leak.

    return 0;
}
