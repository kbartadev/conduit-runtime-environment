/**
 * @file moe_dispatcher.hpp
 * @author Kristóf Barta
 * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include "../core/pinned_slab_allocator.hpp"
#include "../core/networked_conduit.hpp"
#include "../core/timing_wheel.hpp"
#include <iostream>
#include <atomic>

namespace cre::domain {

    // Representation of a scheduled AI task execution
    struct ai_tensor_entity {
        uint32_t next; // For the Slab free-list
        
        // Bitmask (Complex Ownership Relation)
        // bit 0: CPU core is reading, bit 1: GPU #1 is working, bit 2: GPU #2 is working
        std::atomic<uint8_t> ownership_mask{0};

        uint32_t expert_id;       // Target AI model (0: NLP, 1: Vision)
        uint32_t timeout_node_id; // Identifier for timeout tracking
        
        // Payload (Data visible to the GPU)
        float weights[1024];
    };

    template <typename Allocator, typename Conduit, typename Wheel>
    class moe_dispatcher {
        Allocator& memory_pool_;
        Conduit& inbound_pipe_;
        Wheel& timing_wheel_;

    public:
        moe_dispatcher(Allocator& pool, Conduit& pipe, Wheel& time)
            : memory_pool_(pool), inbound_pipe_(pipe), timing_wheel_(time) {}

        // ============================================================
        // Task Dispatch Phase
        // ============================================================
        void process_tick() {
            // 1. Advance timing wheel for timeout management
            uint32_t expired_head = timing_wheel_.tick();

            while (expired_head != 0xFFFFFFFF) {
                std::cout << "[Dispatcher] Task timeout exceeded (" << expired_head << "), aborting execution!\n";
                // Deallocation logic for timed-out GPU tasks would reside here
                expired_head = 0xFFFFFFFF; // Move to the next...
            }

            // 2. Read from inbound channel
            ai_tensor_entity* incoming_request;
            if (inbound_pipe_.pop(incoming_request)) {
                
                // 3. Register task in memory pool and grant ownership
                uint32_t entity_id = 42; // Example ID
                
                // Schedule 100ms timeout limit
                timing_wheel_.schedule(entity_id, 100);

                // 4. Route task based on expert_id
                std::cout << "[MoE Router] New Tensor task received. ";
                if (incoming_request->expert_id == 0) {
                    std::cout << "Task transferred.\n";
                    // Lock-free handoff to the GPU (update bitmask)
                    incoming_request->ownership_mask.store(0b00000010, std::memory_order_release);
                } 
                else if (incoming_request->expert_id == 1) {
                    std::cout << "Task transferred.\n";
                    incoming_request->ownership_mask.store(0b00000100, std::memory_order_release);
                }
            }
        }
    };
} // namespace cre::domain