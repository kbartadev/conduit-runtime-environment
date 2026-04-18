#include <iostream>
#include <thread>

#include "axiom_conduit/core.hpp"

using namespace axiom;

struct telemetry_event : allocated_event<telemetry_event, 5> {
    int sensor_id;
    double value;
    telemetry_event(int id, double v) : sensor_id(id), value(v) {}
};

int main() {
    runtime_domain<telemetry_event> domain;
    auto& pool = domain.get_pool<telemetry_event>();

    // SPSC (Single-Producer Single-Consumer) lock-free conduit with capacity 1024.
    // Designed for contention-free transfer between isolated producer/consumer threads.
    conduit<telemetry_event, 1024> hardware_bus;

    // Consumer thread.
    std::thread background_processor([&]() {
        while (true) {
            // O(1) pop: returns a pointer if available, or nullptr if the conduit is empty.
            if (auto ev = hardware_bus.pop(pool)) {
                if (ev->sensor_id == -1) break;  // Termination sentinel.
                std::cout << "[Thread] Processed sensor " << ev->sensor_id
                          << " value: " << ev->value << "\n";
            }
        }
        std::cout << "[Thread] Shutting down.\n";
    });

    // Producer thread (main thread).
    std::cout << "[Main] Generating telemetry...\n";
    hardware_bus.push(domain.make<telemetry_event>(1, 42.5).release());
    hardware_bus.push(domain.make<telemetry_event>(2, 99.9).release());
    hardware_bus.push(domain.make<telemetry_event>(-1, 0.0).release());  // Poison pill.

    background_processor.join();
    return 0;
}
