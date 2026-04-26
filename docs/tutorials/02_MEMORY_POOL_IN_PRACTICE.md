# Tutorial 2: Memory Pools in Practice

In CONDUIT, calling `new` or `malloc` in the hot path is a critical violation of performance principles. All runtime memory must come from an O(1) slab pool.

## The Rule of Ownership

Events are wrapped in an `cre::event_ptr`. This acts similarly to `std::unique_ptr`, but it does not call `delete`. Instead, it returns the memory slice directly to the lock-free pool in O(1) time.

## Safe Allocation

~~~cpp
// 1. Define the domain and its capacity
// Capacity (e.g., 1024) is required for the O(1) slab allocation
cre::runtime_domain<trade_event, 1024> domain;

// 2. Allocate an event
auto ev = domain.make<trade_event>(150.50);

if (ev) {
   // ev is valid and uniquely owned by this scope
}

// 3. ev goes out of scope -> instantly returns to the free-list
~~~

Never store a raw pointer to an event. If you need to pass an event down the line, use `std::move(ev)`.
