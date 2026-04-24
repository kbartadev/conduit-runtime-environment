# CONDUIT Runtime Environment

CONDUIT is a C++20 deterministic distributed event runtime designed specifically for High-Frequency Trading (HFT) and ultra-low latency environments.

Built strictly on C++20 Concepts and lock-free atomic principles, it treats data flow as a physical fluid and enforces strict architectural boundaries to achieve microsecond-level determinism.

## Core Engineering Principles

CONDUIT operates on a set of immutable engineering constraints designed to prevent CPU pipeline stalls, cache misses, and memory fragmentation:

* **No OOP Inheritance or Virtual Dispatch:** Virtual functions (`vptr` lookups) and RTTI are explicitly banned to prevent instruction pipeline stalls and cache line bloat. Handlers use the Curiously Recurring Template Pattern (CRTP) for static polymorphism.
* **No Global Heap Allocation in the Hot Path:** Standard `new` and `malloc` are forbidden. All runtime memory is pre-allocated and managed via an O(1) union-based slab pool.
* **No MPMC/MPSC Queues:** To prevent False Sharing and MESI cache invalidation storms, only padded Single-Producer Single-Consumer (SPSC) ring buffers (`conduit`) are permitted.
* **No Hidden Synchronization:** Locks, mutexes, and semaphores are strictly prohibited.
* **Strict Unique Ownership:** Events are managed via a custom `cre::event_ptr` which automatically returns memory to the lock-free pool in O(1) time upon scope exit.

## The Architecture Stack

CONDUIT is structured into explicit physical and logical layers:

* **Pool (`cre::pool`):** A lock-free, union-based slab allocator with ABA protection (using 64-bit tagged pointers) that guarantees O(1) memory recycling and prevents fragmentation.
* **Conduit (`cre::conduit`):** A physically isolated, cache-line aligned SPSC ring buffer for transferring `event_ptr` ownership across threads without locks.
* **Switches & Pollers:** Deterministic fan-out (`round_robin_switch`) and fan-in (`round_robin_poller`) topology nodes to distribute load across SPSC tracks.
* **Pipeline (`cre::pipeline`):** A compile-time routing chain. Using C++20 fold expressions and Concepts, it routes events through matching handlers with zero runtime overhead.
* **Network Ingress (`cre::net::networked_conduit`):** Bridges external TCP streams into the runtime, performing zero-copy, bitwise reconstruction of trivially copyable events.

## Usage Example: Basic Pipeline

Routing logic is resolved entirely at compile time. If an event type does not match a handler's signature, the compiler completely bypasses it.

```cpp
#include <iostream>
#include "conduit/core.hpp"

using namespace cre;

// 1. Define a statically identified event
struct data_packet : allocated_event<data_packet, 10> {
    int payload;
    data_packet(int p) : payload(p) {}
};

// 2. Define a CRTP handler
struct multiplier_stage : handler_base<multiplier_stage> {
    void on(event_ptr<data_packet>& ev) {
        if (ev) ev->payload *= 2;
    }
};

int main() {
    // 3. Initialize the memory domain
    runtime_domain<data_packet> domain;
    
    multiplier_stage stage1;
    
    // 4. Construct the compile-time pipeline
    pipeline<multiplier_stage> pipe(stage1);

    // 5. O(1) allocation and deterministic dispatch
    auto ev = domain.make<data_packet>(21);
    pipe.dispatch(ev); // payload becomes 42

    return 0; // ev goes out of scope and memory is reclaimed instantly
}
```

## System Requirements & Tuning

To achieve intended performance and determinism, CONDUIT requires specific system tuning:

- **C++ Standard**: Strictly C++20.
- **Supported Compilers**: MSVC v143 (Reference), Clang 15+, GCC 12+.
- **Hardware Isolation**: Threads must be pinned to isolated CPU cores using thread affinity to ensure O(1) latency guarantees.
- **Optimization**: MANDATORY use of -O3 -march=native -flto to ensure proper inlining of dispatch chains.

## Licensing

- **CONDUIT** is released under a dual license model:
- **Open Source**: GNU Affero General Public License v3 (AGPLv3).
- **Commercial**: Proprietary licensing available for closed-source integration.
