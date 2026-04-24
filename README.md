# CONDUIT Runtime Environment ⚡  

**CONDUIT** is a deterministic, lock-free event routing and processing framework designed for High-Frequency Trading (HFT) and ultra-low latency backend systems.

Built strictly on C++20 Concepts, CONDUIT avoids Object-Oriented runtime overhead (no RTTI, no virtual tables) in favor of Compile-Time Topological Routing, Union-Based Slab Pool allocation, and Hardware-Isolated SPSC Conduits.

## 🔥 Core Invariants

CONDUIT relies on strict execution invariants:

- Zero Dynamic Allocation: `new` and `malloc` are strictly forbidden in the hot path. All memory is pre-allocated and managed via `cre::pool`.  
- Zero Hidden Synchronization: Mutexes, locks, and `std::shared_ptr` are not used. Threads communicate exclusively via cache-line padded conduit ring buffers.  
- Predictable Backpressure: Saturated conduits yield deterministically, immediately reclaiming `event_ptr` memory without deadlocks.
- Structural Honesty: Events use Composition, not Inheritance, ensuring flat, unfragmented memory layouts.

## 🚀 Setup and Topology

CONDUIT requires explicit architectural definitions. You must allocate memory, define a logical pipeline, bind it to a sink, map it to a conduit, and route it through a cluster.

```cpp
#include <conduit/core.hpp>
#include <iostream>

// 1. Define an event using Composition (Fixed Memory Footprint)
struct tick_data { double price; };

struct tick_event : cre::allocated_event<tick_event, 10> {
    tick_data data;
    tick_event(double p) : data{p} {}
};

// 2. Define a zero-overhead CRTP handler
struct tick_processor : cre::handler_base<tick_processor> {
    void on(cre::event_ptr<tick_event>& ev) {
        if (ev) std::cout << "Processed tick: $" << ev->data.price << "\n";
    }
};

int main() {
    // 3. Initialize Memory Domain
    cre::runtime_domain<tick_event> domain;
    auto& pool = domain.get_pool<tick_event>();

    // 4. Build Logical Execution Pipeline
    tick_processor processor;
    cre::pipeline<tick_processor> pipe(processor);

    // 5. Create Physical Topologies (Sink & Conduit)
    cre::bound_sink<decltype(pipe), tick_event> sink(pipe);
    cre::conduit<tick_event, 1024> track;
    track.bind(&sink);

    // 6. Map to a Spatial Cluster Router
    cre::cluster<256> core_router;
    core_router.bind<tick_event>(track);

    // 7. Allocation -> Route -> Process
    core_router.send(domain.make<tick_event>(150.50));

    // Simulate background worker thread popping the conduit
    if (auto ev = track.pop(pool)) {
        sink.handle(std::move(ev));
    }

    return 0;
}
```

🧭 Advanced Routing: Spatial Switches & Pollers

CONDUIT explicitly bans Multi-Producer/Multi-Consumer (MPMC) queues internally to prevent False Sharing and L1 cache invalidation storms. For complex topologies, CONDUIT provides O(1) deterministic Fan-Out and Fan-In primitives.

```cpp
// Distributing load across 2 isolated hardware threads
cre::conduit<trade_event, 4096> shard_0;
cre::conduit<trade_event, 4096> shard_1;

// Deterministic Fan-Out Switch
cre::round_robin_switch<trade_event, 2, 4096> balancer;
balancer.bind_track(0, shard_0);
balancer.bind_track(1, shard_1);

cre::cluster<256> core;
core.bind<trade_event>(balancer);

// Alternates perfectly between shard_0 and shard_1 without lock contention
core.send(domain.make<trade_event>(...));
```

📂 Repository Structure

- /include/conduit/ — The monolithic C++20 core header.  
- /examples/ — 13 step-by-step architectural blueprints.  
- /tests/ — GoogleTest suite proving memory safety and layout invariants.
- /docs/ — In-depth System Architect references and ADRs. 
- /benchmarks/ — HDR Histogram and Google Benchmark validation.

Built for the microsecond. Architected for the nanosecond.

## ⚡ Performance & Determinism
CONDUIT is a fully standard C++20 library.

| Benchmark               | Latency (Mean) | Iterations   |
|-------------------------|----------------|--------------|
| **BM_Conduit_Push**     | **1.77–1.87 ns** | 407,272,727  |
| **BM_Conduit_FullFlux** | **11.8 ns**      | 64,000,000   |

> **Environment**: Windows 11 Pro, MSVC v143 (Release), AMD Ryzen 5 9600X.

## 💻 Compiler & Platform Compatibility
CONDUIT is a fully standard C++20 library.

- Compiler: MSVC C++20 (v143 or newer) 
- Architecture: Windows x64 
- Standard Library: Fully compliant C++20 STL 

### Roadmap & Cross-Compiler Support
While the core logic relies strictly on standard C++ features (Concepts, Atomics, Memory Barriers), cross-compiler support is being actively integrated into the public roadmap:
- **Clang 15+**: (Planned) Verification of LLVM-based optimization and instruction pipeline analysis.
- **GCC 12+**: (Planned) Linux-specific kernel tuning and io_uring integration examples.

## ⚖️ Licensing
CONDUIT is released under the GNU Affero General Public License v3 (AGPLv3). For proprietary environments requiring closed-source integration, contact the author for commercial licensing.
