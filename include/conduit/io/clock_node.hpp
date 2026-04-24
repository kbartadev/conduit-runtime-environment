/**
 * @file clock_node.hpp
 * @author Kristóf Barta
 * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <chrono>
#include <thread>

#include "../core.hpp"

namespace cre::io {

template <typename DomainType, typename TargetConduit>
class clock_node {
    DomainType& domain_;
    TargetConduit& target_conduit_;
    uint32_t resolution_ms_;
    bool is_running_{false};

   public:
    // resolution_ms: How often a Tick event should be emitted (e.g., 1ms, 10ms, 1000ms)
    clock_node(DomainType& domain, TargetConduit& conduit, uint32_t resolution_ms = 1)
        : domain_(domain), target_conduit_(conduit), resolution_ms_(resolution_ms) {}

    // This is started on a dedicated I/O thread
    void run() noexcept {
        is_running_ = true;

        auto last_tick = std::chrono::steady_clock::now();
        auto absolute_start = std::chrono::system_clock::now();

        while (is_running_) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count();

            if (elapsed >= resolution_ms_) {
                // A new Tick has occurred
                auto abs_now = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   (absolute_start + (now - last_tick)).time_since_epoch())
                                   .count();

                auto ev = domain_.template make<cre::sys::tick_event>();
                if (ev) {
                    ev->timestamp_ms = abs_now;
                    ev->delta_ms = elapsed;

                    // Push it into the Core (O(1) with backpressure)
                    if (!target_conduit_.push(ev)) {
                        // If the Core’s conduit is full, the time simply “disappears” (or backs
                        // up). CONDUIT Physics: If the Core is overloaded, we don’t kill it with
                        // memory exhaustion. Dropped ticks make the next tick’s delta_ms larger.
                    }
                }
                last_tick = now;
            }

            // How to avoid burning 100% CPU on the I/O thread during the wait
            // (Important: the Compute Node NEVER sleeps, but I/O nodes DO)
            std::this_thread::sleep_for(std::chrono::milliseconds(resolution_ms_ / 2));
        }
    }

    void stop() noexcept { is_running_ = false; }
};
}  // namespace cre::io
