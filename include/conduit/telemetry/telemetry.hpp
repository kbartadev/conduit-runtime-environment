/**
 * @file telemetry.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include "../core.hpp"

namespace cre::supplemental {

// ============================================================
// SUPPLEMENTAL LAYER: ZERO-OVERHEAD TELEMETRY
// Zero locks, zero atomics, zero false sharing.
// ============================================================

// 1. The Zero-Cost Wrapper
// Wraps the user’s business logic. The C++20 compiler fully inlines this.
template <typename InnerNode>
class telemetry_wrapper {
    InnerNode& inner_;

    // PHYSICAL TRICK: alignas(64)
    // Ensures this counter occupies a dedicated L1 cache line (64 bytes).
    // This way, memory operations from other threads never cause false sharing.
    alignas(64) uint64_t processed_events_{0};

   public:
    explicit telemetry_wrapper(InnerNode& inner) : inner_(inner) {}

    // C++20 auto template: captures every event the InnerNode can handle
    template <typename Event>
    __forceinline void on(cre::core::event_ptr<Event>& ev) {
        processed_events_++;  // One raw CPU register increment (zero cost)
        inner_.on(ev);        // Forward to the actual logic
    }

    // This is what the metrics thread will read
    [[nodiscard]] uint64_t get_count() const volatile noexcept { return processed_events_; }
};

// 2. The Asynchronous Metrics Reader (Scraper I/O Node)
// Runs on a dedicated I/O thread and “glances” at the counters once per second
class telemetry_scraper {
    std::vector<std::function<uint64_t()>> targets_;
    bool is_running_{false};

   public:
    // Attach a wrapper to the scraper
    template <typename Node>
    void register_target(const telemetry_wrapper<Node>& target) {
        targets_.emplace_back([&target]() { return target.get_count(); });
    }

    void run() noexcept {
        is_running_ = true;
        uint64_t last_total = 0;

        while (is_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            uint64_t current_total = 0;
            // Read through memory. Since the counters are stored in a volatile,
            // cache-aligned manner, reading does not block Compute threads.
            for (const auto& read_func : targets_) {
                current_total += read_func();
            }

            uint64_t eps = current_total - last_total;  // Events Per Second (EPS)
            last_total = current_total;

            // Prometheus HTTP export could go here, but for now we print to console
            std::cout << "[CONDUIT Telemetry] Throughput: " << eps
                      << " EPS | Total: " << current_total << "\n";
        }
    }

    void stop() noexcept { is_running_ = false; }
};

}  // namespace cre::supplemental
