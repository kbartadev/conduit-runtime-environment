/**
  * @file core.hpp
  * @brief Enterprise-grade, Zero-Overhead Conduit Runtime Environment Core
  * @author Kristóf Barta
  * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
  * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
  * For commercial use, proprietary licensing, and support, please contact the author.
  * See LICENSE and CONTRIBUTING.md for details.
  */

  /**
 * @file core.hpp
 * @brief Enterprise-grade, Zero-Overhead Conduit Runtime Environment Core
 * @copyright Copyright (c) 2026. All rights reserved.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>
#include <utility>
#include <tuple>
#include <array>
#include <vector>
#include <concepts>
#include <type_traits>
#include <cstring>

namespace cre {

    constexpr std::size_t CACHE_LINE_SIZE = 64;

    // ===========================================================;
    // LAYER 1: BASE EVENT & OWNERSHIP
    // ===========================================================;

    template<typename Derived, uint8_t RoutingID>
    struct allocated_event {
        static constexpr uint8_t EVENT_ID = RoutingID;

        static void* operator new(std::size_t) = delete;
        static void* operator new(std::size_t, void* ptr) noexcept { return ptr; }
        static void operator delete(void*, void*) noexcept {}
        static void operator delete(void*) = delete;
    };

    // ===========================================================;
    // LAYER 2: HIERARCHICAL EXTENSION
    // ===========================================================;

    template<typename... Bases>
    struct extends : public Bases... {
        using base_types = std::tuple<Bases*...>;
    };

    // ===========================================================;
    // LAYER 2.5: EXTENDED EVENT
    // ===========================================================;

    template<typename Derived, uint8_t RoutingID, typename... Bases>
    struct extended_event : public allocated_event<Derived, RoutingID>, public extends<Bases...> {
    };

    // ===========================================================;
    // LAYER 3: DETERMINISTIC SMART POINTER
    // ===========================================================;

    struct non_owning_tag {};

    template<typename T>
    class event_ptr {
        T* ptr_;
        void (*deleter_)(void*, void*);
        void* pool_ctx_;

    public:
        event_ptr() noexcept : ptr_(nullptr), deleter_(nullptr), pool_ctx_(nullptr) {}
        event_ptr(std::nullptr_t) noexcept : event_ptr() {}

        event_ptr(T* p, void (*del)(void*, void*), void* ctx) noexcept
            : ptr_(p), deleter_(del), pool_ctx_(ctx) {
        }

        ~event_ptr() { reset(); }

        event_ptr(event_ptr&& other) noexcept
            : ptr_(other.ptr_), deleter_(other.deleter_), pool_ctx_(other.pool_ctx_) {
            other.ptr_ = nullptr;
            other.deleter_ = nullptr;
        }

        event_ptr& operator=(event_ptr&& other) noexcept {
            if (this != &other) {
                reset();
                ptr_ = other.ptr_;
                deleter_ = other.deleter_;
                pool_ctx_ = other.pool_ctx_;
                other.ptr_ = nullptr;
                other.deleter_ = nullptr;
            }
            return *this;
        }

        event_ptr(const event_ptr&) = delete;
        event_ptr& operator=(const event_ptr&) = delete;

        template<typename U>
            requires std::derived_from<U, T>
        event_ptr(const event_ptr<U>& other, non_owning_tag) noexcept
            : ptr_(static_cast<T*>(other.get())), deleter_(nullptr), pool_ctx_(nullptr) {
        }

        void reset() noexcept {
            if (ptr_ && deleter_) deleter_(pool_ctx_, ptr_);
            ptr_ = nullptr;
            deleter_ = nullptr;
        }

        T* release() noexcept {
            T* temp = ptr_;
            ptr_ = nullptr;
            deleter_ = nullptr;
            return temp;
        }

        T* get() const noexcept { return ptr_; }
        T* operator->() const noexcept { return ptr_; }
        T& operator*() const noexcept { return *ptr_; }
        explicit operator bool() const noexcept { return ptr_ != nullptr; }

        bool operator==(std::nullptr_t) const noexcept { return ptr_ == nullptr; }
        bool operator!=(std::nullptr_t) const noexcept { return ptr_ != nullptr; }
        friend bool operator==(std::nullptr_t, const event_ptr& p) noexcept { return p.ptr_ == nullptr; }
        friend bool operator!=(std::nullptr_t, const event_ptr& p) noexcept { return p.ptr_ != nullptr; }
    };

    // ===========================================================;
    // LAYER 4: O(1) LOCK-FREE MEMORY POOL
    // ===========================================================;

    template<typename T>
    class pool {
        union cell {
            alignas(T) std::byte data[sizeof(T)];
            uint64_t next;
        };

        std::vector<cell> memory_;
        alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> free_head_;

    public:
        static void release_to_pool(void* ctx, void* ptr) noexcept {
            static_cast<pool<T>*>(ctx)->free(static_cast<T*>(ptr));
        }

        explicit pool(std::size_t capacity) : memory_(capacity) {
            for (std::size_t i = 0; i < capacity - 1; ++i) {
                memory_[i].next = i + 1;
            }
            memory_[capacity - 1].next = 0xFFFFFFFF;
            free_head_.store(0, std::memory_order_relaxed);
        }

        pool(pool&& other) noexcept : memory_(std::move(other.memory_)) {
            free_head_.store(other.free_head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        template<typename... Args>
        [[nodiscard]] event_ptr<T> make(Args&&... args) {
            uint64_t head = free_head_.load(std::memory_order_acquire);
            uint64_t next;
            do {
                uint32_t idx = head & 0xFFFFFFFF;
                if (idx == 0xFFFFFFFF) return event_ptr<T>(nullptr);

                next = memory_[idx].next;
                uint64_t new_head = ((head >> 32) + 1) << 32 | (next & 0xFFFFFFFF);

                if (free_head_.compare_exchange_weak(head, new_head, std::memory_order_release, std::memory_order_relaxed)) {
                    T* ptr = reinterpret_cast<T*>(&memory_[idx].data);
                    new (ptr) T(std::forward<Args>(args)...);
                    return event_ptr<T>(ptr, release_to_pool, this);
                }
            } while (true);
        }

        void free(T* ptr) noexcept {
            if (!ptr) return;
            ptr->~T();
            std::size_t idx = reinterpret_cast<cell*>(ptr) - memory_.data();
            uint64_t head = free_head_.load(std::memory_order_acquire);
            do {
                memory_[idx].next = head & 0xFFFFFFFF;
                uint64_t new_head = ((head >> 32) + 1) << 32 | (idx & 0xFFFFFFFF);
                if (free_head_.compare_exchange_weak(head, new_head, std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
            } while (true);
        }
    };

    // ===========================================================;
    // LAYER 5: SPSC RING BUFFER (CONDUIT)
    // ===========================================================;

    template<typename T, std::size_t Size>
    class conduit {
        std::array<T*, Size> ring_;
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> write_idx_{ 0 };
        alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> read_idx_{ 0 };

    public:
        bool push(T* ptr) {
            auto current_write = write_idx_.load(std::memory_order_relaxed);
            auto next_write = (current_write + 1) % Size;

            if (next_write == read_idx_.load(std::memory_order_acquire)) {
                return false;
            }

            ring_[current_write] = ptr;
            write_idx_.store(next_write, std::memory_order_release);
            return true;
        }

        event_ptr<T> pop(pool<T>& p) {
            auto current_read = read_idx_.load(std::memory_order_relaxed);
            if (current_read == write_idx_.load(std::memory_order_acquire)) {
                return event_ptr<T>(nullptr);
            }

            T* ptr = ring_[current_read];
            read_idx_.store((current_read + 1) % Size, std::memory_order_release);
            return event_ptr<T>(ptr, pool<T>::release_to_pool, &p);
        }
    };

    // ===========================================================;
    // LAYER 6: HANDLERS & DISPATCH (Zero-Cost Mutating Pipeline)
    // ===========================================================;

    template<typename Derived>
    struct handler_base {};

    template<typename... Handlers>
    class pipeline {
        std::tuple<Handlers&...> handlers_;

    public:
        pipeline(Handlers&... h) : handlers_(h...) {}

        template<typename Event>
        void dispatch(event_ptr<Event>& ev) {
            if (!ev) [[unlikely]] return;

            std::apply([&](auto&... h) {
                (inner_dispatch(h, ev) && ...);
                }, handlers_);
        }

        template<typename Event>
        void dispatch(event_ptr<Event>&& ev) {
            event_ptr<Event> local_ev = std::move(ev);
            dispatch(local_ev);
        }

    private:
        template<typename Handler, typename Event>
        bool inner_dispatch(Handler& h, event_ptr<Event>& ev) {
            return traverse_and_call<Handler, Event, Event>(h, ev);
        }

        template<typename Handler, typename Event, typename CurrentType>
        bool traverse_and_call(Handler& h, event_ptr<Event>& ev) {
            bool proceed = true;

            if constexpr (requires { typename CurrentType::base_types; }) {
                proceed = std::apply([&](auto*... dummy) {
                    return ((traverse_and_call<Handler, Event, std::remove_pointer_t<decltype(dummy)>>(h, ev) && ev) && ...);
                    }, typename CurrentType::base_types{});
            }

            if (proceed && ev) {
                if constexpr (std::is_same_v<Event, CurrentType>) {
                    proceed = call_on_if_exists(h, ev);
                }
                else {
                    event_ptr<CurrentType> base_view(ev, non_owning_tag{});
                    proceed = call_on_if_exists(h, base_view);

                    if (!base_view) {
                        ev.release();
                        proceed = false;
                    }
                }
            }

            return proceed && ev;
        }

        template<typename Handler, typename E>
        bool call_on_if_exists(Handler& h, event_ptr<E>& ev) {
            if constexpr (requires { h.on(ev); }) {
                using Ret = decltype(h.on(ev));
                if constexpr (std::is_same_v<Ret, bool>) {
                    return h.on(ev);
                }
                else {
                    h.on(ev);
                    return true;
                }
            }
            return true;
        }
    };

    // ===========================================================;
    // LAYER 7-8: BOUNDARIES & NETWORK
    // ===========================================================;

    template<typename Pipeline, typename Event>
    class bound_sink {
        Pipeline& pipe_;
    public:
        explicit bound_sink(Pipeline& p) : pipe_(p) {}
        void handle(event_ptr<Event> ev) { if (ev) pipe_.dispatch(ev); }
    };

    template<typename Event>
    struct trivial_serializer {
        static_assert(std::is_trivially_copyable_v<Event>, "Event must be POD");
        static std::pair<const uint8_t*, std::size_t> encode(const Event* ev) {
            return { reinterpret_cast<const uint8_t*>(ev), sizeof(Event) };
        }
        static void decode(const uint8_t* data, Event* out) { std::memcpy(out, data, sizeof(Event)); }
    };

    // ===========================================================;
    // LAYER 9: RUNTIME DOMAIN & ROUTING
    // ===========================================================;

    template<typename... Events>
    class runtime_domain {
        std::tuple<pool<Events>...> pools_;
    public:
        explicit runtime_domain(std::size_t pool_size = 1024) : pools_(pool<Events>(pool_size)...) {}

        template<typename Event, typename... Args>
        [[nodiscard]] event_ptr<Event> make(Args&&... args) {
            return std::get<pool<Event>>(pools_).make(std::forward<Args>(args)...);
        }

        template<typename Event>
        pool<Event>& get_pool() { return std::get<pool<Event>>(pools_); }
    };

    struct cluster {
        template<typename E = void, typename... Args> void send(Args&&...) {}
    };
    struct router {};

    template<typename E = void, typename... Args>
    inline cluster bind_route(Args&&...) { return cluster{}; }

    template<typename T, std::size_t NumOutputs, std::size_t ConduitSize>
    class round_robin_switch : public handler_base<round_robin_switch<T, NumOutputs, ConduitSize>> {
        std::array<conduit<T, ConduitSize>*, NumOutputs> outputs_{};
        std::size_t next_{ 0 };

    public:
        void bind_track(std::size_t index, conduit<T, ConduitSize>& track) {
            if (index < NumOutputs) {
                outputs_[index] = &track;
            }
        }

        void on(event_ptr<T>& ev) {
            if (!ev) return;

            for (std::size_t i = 0; i < NumOutputs; ++i) {
                std::size_t idx = (next_ + i) % NumOutputs;

                if (outputs_[idx]) {
                    if (outputs_[idx]->push(ev.get())) {
                        ev.release();
                        next_ = (idx + 1) % NumOutputs;
                        return;
                    }
                }
            }
        }
    };

    template<typename T, std::size_t NumInputs, std::size_t ConduitSize>
    class round_robin_poller {
        std::array<conduit<T, ConduitSize>*, NumInputs> inputs_{};
        std::size_t next_{ 0 };

    public:
        void bind_track(std::size_t index, conduit<T, ConduitSize>& track) {
            if (index < NumInputs) {
                inputs_[index] = &track;
            }
        }

        // Visszatettük a memóriapoolt paraméternek!
        event_ptr<T> poll(pool<T>& p) {
            for (std::size_t i = 0; i < NumInputs; ++i) {
                std::size_t idx = (next_ + i) % NumInputs;

                if (inputs_[idx]) {
                    // A te pop(p) függvényed már event_ptr-t ad vissza, így csak átvesszük!
                    if (auto ev = inputs_[idx]->pop(p)) {
                        next_ = (idx + 1) % NumInputs;
                        return ev;
                    }
                }
            }
            return event_ptr<T>{nullptr};
        }
    };

} // namespace cre