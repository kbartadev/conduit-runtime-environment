 /**
  * @file core.hpp
  * @version 0.7.5
  * @author Kristóf Barta
  * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
  *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
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

namespace axiom {

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
    // LAYER 2: O(1) SLAB POOL
    // ============================================================

#ifdef __cpp_lib_hardware_interference_size
    constexpr std::size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
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

    template<typename Event>
    class pool {
        std::unique_ptr<cell<Event>[]> memory_;
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> free_head_{0};
        uint32_t capacity_;
        static constexpr uint32_t END_OF_LIST = 0xFFFFFFFF;

    public:
        explicit pool(uint32_t capacity = 1024) : capacity_(capacity) {
            memory_ = std::make_unique<cell<Event>[]>(capacity);
            for (uint32_t i = 0; i < capacity - 1; ++i) {
                memory_[i].next_index = i + 1;
            }
            memory_[capacity - 1].next_index = END_OF_LIST;
            free_head_.store(0, std::memory_order_relaxed);
        }

        void* allocate_raw() {
            uint64_t head = free_head_.load(std::memory_order_acquire);
            uint32_t index, tag;
            do {
                index = static_cast<uint32_t>(head & 0xFFFFFFFFu);
                tag = static_cast<uint32_t>(head >> 32);
                if (index == END_OF_LIST) std::terminate();
                uint32_t next = memory_[index].next_index;
                uint64_t new_head = (static_cast<uint64_t>(tag + 1) << 32) | next;
                if (free_head_.compare_exchange_weak(head, new_head, std::memory_order_release, std::memory_order_acquire)) break;
            } while (true);
            return memory_[index].payload;
        }

        void deallocate_raw(void* raw_ptr) noexcept {
            auto* c = reinterpret_cast<cell<Event>*>(raw_ptr);
            uint32_t index = static_cast<uint32_t>(c - memory_.get());
            uint64_t head = free_head_.load(std::memory_order_acquire);
            uint32_t tag;
            do {
                tag = static_cast<uint32_t>(head >> 32);
                c->next_index = static_cast<uint32_t>(head & 0xFFFFFFFFu);
                uint64_t new_head = (static_cast<uint64_t>(tag + 1) << 32) | index;
                if (free_head_.compare_exchange_weak(head, new_head, std::memory_order_release, std::memory_order_acquire)) break;
            } while (true);
        }

        template<typename... Args>
        [[nodiscard]] event_ptr<Event> make(Args&&... args) {
            void* raw = allocate_raw();
            Event* ev = new(raw) Event(std::forward<Args>(args)...);
            return event_ptr<Event>(ev, pool_deleter<Event>{this});
        }
    };

    // ============================================================
    // LAYER 3: SCALABLE CLUSTER (O(1) Static Topology)
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

    // Compile-time deterministic router. Zero virtual dispatch.
    template <typename... Routes>
    class cluster {
        std::tuple<Routes...> routes_;

       public:
        // Initializes the cluster with a fixed, static routing topology.
        explicit cluster(Routes... rs) : routes_(rs...) {}

        // O(1) Branchless Dispatch via compile-time fold expressions.
        template <typename Event>
        void send(event_ptr<Event>& ev) {
            if (!ev) return;

            // The compiler completely unrolls this expression at compile-time.
            // Branches for non-matching types are explicitly discarded, resulting
            // in a single, direct 'call' instruction in the generated assembly.
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
        alignas(CACHE_LINE_SIZE) std::atomic<uint32_t> read_idx_{0};
        alignas(CACHE_LINE_SIZE) Event* ring_[Capacity]{};

    public:
        bool push(Event* ev) noexcept {
            uint32_t curr_w = write_idx_.load(std::memory_order_relaxed);
            uint32_t next_w = (curr_w + 1) % Capacity;
            if (next_w == read_idx_.load(std::memory_order_acquire)) return false;
            ring_[curr_w] = ev;
            write_idx_.store(next_w, std::memory_order_release);
            return true;
        }

        event_ptr<Event> pop(pool<Event>& p) noexcept {
            uint32_t curr_r = read_idx_.load(std::memory_order_relaxed);
            if (curr_r == write_idx_.load(std::memory_order_acquire)) return nullptr;
            Event* ev = ring_[curr_r];
            read_idx_.store((curr_r + 1) % Capacity, std::memory_order_release);
            return event_ptr<Event>(ev, pool_deleter<Event>{&p});
        }
    };

    // ============================================================
    // LAYER 5: FAN-OUT & FAN-IN
    // ============================================================

    template<typename Event, std::size_t NumTracks, uint32_t TrackCapacity = 1024>
    class round_robin_switch {
        conduit<Event, TrackCapacity>* tracks_[NumTracks]{};
        std::size_t cursor_ = 0;

    public:
        void bind_track(std::size_t index, conduit<Event, TrackCapacity>& track) {
            if (index < NumTracks) tracks_[index] = &track;
        }

        void on(event_ptr<Event>& ev) {
            if (!ev) return;
            std::size_t target = cursor_;
            cursor_ = (cursor_ + 1) % NumTracks;
            if (tracks_[target] && tracks_[target]->push(ev.get())) {
                ev.release();  // Ownership successfully transferred to the conduit
            }
        }
    };

    template<typename Event, std::size_t NumTracks, uint32_t TrackCapacity = 1024>
    class round_robin_poller {
        conduit<Event, TrackCapacity>* tracks_[NumTracks]{};
        std::size_t cursor_ = 0;

    public:
        void bind_track(std::size_t index, conduit<Event, TrackCapacity>& track) {
            if (index < NumTracks) tracks_[index] = &track;
        }

        event_ptr<Event> poll(pool<Event>& p) {
            for (std::size_t i = 0; i < NumTracks; ++i) {
                std::size_t target = (cursor_ + i) % NumTracks;
                if (tracks_[target]) {
                    if (auto ev = tracks_[target]->pop(p)) {
                        cursor_ = (target + 1) % NumTracks;
                        return ev;
                    }
                }
            }
            return nullptr;
        }
    };

    // ============================================================
    // LAYER 6: PIPELINE (C++20 CONCEPTS)
    // ============================================================

    template<typename T> struct handler_base { using self_t = T; };

    template<typename Handler, typename Event>
    concept CanHandleExact = requires(Handler h, event_ptr<Event>& ev) {
        { h.on(ev) };
    };

    template<typename... Handlers>
    class pipeline {
        std::tuple<Handlers&...> handlers_;
    public:
        explicit pipeline(Handlers&... hs) : handlers_(hs...) {}

        template<typename Event>
        void dispatch(event_ptr<Event>& ev) {
            if (!ev) return;
            std::apply([&](auto&... h) {
                ( ... , [&]() {
                    if (!ev) return;
                    if constexpr (CanHandleExact<std::remove_reference_t<decltype(h)>, Event>) {
                        h.on(ev);
                    }
                }() );
            }, handlers_);
        }
        template <typename Event>
        void dispatch(event_ptr<Event>&& ev) {
            dispatch(ev);
        }
    };

    // ============================================================
    // LAYER 7-8: BOUNDARIES & NETWORK
    // ============================================================

    template<typename Pipeline, typename Event>
    class bound_sink {
        Pipeline& pipe_;
    public:
        explicit bound_sink(Pipeline& p) : pipe_(p) {}
        void handle(event_ptr<Event> ev) { if (ev) pipe_.dispatch(ev); }
    };

    template<typename Event>
    struct trivial_serializer {
        static_assert(std::is_trivially_copyable_v<Event>, "Event must be POD for network");
        static std::pair<const uint8_t*, std::size_t> encode(const Event* ev) {
            return {reinterpret_cast<const uint8_t*>(ev), sizeof(Event)};
        }
        static void decode(const uint8_t* data, Event* out) { std::memcpy(out, data, sizeof(Event)); }
    };

    // ============================================================
    // LAYER 9: RUNTIME DOMAIN
    // ============================================================

    template<typename... Events>
    class runtime_domain {
        std::tuple<pool<Events>...> pools_;
    public:
        template<typename Event, typename... Args>
        [[nodiscard]] event_ptr<Event> make(Args&&... args) {
            return std::get<pool<Event>>(pools_).make(std::forward<Args>(args)...);
        }
        template<typename Event>
        pool<Event>& get_pool() { return std::get<pool<Event>>(pools_); }
    };

} // namespace axiom
