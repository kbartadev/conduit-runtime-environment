#include <chrono>
#include <iostream>
#include <thread>

#include "axiom/axiom.hpp"

using namespace axiom;

// System-level event carrying a timestamp.
// Time is just another event type in the fabric.
struct heartbeat_tick : allocated_event<heartbeat_tick, 99> {
    uint64_t timestamp_ms;
    heartbeat_tick(uint64_t t) : timestamp_ms(t) {}
};

struct connection_monitor : handler_base<connection_monitor> {
    uint64_t last_activity_ms = 0;

    // External activity updates (simulated here).
    void update_activity(uint64_t current_time) { last_activity_ms = current_time; }

    // On receiving a time event, compute idle duration and check timeout.
    void on(event_ptr<heartbeat_tick>& ev) {
        if (!ev) return;
        uint64_t idle_time = ev->timestamp_ms - last_activity_ms;

        std::cout << "[Monitor] Heartbeat received. Idle for: " << idle_time << "ms.\n";
        if (idle_time > 2000) {
            std::cout << "[Monitor] WARNING: Connection Timeout! Dropping session.\n";
        }
    }
};

int main() {
    runtime_domain<heartbeat_tick> domain;
    auto& pool = domain.get_pool<heartbeat_tick>();
    conduit<heartbeat_tick, 1024> time_pipe;

    // Chronos thread: emits a heartbeat event once per second.
    // Time is pushed into the same event fabric as business traffic.
    std::thread chronos_thread([&]() {
        for (int i = 1; i <= 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            uint64_t now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
            while (!time_pipe.push(domain.make<heartbeat_tick>(now).release())) {
                std::this_thread::yield();
            }
        }
        time_pipe.push(domain.make<heartbeat_tick>(0).release());  // Exit signal.
    });

    // Main thread acts as the processor.
    connection_monitor monitor;
    monitor.update_activity(std::chrono::system_clock::now().time_since_epoch().count() / 1000000);

    // Event-driven time handling: no polling loops, no timers scattered everywhere.
    while (true) {
        if (auto ev = time_pipe.pop(pool)) {
            if (ev->timestamp_ms == 0) break;
            monitor.on(ev);
        }
    }

    chronos_thread.join();
    return 0;
}
