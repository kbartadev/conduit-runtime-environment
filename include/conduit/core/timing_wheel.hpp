/**
 * @file timing_wheel.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include "physical_layout.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>

namespace cre::core {

    // ============================================================
    // CONDUIT CORE: O(1) TIMING WHEEL
    // Deterministic timeout handling for terminating stuck Sagas.
    // Zero‑allocation, intrusive linked‑list based structure.
    // ============================================================

    // This structure must be inherited into the Saga state machine’s memory
    struct timing_node {
        uint32_t timer_next{ 0xFFFFFFFF }; // END_OF_LIST
        uint32_t timer_prev{ 0xFFFFFFFF };
        uint32_t wheel_bucket{ 0xFFFFFFFF }; // Which bucket it resides in (for fast removal)
    };

    template <typename SlabAllocator, uint32_t WheelSlots = 65536>
    class timing_wheel {
        static_assert((WheelSlots& (WheelSlots - 1)) == 0,
            "CONDUIT ERROR: WheelSlots must be a power of 2 for O(1) modulo.");
        static constexpr uint32_t SLOT_MASK = WheelSlots - 1;
        static constexpr uint32_t END_OF_LIST = 0xFFFFFFFF;

        // The wheel’s “teeth”. Each stores the index of the first element in that bucket.
        std::array<uint32_t, WheelSlots> buckets_;

        uint64_t current_tick_{ 0 }; // Absolute time (monotonically increasing)
        SlabAllocator& memory_pool_; // Reference to physical memory to access nodes

    public:
        explicit timing_wheel(SlabAllocator& pool) : memory_pool_(pool) {
            buckets_.fill(END_OF_LIST);
        }

        // ============================================================
        // O(1) TIMER INSERTION
        // ============================================================
        void schedule(uint32_t node_index, uint32_t delay_ticks) noexcept {
            if (delay_ticks >= WheelSlots) {
                // A hierarchical wheel would be needed here, but in an HFT system
                // timeouts longer than ~65 seconds are rare.
                delay_ticks = WheelSlots - 1;
            }

            // Compute the target bucket
            uint32_t bucket_index = (current_tick_ + delay_ticks) & SLOT_MASK;

            auto* node = memory_pool_.get_by_index(node_index); // (Assumes the Slab provides an O(1) getter)

            // Intrusive insertion at the head of the list (O(1))
            node->timer_next = buckets_[bucket_index];
            node->timer_prev = END_OF_LIST;
            node->wheel_bucket = bucket_index;

            if (buckets_[bucket_index] != END_OF_LIST) {
                auto* old_head = memory_pool_.get_by_index(buckets_[bucket_index]);
                old_head->timer_prev = node_index;
            }

            buckets_[bucket_index] = node_index;
        }

        // ============================================================
        // O(1) TIMER CANCELLATION (If the Tensor arrived in time!)
        // ============================================================
        void cancel(uint32_t node_index) noexcept {
            auto* node = memory_pool_.get_by_index(node_index);
            uint32_t bucket = node->wheel_bucket;

            if (bucket == END_OF_LIST) return; // Not scheduled

            // O(1) list unlink
            if (node->timer_prev != END_OF_LIST) {
                memory_pool_.get_by_index(node->timer_prev)->timer_next = node->timer_next;
            }
            else {
                buckets_[bucket] = node->timer_next; // We were the head
            }

            if (node->timer_next != END_OF_LIST) {
                memory_pool_.get_by_index(node->timer_next)->timer_prev = node->timer_prev;
            }

            node->wheel_bucket = END_OF_LIST;
        }

        // ============================================================
        // O(1) CLOCK ADVANCE (Triggers expired timers)
        // ============================================================
        // Returns the head of the expired linked list. The Core must iterate it.
        uint32_t tick() noexcept {
            current_tick_++;
            uint32_t current_bucket = current_tick_ & SLOT_MASK;

            uint32_t expired_head = buckets_[current_bucket];
            buckets_[current_bucket] = END_OF_LIST; // Clear the bucket

            return expired_head;
        }
    };

} // namespace cre::core
