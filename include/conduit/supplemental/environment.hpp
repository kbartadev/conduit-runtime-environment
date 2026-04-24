/**
 * @file environment.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#include "../core.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace cre::supplemental {

// ============================================================
// SUPPLEMENTAL LAYER: ENVIRONMENT BUILDER
// Declarative API for constructing threads, Nodes, and topology.
// Runs strictly during initialization; does not affect Core O(1) physics.
// ============================================================

class environment {
    // Threads are stored here only during initialization
    std::vector<std::thread> workers_;
    bool is_running_{false};

    // Internal helper for CPU core pinning (OS-level determinism)
    void pin_thread_to_core(std::thread& t, int core_id) {
        if (core_id < 0) return;  // No pinning

#if defined(_WIN32)
        HANDLE native_handle = t.native_handle();
        DWORD_PTR mask = (1ull << core_id);
        SetThreadAffinityMask(native_handle, mask);
#else
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(t.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
    }

   public:
    environment() = default;

    ~environment() { stop(); }

    // 1. Add a Compute/Worker Node to the topology
    // This hides the boilerplate “while(running)” loop and conduit popping
    template <typename Node, typename Conduit>
    void spawn_worker(Node& node, Conduit& rx_conduit, int cpu_core = -1) {
        workers_.emplace_back([this, &node, &rx_conduit]() {
            std::cout << "[CONDUIT Env] Worker thread started.\n";

            // HOT PATH — This is where O(1) is decided
            while (is_running_) {
                if (auto ev = rx_conduit.pop()) {
                    node.on(ev);
                    // When leaving scope, 'ev' returns to the pool via RAII
                } else {
                    // If the conduit is empty, yield (or spin-wait in HFT)
                    // Backpressure tuning could be added here
                    std::this_thread::yield();
                }
            }
        });

        // OS-level optimization
        pin_thread_to_core(workers_.back(), cpu_core);
    }

    // 2. Start an I/O Node (e.g., Clock or Durable Source)
    // These Nodes have their own run() loop, which may block
    template <typename IONode>
    void spawn_io_node(IONode& io_node, int cpu_core = -1) {
        workers_.emplace_back([this, &io_node]() {
            std::cout << "[CONDUIT Env] I/O node thread started.\n";

            // The I/O Node runs until stopped internally or by the Env
            io_node.run();
        });

        pin_thread_to_core(workers_.back(), cpu_core);
    }

    // Arm the system
    void start() {
        std::cout << "[CONDUIT Env] Topology locked. Igniting physical simulation...\n";
        is_running_ = true;
    }

    // Block the main thread while the system is running
    void wait_for_shutdown() {
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    void stop() { is_running_ = false; }
};

}  // namespace cre::supplemental
