/**
  * @file core.hpp
  * @author Kristóf Barta
  * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
  * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
  * For commercial use, proprietary licensing, and support, please contact the author.
  * See LICENSE and CONTRIBUTING.md for details.
  */

#pragma once

#include <atomic>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <new>
#include <utility>
#include <tuple>
#include <array>
#include <concepts>
#include <type_traits>
#include <cstring>

namespace cre {

    template<typename Event> class pool;

    // ============================================================
    // LAYER 1: BASE EVENT & OWNERSHIP
    // ============================================================

    template<typename Derived, uint8_t RoutingID>
    struct allocated_event {
        static constexpr uint8_t EVENT_ID = RoutingID;

        static void* operator new(std::size_t) = delete;
        static void* operator new(std::size_t, void* ptr) noexcept { return ptr; }

        static void operator delete(void*, void*) noexcept {}
        static void operator delete(void*) = delete;
    };

    template<typename T>
    struct pool_deleter {
        pool<T>* parent_pool = nullptr;

        void operator()(T* ptr) const noexcept {
            if (ptr) {
                ptr->~T();
                if (parent_pool) parent_pool->deallocate_raw(ptr); 
            }
        }
    };

    template<typename T>
    using event_ptr = std::unique_ptr<T, pool_deleter<T>>;

    // ============================================================
    // LAYER 2: WAIT-FREE MPSC SLAB POOL
    // Implementation of MPSC free-list synchronization.
    // ============================================================

#ifdef __cpp_lib_hardware_interference_size
    using std::hardware_destructive_interference_size;
    constexpr std::size_t CACHE_LINE_SIZE = hardware_destructive_interference_size;
#else
    constexpr std::size_t CACHE_LINE_SIZE = 64;
#endif

    template <typename Event>
    struct cell {
        union {
            alignas(Event) unsigned char payload[sizeof(Event)];
            uint32_t next_index;
        };
    };

    template <typename Event>
    class pool {
        std::unique_ptr<cell<Event>[]> memory_;

        // FAST PATH: Owned by the pinned Node thread. Zero atomic overhead.
        uint32_t local_head_{0};

        // RETURN PATH: Atomic MPSC LIFO stack for events returning from other threads.
        // Isolated to prevent false sharing with the fast path.
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> shared_head_{0xFFFFFFFFu};

        uint32_t capacity_;
        static constexpr uint32_t END_OF_LIST = 0xFFFFFFFF;

       public:
        explicit pool(uint32_t capacity = 1024) : capacity_(capacity) {
            memory_ = std::make_unique<cell<Event>[]>(capacity);
            for (uint32_t i = 0; i < capacity - 1; ++i) {
                memory_[i].next_index = i + 1;
            }
            memory_[capacity - 1].next_index = END_OF_LIST;

            local_head_ = 0;
            shared_head_.store(static_cast<uint64_t>(END_OF_LIST), std::memory_order_relaxed);
        }

        // Wait-Free Allocation
        void* allocate_raw() {
            // Check local single-threaded cache first
            if (local_head_ != END_OF_LIST) {
                uint32_t index = local_head_;
                local_head_ = memory_[index].next_index;
                return memory_[index].payload;
            }

            // Refill local cache from shared return path using atomic exchange
            uint64_t stolen_stack = shared_head_.exchange(static_cast<uint64_t>(END_OF_LIST),
                                                          std::memory_order_acquire);
            uint32_t index = static_cast<uint32_t>(stolen_stack & 0xFFFFFFFFu);

            // Memory pool exhausted, returning nullptr
            if (index == END_OF_LIST) return nullptr;

            local_head_ = memory_[index].next_index;
            return memory_[index].payload;
        }

        // Thread-safe return path used by I/O threads or other Nodes
        void deallocate_raw(void* raw_ptr) noexcept {
            auto* c = reinterpret_cast<cell<Event>*>(raw_ptr);
            uint32_t index = static_cast<uint32_t>(c - memory_.get());

            uint64_t old_head = shared_head_.load(std::memory_order_relaxed);
            uint64_t new_head;

            // CAS loop for the MPSC return path.
            // Tagged pointer prevents ABA and ensures eventual consistency.
            do {
                uint32_t tag = static_cast<uint32_t>(old_head >> 32);
                c->next_index = static_cast<uint32_t>(old_head & 0xFFFFFFFFu);
                new_head = (static_cast<uint64_t>(tag + 1) << 32) | index;
            } while (!shared_head_.compare_exchange_weak(
                old_head, new_head, std::memory_order_release, std::memory_order_relaxed));
        }

        // ============================================================
        // CONDUIT HIGH-LEVEL API
        // ============================================================

        // Standard construction for Compute Nodes
        template <typename... Args>
        [[nodiscard]] event_ptr<Event> make(Args&&... args) {
            void* raw = allocate_raw();
            if (!raw) return nullptr;
            Event* ev = new (raw) Event(std::forward<Args>(args)...);
            return event_ptr<Event>(ev, pool_deleter<Event>{this});
        }

        // Provides raw memory for in-place writing (e.g., from socket)
        [[nodiscard]] event_ptr<Event> make_uninitialized() {
            void* raw = allocate_raw();
            if (!raw) return nullptr;
            return event_ptr<Event>(reinterpret_cast<Event*>(raw), pool_deleter<Event>{this});
        }
    };

    // ============================================================
    // LAYER 3: SCALABLE CLUSTER (Static Topology)
    // ============================================================

    // Binds a specific Event type to a target Endpoint at compile-time.
    template <typename EventType, typename TargetType>
    struct route_binding {
        using event_t = EventType;
        TargetType& target;
    };

    // Helper factory to create a deterministic route binding.
    template <typename EventType, typename TargetType>
    route_binding<EventType, TargetType> bind_route(TargetType& t) {
        return route_binding<EventType, TargetType>{t};
    }

    // Compile-time deterministic router. Static dispatch (inlined).
    template <typename... Routes>
    class cluster {
        std::tuple<Routes...> routes_;

       public:
        // Initializes the cluster with a fixed, static routing topology.
        explicit cluster(Routes... rs) : routes_(rs...) {}

        // Branchless Dispatch via compile-time fold expressions.
        template <typename Event>
        void send(event_ptr<Event>& ev) {
            if (!ev) return;

            // The compiler unrolls this expression at compile-time.
            std::apply(
                [&ev](auto&... route) {
                    (..., [&ev, &route]() {
                        using RouteEventT =
                            typename std::remove_reference_t<decltype(route)>::event_t;

                        // Compile-time type check
                        if constexpr (std::is_same_v<RouteEventT, Event>) {
                            route.target.on(ev);
                        }
                    }());
                },
                routes_);
        }

        // Rvalue overload for consuming semantics.
        template <typename Event>
        void send(event_ptr<Event>&& ev) {
            auto tmp = std::move(ev);
            send(tmp);
        }
    };

    // ============================================================
    // LAYER 4: PHYSICAL TRANSPORT (CONDUIT)
    // ============================================================

    template<typename Event, uint32_t Capacity>
    class conduit {
        alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> write_idx_{0};
        alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> read