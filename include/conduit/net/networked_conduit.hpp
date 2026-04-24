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
#include <concepts>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <type_traits>

#include "../core.hpp"

#if defined(_WIN32)
#include <winsock2.h>
using os_socket_t = SOCKET;
constexpr int OS_EWOULDBLOCK = WSAEWOULDBLOCK;
#else
#include <sys/socket.h>

#include <cerrno>
using os_socket_t = int;
constexpr int OS_EWOULDBLOCK = EAGAIN;
#ifndef INVALID_SOCKET
constexpr int INVALID_SOCKET = -1;
#endif
#endif

namespace cre::net {

// ---------------------------------------------------------
// 5. & 4. Stricter ConduitEvent Concept & Trivially Copyable
// ---------------------------------------------------------
template <typename T>
concept ConduitEvent = requires {
    { T::TYPE_ID } -> std::convertible_to<uint32_t>;
} && std::is_trivially_copyable_v<T>;

template <typename T>
struct alignas(64) wire_frame {
    uint32_t type_id;
    std::byte payload[sizeof(T)];
};

template <ConduitEvent T, size_t Capacity>
class alignas(64) networked_conduit {
   private:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2.");
    static constexpr size_t MASK = Capacity - 1;
    static constexpr size_t FRAME_SIZE = sizeof(wire_frame<T>);

    // 7. OS-specific invalid socket
    os_socket_t sock_fd_{INVALID_SOCKET};

    // Atomic flag, since both push() (Producer) and poll_* (Consumer) read/write it
    alignas(64) std::atomic<bool> is_dead_{true};

    // ---------------------------------------------------------
    // TX Ring (Producer: Local Pipeline / Consumer: OS Socket)
    // ---------------------------------------------------------
    alignas(64) std::array<wire_frame<T>, Capacity> tx_ring_;
    alignas(64) std::atomic<size_t> tx_head_{0};
    alignas(64) std::atomic<size_t> tx_tail_{0};

    // 1. Partial Send State (Only the poll thread reads/writes this; no atomics needed)
    size_t tx_inflight_offset_{0};

    // ---------------------------------------------------------
    // RX State (Consumer: OS Socket / Producer: Local Domain)
    // ---------------------------------------------------------
    alignas(64) wire_frame<T> rx_staging_;
    size_t rx_bytes_read_{0};

   public:
    networked_conduit() = default;

    void bind_socket(os_socket_t fd) noexcept {
        // Ideally assert that fd is actually in O_NONBLOCK mode
        sock_fd_ = fd;
        is_dead_.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool push(cre::event_ptr<T>& ev) noexcept {
        if (is_dead_.load(std::memory_order_acquire) || !ev) return false;

        const size_t head = tx_head_.load(std::memory_order_relaxed);
        const size_t tail = tx_tail_.load(std::memory_order_acquire);

        if (head - tail >= Capacity) {
            return false;
        }

        wire_frame<T>& frame = tx_ring_[head & MASK];
        frame.type_id = T::TYPE_ID;
        std::memcpy(frame.payload, ev.get(), sizeof(T));

        tx_head_.store(head + 1, std::memory_order_release);
        ev.reset();
        return true;
    }

    // 2. Poll TX: Cyclic drain and partial-send handling
    void poll_tx() noexcept {
        if (is_dead_.load(std::memory_order_relaxed)) return;

        size_t tail = tx_tail_.load(std::memory_order_relaxed);
        const size_t head = tx_head_.load(std::memory_order_acquire);

        // TODO(Performance): Implement vector I/O (writev / WSASend) for true batching.
        // Currently relies on a tight non-blocking send() loop.
        while (tail < head) {
            wire_frame<T>& frame = tx_ring_[tail & MASK];

            const char* data_ptr = reinterpret_cast<const char*>(&frame) + tx_inflight_offset_;
            const size_t bytes_to_send = FRAME_SIZE - tx_inflight_offset_;

            int bytes_sent = ::send(sock_fd_, data_ptr, bytes_to_send, 0);

            if (bytes_sent > 0) {
                tx_inflight_offset_ += bytes_sent;

                if (tx_inflight_offset_ == FRAME_SIZE) {
                    // Full frame sent, move to next
                    tx_inflight_offset_ = 0;
                    tail++;
                }
            } else {
                int err = get_last_os_error();
                if (err == OS_EWOULDBLOCK) {
                    break;  // TCP window full, deterministic yield
                } else {
                    mark_dead();
                    break;
                }
            }
        }
        tx_tail_.store(tail, std::memory_order_release);
    }

    // 3. Poll RX: Continuous drain until the kernel buffer is fully exhausted
    template <typename DomainType, typename SinkType>
    void poll_rx(DomainType& local_domain, SinkType& local_sink) noexcept {
        if (is_dead_.load(std::memory_order_relaxed)) return;

        while (true) {
            char* target_buffer = reinterpret_cast<char*>(&rx_staging_) + rx_bytes_read_;
            const size_t bytes_to_read = FRAME_SIZE - rx_bytes_read_;

            int bytes_read = ::recv(sock_fd_, target_buffer, bytes_to_read, 0);

            if (bytes_read > 0) {
                rx_bytes_read_ += bytes_read;

                if (rx_bytes_read_ == FRAME_SIZE) {
                    if (rx_staging_.type_id != T::TYPE_ID) {
                        mark_dead();
                        return;
                    }

                    auto ev = local_domain.template make<T>();
                    if (ev) {
                        std::memcpy(ev.get(), rx_staging_.payload, sizeof(T));

                        // 100% ZERO-OVERHEAD CALL: No virtual dispatch!
                        local_sink.on(ev);
                    }

                    rx_bytes_read_ = 0;
                }
            } else if (bytes_read == 0) {
                mark_dead();
                break;
            } else {
                int err = get_last_os_error();
                if (err == OS_EWOULDBLOCK) {
                    break;
                } else {
                    mark_dead();
                    break;
                }
            }
        }
    }

    [[nodiscard]] bool is_alive() const noexcept {
        return !is_dead_.load(std::memory_order_acquire);
    }

   private:
    void mark_dead() noexcept {
        bool expected = false;
        if (is_dead_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            // PHYSICAL CLEANUP: Reset all partial-transfer state.
            // This prevents "Zombie Frame" corruption if the Orchestrator
            // later binds a new OS socket to this conduit.
            rx_bytes_read_ = 0;
            tx_inflight_offset_ = 0;

            // 6. DEAD Transition surfacing
            // TODO(Architecture): Emit an CONDUIT system event (e.g., conduit_dead_event)
            // into the local runtime topology to trigger failover/orchestration routines.
        }
    }

    static int get_last_os_error() noexcept {
#if defined(_WIN32)
        return WSAGetLastError();
#else
        return errno;
#endif
    }
};

}  // namespace cre::net
