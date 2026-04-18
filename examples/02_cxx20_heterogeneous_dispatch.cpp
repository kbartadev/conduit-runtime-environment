#include "axiom_conduit/core.hpp"
#include <iostream>

using namespace axiom;

// 1. Distinct event types with static identities.
struct trade_event : allocated_event<trade_event, 10> {
    double price;
    trade_event(double p) : price(p) {}
};

struct audit_event : allocated_event<audit_event, 20> {
    int severity;
    audit_event(int s) : severity(s) {}
};

// 2. Specialized handler that only processes trade events.
//    It is never invoked for other event types.
struct trading_engine : handler_base<trading_engine> {
    void on(event_ptr<trade_event>& ev) {
        if (ev) std::cout << "[Trade Engine] Executed trade at $" << ev->price << "\n";
    }
};

// 3. Global handler that observes both trade and audit events.
//    Overloads are resolved statically based on the event type.
struct compliance_logger : handler_base<compliance_logger> {
    void on(event_ptr<trade_event>& ev) {
        if (ev) std::cout << "[Compliance] Logged trade.\n";
    }
    
    void on(event_ptr<audit_event>& ev) {
        if (ev) std::cout << "[Compliance] Logged audit alert level: " << ev->severity << "\n";
    }
};

int main() {
    runtime_domain<trade_event, audit_event> domain;

    trading_engine engine;
    compliance_logger logger;

    // Heterogeneous pipeline: the compiler (via C++20 constraints) decides
    // which handlers are applicable for each event type at compile time.
    pipeline<trading_engine, compliance_logger> unified_pipe(engine, logger);

    auto t_ev = domain.make<trade_event>(1500.50);
    auto a_ev = domain.make<audit_event>(3);

    std::cout << "--- Sending Trade Event ---\n";
    // Dispatching a trade event invokes both trading_engine and compliance_logger.
    unified_pipe.dispatch(t_ev); 

    std::cout << "\n--- Sending Audit Event ---\n";
    // Dispatching an audit event invokes only compliance_logger.
    // trading_engine is skipped with zero runtime cost.
    unified_pipe.dispatch(a_ev); 

    return 0;
}
