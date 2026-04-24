/**
 * @file async_logger.hpp
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
#include <string>
#include <thread>
#include <vector>

#include "../core.hpp"

namespace cre::supplemental {

// ============================================================
// SUPPLEMENTAL LAYER: ZERO-OVERHEAD LOGGER
// Zero string formatting on the hot path. Zero mutexes.
// ============================================================

// 1. The POD Log Event (what flows through the conduit)
struct log_event : cre::core::allocated_event<log_event, 252> {
    uint64_t timestamp_ns;  // Exact time of occurrence
    uint16_t message_id;    // Which format string to use
    uint64_t arg1;          // Raw parameters (e.g., ID, Price)
    uint64_t arg2;
};

// 2. The Asynchronous Formatter and Writer I/O Node
template <typename TargetConduit>
class async_logger_node {
    TargetConduit& conduit_;
    bool is_running_{false};

    // Format strings registered at compile time or startup
    std::vector<std::string> format_strings_;

   public:
    explicit async_logger_node(TargetConduit& conduit) : conduit_(conduit) {}

    // Register a message and receive an ID that O(1) code can use
    uint16_t register_message(const std::string& format_str) {
        format_strings_.push_back(format_str);
        return static_cast<uint16_t>(format_strings_.size() - 1);
    }

    // Runs in the background on a dedicated I/O thread
    void run() noexcept {
        is_running_ = true;

        while (is_running_) {
            if (auto ev = conduit_.pop()) {
                // Slow formatting happens here, far away from the Core
                if (ev->message_id < format_strings_.size()) {
                    // Simplified formatting (real implementation would use std::format)
                    std::string msg = format_strings_[ev->message_id];

                    // Replace placeholders (very primitive example)
                    size_t pos = msg.find("{}");
                    if (pos != std::string::npos) msg.replace(pos, 2, std::to_string(ev->arg1));
                    pos = msg.find("{}");
                    if (pos != std::string::npos) msg.replace(pos, 2, std::to_string(ev->arg2));

                    std::cout << "[LOG " << ev->timestamp_ns << "] " << msg << "\n";
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Rest the CPU
            }
        }
    }

    void stop() { is_running_ = false; }
};

// 3. Developer Wrapper (for convenient use in logic)
template <typename Domain, typename TargetConduit>
class logger_client {
    Domain& domain_;
    TargetConduit& conduit_;

   public:
    logger_client(Domain& domain, TargetConduit& conduit) : domain_(domain), conduit_(conduit) {}

    // O(1) call: sets 4 registers and pushes. No string operations!
    __forceinline void log(uint16_t message_id, uint64_t arg1 = 0, uint64_t arg2 = 0) noexcept {
        if (auto ev = domain_.template make<log_event>()) {
            // Querying the system clock here may be expensive; ideally we would use the Core’s
            // internal clock
            ev->timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            ev->message_id = message_id;
            ev->arg1 = arg1;
            ev->arg2 = arg2;
            conduit_.push(std::move(ev));
        }
    }
};

}  // namespace cre::supplemental
