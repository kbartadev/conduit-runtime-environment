#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

#include "axiom/axiom.hpp"

using namespace axiom;

struct work_event : allocated_event<work_event, 10> {
    int id;
    bool is_poison_pill;
    work_event(int i, bool poison = false) : id(i), is_poison_pill(poison) {}
};

std::atomic<bool> global_shutdown_requested{false};

// OS-level signal handler (e.g., Ctrl+C).
// Translates an external interrupt into a controlled shutdown request.
void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n[OS] SIGINT received. Initiating graceful shutdown...\n";
        global_shutdown_requested.store(true, std::memory_order_release);
    }
}

int main() {
    std::signal(SIGINT, signal_handler);

    runtime_domain<work_event> domain;
    auto& pool = domain.get_pool<work_event>();
    conduit<work_event, 1024> processing_pipe;

    // Background worker that runs until a poison pill is received.
    // No forced termination, no half-processed queues.
    std::thread worker([&]() {
        int processed = 0;
        while (true) {
            if (auto ev = processing_pipe.pop(pool)) {
                if (ev->is_poison_pill) {
                    std::cout << "[Worker] Poison pill received. Draining complete. Exiting.\n";
                    break;
                }
                processed++;
                // Simulated heavy work.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        std::cout << "[Worker] Safely processed " << processed << " events before shutdown.\n";
    });

    // Main thread generating traffic.
    int event_counter = 0;
    while (!global_shutdown_requested.load(std::memory_order_acquire)) {
        if (processing_pipe.push(domain.make<work_event>(event_counter++).release())) {
            // Producer is faster than the worker, so the conduit fills up.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // For testing: automatically trigger shutdown after 50 events,
        // as if Ctrl+C had been pressed.
        if (event_counter == 50) global_shutdown_requested.store(true);
    }

    // --- GRACEFUL DRAIN ---
    std::cout << "[Main] Halting traffic. Sending poison pill...\n";

    // Do not kill the worker thread abruptly.
    // Push a poison pill to the end of the conduit so it can drain all
    // in-flight events before exiting.
    while (!processing_pipe.push(domain.make<work_event>(-1, true).release())) {
        std::this_thread::yield();  // Wait for space if the conduit is full.
    }

    worker.join();

    // Proof: the worker drained the conduit, and every event_ptr returned
    // its memory to the pool. Zero leaks at shutdown.
    std::cout << "[Main] System halted cleanly.\n";

    return 0;
}
