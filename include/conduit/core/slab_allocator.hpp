/**
 * @file slab_allocator.hpp
 * @author Kristóf Barta
 * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <new>
#include <stdexcept>

#include "physical_layout.hpp"

namespace cre::core {

// ============================================================
// CONDUIT CORE: LOCK-FREE SLAB ALLOCATOR
// Wait-free, lock-free memory pool.
// ============================================================

template <typename T, uint32_t Capacity>
class slab_allocator {
    // 1. Structure of a memory cell (aligned to a cache line)
    struct alignas(CACHE_LINE_SIZE) slot {
        // Raw memory for type T (not yet constructed)
        alignas(T) std::byte storage[sizeof(T)];

        // Next free index in the chain (only valid when the slot is free)
        uint32_t next_free;
    };

    // Pre-allocated memory pool
    std::array<slot, Capacity> memory_;

    // 2. Lock-free pointer (Head)
    // [Upper 32 bits: ABA counter] | [Lower 32 bits: free slot index]
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> free_head_;

    static constexpr uint32_t END_OF_LIST = 0xFFFFFFFF;

    // Helper for packing the 64-bit state
    static uint64_t pack(uint32_t aba_counter, uint32_t index) noexcept {
        return (static_cast<uint64_t>(aba_counter) << 32) | index;
    }

    static void unpack(uint64_t packed, uint32_t& aba_counter, uint32_t& index) noexcept {
        aba_counter = static_cast<uint32_t>(packed >> 32);
        index = static_cast<uint32_t>(packed & 0xFFFFFFFF);
    }

   public:
    slab_allocator() {
        if (Capacity == 0) throw std::logic_error("CONDUIT: Capacity must be > 0");

        // 3. Initialization (only during startup)
        for (uint32_t i = 0; i < Capacity - 1; ++i) {
            memory_[i].next_free = i + 1;
        }
        memory_[Capacity - 1].next_free = END_OF_LIST;

        // Head of the list points to index 0, with ABA version 0
        free_head_.store(pack(0, 0), std::memory_order_relaxed);
    }

    ~slab_allocator() {
        // Memory is cleared here; destructors of T are called by runtime_domain
    }

    // ============================================================
    // ALLOCATION (Hot Path)
    // ============================================================
    template <typename... Args>
    T* allocate(Args&&... args) noexcept {
        uint64_t current_head = free_head_.load(std::memory_order_acquire);
        uint32_t aba_counter, index;

        while (true) {
            unpack(current_head, aba_counter, index);

            // Returns nullptr if pool is exhausted
            if (index == END_OF_LIST) return nullptr;

            uint32_t next_index = memory_[index].next_free;
            uint64_t new_head = pack(aba_counter + 1, next_index);

            // Lock-free CAS
            if (free_head_.compare_exchange_weak(current_head, new_head, std::memory_order_release,
                                                 std::memory_order_acquire)) {
                // CAS slot reservation successful.
                // Placement-new: construct the C++ object in raw memory
                T* ptr = new (&memory_[index].storage) T(std::forward<Args>(args)...);

                // Save its own index so deallocation can return in O(1)
                ptr->internal_index = index;
                return ptr;
            }
            // If CAS failed, retry.
        }
    }

    // ============================================================
    // DEALLOCATION (Return to Pool)
    // ============================================================
    void deallocate(T* ptr) noexcept {
        if (!ptr) return;

        // 1. Call the C++ destructor (placement-delete equivalent)
        ptr->~T();

        // 2. Return to the free list
        uint32_t index = ptr->internal_index;

        uint64_t current_head = free_head_.load(std::memory_order_acquire);
        uint32_t aba_counter, current_index;

        while (true) {
            unpack(current_head, aba_counter, current_index);

            // Our block’s next element becomes the old head
            memory_[index].next_free = current_index;

            // Our block becomes the new head (ABA counter increments again)
            uint64_t new_head = pack(aba_counter + 1, index);

            if (free_head_.compare_exchange_weak(current_head, new_head, std::memory_order_release,
                                                 std::memory_order_acquire)) {
                return; // Successfully returned
            }
        }
    }
};

}  // namespace cre::core