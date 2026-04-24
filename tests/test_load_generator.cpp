#pragma once

#include <chrono>
#include <thread> // Required for std::this_thread::yield and sleep_for

#include <conduit/core.hpp>

namespace cre::testing {

template <typename DomainType, typename TargetConduit, typename Event>
class load_generator {
    DomainType& domain_;
    TargetConduit& conduit_;
    bool is_running_{false};
    uint64_t target_eps_{0};  // Events Per Second (0 = Max speed)

   public:
    load_generator(DomainType& domain, TargetConduit& conduit, uint64_t eps = 0)
        : domain_(domain), conduit_(conduit), target_eps_(eps) {}

    void run() noexcept {
        is_running_ = true;
        uint64_t generated = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (is_running_) {
            // 1. O(1) event generation from the pool
            auto ev = domain_.template make_uninitialized<Event>();

            if (ev) {
                // Minimal data fill so the CPU has something to do
                ev->workflow_id = generated % 65536;

                // 2. O(1) push into the conduit
                // If the conduit is full (Backpressure), the generator slows down — this is
                // physical self‑protection
                while (!conduit_.push(std::move(ev)) && is_running_) {
                    std::this_thread::yield();
                }
                generated++;
            }

            // Rate‑limited EPS mode (if not running at max speed)
            if (target_eps_ > 0 && generated >= target_eps_) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed =
                    std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                if (elapsed < 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } else {
                    generated = 0;
                    start_time = std::chrono::steady_clock::now();
                }
            }
        }
    }

    void stop() { is_running_ = false; }
};
}  // namespace cre::testing
