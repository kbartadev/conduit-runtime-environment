#include <iostream>
#include <string>

#include "axiom_conduit/core.hpp"

using namespace axiom;

// --- 1. Multi-layer event composed from independent logical layers.
//     Composition is used instead of OOP inheritance to avoid fragmentation
//     and to remain compatible with O(1) slab-based allocation.
struct net_layer {
    std::string ip;
};
struct auth_layer {
    int session_id;
};
struct app_layer {
    std::string payload;
};

struct http_request : allocated_event<http_request, 10> {
    net_layer net;
    auth_layer auth;
    app_layer app;

    http_request(const char* ip, int sid, const char* p) : net{ip}, auth{sid}, app{p} {}
};

// --- 2. Independent, specialized handlers.
//     Each stage operates only on its own layer and has no visibility into others.
struct firewall_stage : handler_base<firewall_stage> {
    void on(net_layer& net) { std::cout << "[Firewall] Checking IP: " << net.ip << "\n"; }
};

struct auth_stage : handler_base<auth_stage> {
    void on(auth_layer& auth) {
        std::cout << "[Auth] Validating session: " << auth.session_id << "\n";
    }
};

// --- 3. Cascading processor that defines the strict execution order.
//     The sequence is explicit and fully inlined at compile time.
struct request_processor : handler_base<request_processor> {
    firewall_stage fw;
    auth_stage au;

    void on(event_ptr<http_request>& ev) {
        if (!ev) return;
        std::cout << "--- Processing new request ---\n";

        // Strict waterfall-style ordering (compile-time inlined).
        fw.on(ev->net);
        au.on(ev->auth);

        // Final application-level processing.
        std::cout << "[App] Processing payload: " << ev->app.payload << "\n";
    }
};

int main() {
    runtime_domain<http_request> domain;
    request_processor processor;

    // The pipeline contains only the top-level processor,
    // which internally drives all subordinate stages.
    pipeline<request_processor> pipe(processor);

    // Two complex events allocated and processed in O(1).
    pipe.dispatch(domain.make<http_request>("192.168.1.1", 1042, "GET /index.html"));
    pipe.dispatch(domain.make<http_request>("10.0.0.5", 9999, "POST /login"));

    return 0;
}
