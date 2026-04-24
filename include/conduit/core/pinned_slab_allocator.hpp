/**
 * @file pinned_slab_allocator.hpp
 * @author Kristóf Barta
 * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include "physical_layout.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace cre::core {

    // ============================================================
    // PINNED SLAB ALLOCATOR
    // Page-locked physical memory for GPU zero-copy DMA transfers.
    // The OS cannot page this to disk (swap), allowing the GPU 
    // to read it directly without CPU intervention.
    // ============================================================

    template <typename EventType, size_t PoolSize>
    class pinned_slab_allocator {
        static_assert(sizeof(EventType) % CACHE_LINE_SIZE == 0, 
                      "CONDUIT ERROR: EventType must be padded to Cache Line size (64 bytes) for GPU efficiency!");

        EventType* raw_memory_{nullptr};
        
        // Lock-free LIFO free-list for indices
        std::atomic<uint32_t> head_index_{0};
        std::atomic<size_t> free_count_{PoolSize};

    public:
        pinned_slab_allocator() {
            size_t total_bytes = sizeof(EventType) * PoolSize;
            std::cout << "[CONDUIT Memory] Requesting " << (total_bytes / 1024 / 1024) 
                      << " MB of PINNED (Page-Locked) memory from OS...\n";

#if defined(_WIN32)
            raw_memory_ = static_cast<EventType*>(VirtualAlloc(
                NULL, total_bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!raw_memory_) throw std::runtime_error("CONDUIT: VirtualAlloc failed");
            
            if (!VirtualLock(raw_memory_, total_bytes)) {
                VirtualFree(raw_memory_, 0, MEM_RELEASE);
                throw std::runtime_error("CONDUIT: VirtualLock failed. Run as Administrator or increase Working Set size!");
            }
#else
            raw_memory_ = static_cast<EventType*>(mmap(
                nullptr, total_bytes, PROT_READ | PROT_WRITE, 
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0));
            if (raw_memory_ == MAP_FAILED) throw std::runtime_error("CONDUIT: mmap failed");
            
            if (mlock(raw_memory_, total_bytes) != 0) {
                munmap(raw_memory_, total_bytes);
                throw std::runtime_error("CONDUIT: mlock failed. Check 'ulimit -l' (locked memory limit)!");
            }
#endif

            // Initialize free-list (Intrusive chaining in memory)
            for (uint32_t i = 0; i < PoolSize - 1; ++i) {
                // Temporarily borrow the first 4 bytes for the 'next' pointer
                *reinterpret_cast<uint32_t*>(&raw_memory_[i]) = i + 1;
            }
            *reinterpret_cast<uint32_t*>(&raw_memory_[PoolSize - 1]) = 0xFFFFFFFF; // End of list
            
            std::cout << "[CONDUIT Memory] Pinned Slab initialized. GPU DMA target acquired.\n";
        }

        ~pinned_slab_allocator() {
            size_t total_bytes = sizeof(EventType) * PoolSize;
#if defined(_WIN32)
            if (raw_memory_) {
                VirtualUnlock(raw_memory_, total_bytes);
                VirtualFree(raw_memory_, 0, MEM_RELEASE);
            }
#else
            if (raw_memory_ && raw_memory_ != MAP_FAILED) {
                munlock(raw_memory_, total_bytes);
                munmap(raw_memory_, total_bytes);
            }
#endif
        }

        // ============================================================
        // ZERO-COPY ALLOCATION (Providing GPU-visible pointer)
        // ============================================================
        EventType* allocate() noexcept {
            uint32_t current_head = head_index_.load(std::memory_order_relaxed);
            uint32_t next_head;
            
            do {
                if (current_head == 0xFFFFFFFF) return nullptr; // Pool empty
                
                // Read the next index from the linked list
                next_head = *reinterpret_cast<uint32_t*>(&raw_memory_[current_head]);

            } while (!head_index_.compare_exchange_weak(current_head, next_head, 
                     std::memory_order_release, std::memory_order_relaxed));

            free_count_.fetch_sub(1, std::memory_order_relaxed);
            
            // Overwrite chaining data, providing a clean block for the GPU
            EventType* allocated_block = &raw_memory_[current_head];
            new (allocated_block) EventType(); // Placement new
            
            return allocated_block;
        }

        // ============================================================
        // ZERO-COPY DEALLOCATION (Reclamation post-GPU execution)
        // ============================================================
        void deallocate(EventType* ptr) noexcept {
            if (!ptr || ptr < raw_memory_ || ptr >= raw_memory_ + PoolSize) return;

            uint32_t block_index = static_cast<uint32_t>(ptr - raw_memory_);
            uint32_t current_head = head_index_.load(std::memory_order_relaxed);

            do {
                *reinterpret_cast<uint32_t*>(ptr) = current_head;
            } while (!head_index_.compare_exchange_weak(current_head, block_index, 
                     std::memory_order_release, std::memory_order_relaxed));

            free_count_.fetch_add(1, std::memory_order_relaxed);
        }

        size_t get_free_count() const noexcept { return free_count_.load(std::memory_order_relaxed); }
    };

} // namespace cre::core