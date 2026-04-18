# AXIOM Conduit Runtime ⚡  

[![C++20](https://img.shields.io/badge/C++-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Lock-Free](https://img.shields.io/badge/Concurrency-Lock--Free-orange.svg)]()
[![Zero-Allocation](https://img.shields.io/badge/Memory-Zero--Allocation-success.svg)]()

**AXIOM Conduit Runtime** is a deterministic, O(1) lock-free event routing and processing framework designed for High-Frequency Trading (HFT) and ultra-low latency telecommunication systems.

Built strictly on C++20 Concepts, AXIOM eliminates Object-Oriented runtime overhead (no RTTI, no virtual tables) in favor of Compile-Time Topological Routing, O(1) Union-Based Slab Pool Physics, and Hardware-Isolated SPSC Conduits.

🔥 Core Invariants  
AXIOM is not just a library; it is a rigid physical system governed by strict invariants:

- Zero Dynamic Allocation: `new` and `malloc` are strictly forbidden in the hot path. All memory is pre-warmed and managed via `axiom::pool`.  
- Zero Hidden Synchronization: Mutexes, locks, and `std::shared_ptr` do not exist here. Threads communicate exclusively via cache-line padded conduit ring buffers.  
- Physical Determinism: Backpressure is handled elegantly. Saturated conduits yield deterministically, instantly reclaiming `event_ptr` memory in O(1) time without deadlocks.  
- Structural Honesty: Events use Composition, not Inheritance. No vptr bloat, no fragmented memory layouts.

🚀 Quickstart: The Physical Topography  
AXIOM requires explicit architectural definitions. You do not just "dispatch" an event; you allocate memory, define a logical pipeline, bind it to a physical sink, map it to a conduit, and route it through a cluster.

```cpp
#include <axiom_conduit/runtime.hpp>
#include <iostream>

// 1. Define an event using Composition (Fixed Memory Footprint)
struct tick_data { double price; };
struct tick_event : axiom::allocated_event<tick_event, 10> {
    tick_data data;
    tick_event(double p) : data{p} {}
};

// 2. Define a zero-overhead CRTP handler
struct tick_processor : axiom::handler_base<tick_processor> {
    void on(axiom::event_ptr<tick_event>& ev) {
        if (ev) std::cout << "Processed tick: $" << ev->data.price << "n";
    }
};

int main() {
    // 3. Initialize O(1) Deterministic Memory Domain
    axiom::runtime_domain<tick_event> domain;
    auto& pool = domain.get_pool<tick_event>();

    // 4. Build Logical Execution Pipeline
    tick_processor processor;
    axiom::pipeline<tick_processor> pipe(processor);

    // 5. Create Physical Topologies (Sink & Conduit)
    axiom::bound_sink<decltype(pipe), tick_event> sink(pipe);
    axiom::conduit<tick_event, 1024> track;
    track.bind(&sink);

    // 6. Map to a Spatial Cluster Router
    axiom::cluster<256> core_router;
    core_router.bind<tick_event>(track);

    // 7. O(1) Allocation -> Lock-Free Route -> Process
    core_router.send(domain.make<tick_event>(150.50));

    // Simulate background worker thread popping the conduit
    if (auto ev = track.pop(pool)) {
        sink.handle(std::move(ev)); // Memory is reclaimed when 'ev' goes out of scope
    }

    return 0;
}
```

🧭 Advanced Routing: Spatial Switches & Pollers  
AXIOM explicitly bans Multi-Producer/Multi-Consumer (MPMC) queues to prevent False Sharing and L1 cache invalidation storms. For complex topologies, AXIOM provides O(1) deterministic Fan-Out and Fan-In primitives.

```cpp
// Distributing 1 Million events across 2 isolated hardware threads
axiom::conduit<trade_event, 4096> shard_0;
axiom::conduit<trade_event, 4096> shard_1;

// O(1) Deterministic Fan-Out Switch
axiom::round_robin_switch<trade_event, 2, 4096> balancer;
balancer.bind_track(0, shard_0);
balancer.bind_track(1, shard_1);

axiom::cluster<256> core;
core.bind<trade_event>(balancer);

// Alternates perfectly between shard_0 and shard_1 without lock contention
core.send(domain.make<trade_event>(...));
```

📂 Repository Structure  
- /include/axiom_conduit/runtime.hpp — The monolithic C++20 core header.  
- /examples/ — 13 step-by-step architectural blueprints (from basic pipelines to OS-level graceful draining).  
- /tests/ — GoogleTest suite proving memory physics, ABA protection, and layout invariants.  
- /docs/ — In-depth System Architect references, ADRs, and SRE Runbooks.  
- /benchmarks/ — HDR Histogram and Google Benchmark methodologies for P99.99 nanosecond latency validation.

Built for the microsecond. Architected for the nanosecond.

## ⚡ Performance & Determinism

AXIOM is engineered for deterministic, sub‑microsecond event routing. Benchmarks show that the runtime operates near the physical limits of the hardware.

| Benchmark               | Latency (Mean) | Iterations   |
|-------------------------|----------------|--------------|
| **BM_Conduit_Push**     | **1.77–1.87 ns** | 407,272,727  |
| **BM_Conduit_FullFlux** | **11.8 ns**      | 64,000,000   |

> **Hardware:** AMD Ryzen 5 9600X (6C/12T @ 3.91 GHz)  
> **Architecture:** Zen 5 (Granite Ridge, TSMC 4 nm)  
> **Instruction Set:** AVX‑512, AVX‑VNNI, FMA3, SHA  
> **Cache Hierarchy:**  
> • L1 Data: 48 KiB (12‑way)  
> • L1 Instruction: 32 KiB (8‑way)  
> • L2: 1 MiB per core (8‑way)  
> • L3: 32 MiB shared (16‑way)  
> **Environment:** Windows 11 Pro (Build 22631), MSVC v143 (Release)

## 💻 Compiler & Platform Compatibility
AXIOM is a **fully standard** C++20 library with no platform-specific dependencies, ensuring maximum portability across high-performance environments.

**Current Reference Implementation**
The current reference build targets the following toolchain:
- Compiler: MSVC C++20 (v143 or newer) 
- Architecture: Windows x64 
- Standard Library: Fully compliant C++20 STL 

### Roadmap & Cross-Compiler Support
While the core logic relies strictly on standard C++ features (Concepts, Atomics, Memory Barriers), cross-compiler support is being actively integrated into the public roadmap:
- **Clang 15+**: (Planned) Verification of LLVM-based optimization and instruction pipeline analysis.
- **GCC 12+**: (Planned) Linux-specific kernel tuning and io_uring integration examples.

## ⚖️ Licensing & Commercial Support

AXIOM is released under the **GNU Affero General Public License v3 (AGPLv3)**. 

### What this means
If you use AXIOM in a service (even behind a network), you must release your entire source code under the same AGPLv3 license. This ensures the technology remains open and collaborative.

### Commercial Licensing (The Enterprise Path)
For organizations that cannot comply with the AGPLv3 requirements (e.g., High-Frequency Trading firms, proprietary banking backends, closed-source telecom stacks), we offer a **Commercial License**.

The Commercial License provides:
- Permission to use AXIOM in a closed-source, proprietary environment.
- No obligation to share your source code.
- Direct architectural support and priority bug fixes.
- Enterprise-only modules (e.g., specialized Layer 8 network drivers).

For pricing and legal inquiries, please contact: kbartadev@gmail.com