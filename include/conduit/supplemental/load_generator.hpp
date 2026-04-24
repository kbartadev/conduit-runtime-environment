#pragma once

#include "../core/physical_layout.hpp"
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace cre::supplemental {

    // ============================================================
    // CONDUIT SUPPLEMENTAL: SYNTHETIC BLASTER
    // Zero-allocation load generator for maximum saturation.
    // ============================================================

    template <typename ConduitType, typename AllocatorType, typename EventType>
    class load_generator {
        ConduitType& target_conduit_;
        AllocatorType& memory_pool_;
        uint32_t total_events_to_send_;

        // Pre-allocated pointer array (the weapon magazine)
        std::vector<EventType*> pre_allocated_ammo_;

    public:
        load_generator(ConduitType& conduit, AllocatorType& pool, uint32_t events_count)
            : target_conduit_(conduit), memory_pool_(pool), total_events_to_send_(events_count)
        {
            pre_allocated_ammo_.reserve(events_count);
        }

        ~load_generator() {
            // Return the ammo to the pool when finished
            for (auto* ev : pre_allocated_ammo_) {
                memory_pool_.deallocate(ev);
            }
        }

        // Phase 1: Loading the magazine (not included in performance measurement)
        void arm_weapon() {
            std::cout << "[CONDUIT Blaster] Arming weapon with " << total_events_to_send_ << " events...\n";
            for (uint32_t i = 0; i < total_events_to_send_; ++i) {
                EventType* ev = memory_pool_.allocate();
                if (!ev) {
                    throw std::runtime_error("CONDUIT: Memory pool exhausted during arming phase!");
                }
                ev->size_bytes = sizeof(EventType);
                pre_allocated_ammo_.push_back(ev);
            }
            std::cout << "[CONDUIT Blaster] Weapon armed. Ready to fire.\n";
        }

        // Phase 2: Firing (raw physical O(1) loop)
        void fire() {
            std::cout << "[CONDUIT Blaster] Firing...\n";
            auto start_time = std::chrono::high_resolution_clock::now();

            uint32_t successful_pushes = 0;
            uint32_t backpressure_hits = 0;
            // Number of times the conduit was full

            for (uint32_t i = 0; i < total_events_to_send_; /* manual increment */) {
                // Attempt to push into the conduit in O(1)
                if (target_conduit_.push(pre_allocated_ammo_[i])) {
                    i++;
                    successful_pushes++;
                }
                else {
                    // If the conduit is full (the Core cannot keep up), spin
                    // We intentionally avoid sleeping to measure raw pressure
                    backpressure_hits++;
                }
            }

            auto end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed_seconds = end_time - start_time;

            // Phase 3: Telemetry and Results
            double ops_per_sec = successful_pushes / elapsed_seconds.count();

            std::cout << "=========================================\n";
            std::cout << " CONDUIT PHYSICAL STRESS TEST RESULTS\n";
            std::cout << "=========================================\n";
            std::cout << " Total Events Sent : " << successful_pushes << "\n";
            std::cout << " Time Elapsed      : " << elapsed_seconds.count() << " seconds\n";
            std::cout << " Backpressure Hits : " << backpressure_hits << " (Conduit full events)\n";
            std::cout << "-----------------------------------------\n";
            std::cout << " THROUGHPUT        : " << static_cast<uint64_t>(ops_per_sec) << " events/sec\n";
            std::cout << "=========================================\n";
        }
    };
} // namespace cre::supplemental