/**
 * @file networked_conduit.hpp
 * @author Kristóf Barta
 * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

#include "physical_layout.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cre::core {

// ============================================================
// CONDUIT CORE: SPSC LOCK-FREE RING BUFFER
// A zero-cost bridge between the Compute and I/O threads.
// Capacity must be a power of 2 for fast modulo operations.
// ============================================================
template <typename T, uint32_t Capacity>
class spsc_ring_buffer {
    static_assert((Capacity != 0) && ((Capacity & (Capacity - 1)) == 0),
                  "Capacity must be power of 2");

    std::array<T*, Capacity> buffer_{nullptr};

    // Writer and reader indices are aligned to cache lines to avoid false sharing.
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> write_idx_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> read_idx_{0};

   public:
    // Called by the Compute thread
    __forceinline bool push(T* item) noexcept {
        const uint32_t current_write = write_idx_.load(std::memory_order_relaxed);
        const uint32_t next_write = (current_write + 1) & (Capacity - 1);

        // Ring is full (backpressure)
        if (next_write == read_idx_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[current_write] = item;
        write_idx_.store(next_write, std::memory_order_release);
        return true;
    }

    // Called by the I/O thread
    __forceinline T* pop() noexcept {
        const uint32_t current_read = read_idx_.load(std::memory_order_relaxed);

        // Ring is empty
        if (current_read == write_idx_.load(std::memory_order_acquire)) {
            return nullptr;
        }

        T* item = buffer_[current_read];
        read_idx_.store((current_read + 1) & (Capacity - 1), std::memory_order_release);
        return item;
    }
};

// ============================================================
// CONDUIT CORE: NETWORKED CONDUIT (TCP SENDER NODE)
// Dedicated TCP writer running on the I/O thread
// ============================================================
template <typename EventBaseType, uint32_t QueueSize>
class tcp_sender_node {
    spsc_ring_buffer<EventBaseType, QueueSize> ring_;
    int socket_fd_{-1};
    bool is_running_{false};

   public:
    tcp_sender_node(int connected_socket) : socket_fd_(connected_socket) {}

    ~tcp_sender_node() { stop(); }

    // 1. Called by the Compute thread (the hot path)
    // Fully lock-free, O(1), zero OS calls
    __forceinline bool send_async(EventBaseType* ev) noexcept { return ring_.push(ev); }

    // 2. Called by the I/O thread (background worker from the Supplemental Env)
    void run() noexcept {
        if (socket_fd_ < 0) return;
        is_running_ = true;

        while (is_running_) {
            if (auto ev = ring_.pop()) {
                // This is where we leave the O(1) domain and hand it to the kernel
                // Copying bits to the network card
                // (In reality, partial sends (EAGAIN) must be handled here)
                send(socket_fd_, reinterpret_cast<const char*>(ev), ev->size_bytes, 0);

                // After it goes out to the network, we release it in the O(1) slab allocator
                // ev->release(); // If we have reference counting
            } else {
                // If there is nothing to send, let the I/O thread rest
                std::this_thread::yield();
            }
        }
    }

    void stop() noexcept { is_running_ = false; }
};

}  // namespace cre::core
