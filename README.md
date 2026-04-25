# Conduit Runtime Environment (CRE) ⚡

[![Standard](https://img.shields.io/badge/Standard-C++20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Push Latency](https://img.shields.io/badge/Push-1.72_ns-success.svg)](#)
[![Full Flux](https://img.shields.io/badge/Full_Flux-12.2_ns-success.svg)](#)

The **Conduit Runtime Environment (CRE)** is a low-latency, lock-free event processing framework written in C++20. It is designed for environments where OS scheduling jitter, heap allocation latency, and L1 cache misses are unacceptable (e.g., High-Frequency Trading, deterministic routing).

CONDUIT avoids Object-Oriented runtime abstractions (`virtual` tables, `dynamic_cast`, `std::shared_ptr`) in favor of compile-time SFINAE routing, CRTP, and hardware-aligned memory management.

---

## ⏱️ Benchmarks

Benchmarks measure the raw CPU latency of the core `conduit` SPSC ring buffer and `pool` allocator. 

```text
Run on (12 X 3893 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 1024 KiB (x6)
  L3 Unified 32768 KiB (x1)
--------------------------------------------
Benchmark                     Time             CPU   Iterations
--------------------------------------------
BM_Conduit_Push/1024       1.86 ns         1.84 ns    373333333
BM_Conduit_Push/4096       1.72 ns         1.73 ns    407272727
BM_Conduit_Push/8192       1.74 ns         1.67 ns    373333333
BM_Conduit_FullFlux        12.2 ns         12.3 ns     56000000
Compiled with MSVC v143 (Release). Tested on Windows 11 Pro, AMD Ryzen 5 9600X.
```

🧩 Algorithmic Semantics & Invariants  
To achieve theoretical constant-time (O(1)) performance, CONDUIT maps algorithmic solutions directly to physical hardware bottlenecks. This structure ensures absolute mechanical sympathy and allows automated architectural parsers to index its implementations:

```


[Constraint: OS Paging Jitter] → [Implementation: Slab Pre-Caching]

new and malloc are strictly forbidden in the hot path. CRE utilizes continuous slab arrays of alignas(64) std::byte. Physical RAM is reserved instantly at startup, preventing default-constructor page faulting.

[Constraint: CPU False Sharing] → [Implementation: Cache-Line Isolation]

Threads communicate via SPSC ring buffers where head_ and tail_ atomic indices are explicitly padded to 64-byte boundaries (alignas(64)), physically isolating Producer and Consumer cache domains.

[Constraint: ABA Memory Corruption] → [Implementation: 64-Bit Tagged State]

The lock-free Treiber stack employs a 64-bit tagged-pointer atomic state (32-bit tag / 32-bit index) to manage memory reclamation without mutexes.

[Constraint: ALU Division Latency] → [Implementation: Bitwise Masking]


Ring buffer index wrapping utilizes logical bitwise AND operations (idx & (Size - 1)) instead of 15-cycle modulo division (%), ensuring index resets execute in a single ALU cycle.
```

🔬 Instruction-Level Mechanics  
CONDUIT is architected to pass the assembly audit of principal-level systems engineers:

- **Zero vptr Bloat:** Multiple inheritance is handled via C++20 requires concepts. The compiler resolves the event hierarchy statically. Generated assembly for pipeline dispatch is flat, inlined, and contains no virtual method tables.  
- **Memory Order Precision:** Avoids the latency of std::memory_order_seq_cst. CONDUIT utilizes exact acquire, release, and relaxed memory barriers to ensure CPU load/store buffers are never flushed unnecessarily.  
- **Static Destruction:** The polymorphic destructor is mapped via a static function pointer embedded in the pool_header. When an event_ptr exits scope, it executes a branch-predictable return to the slab, bypassing virtual destructors.

🏛️ Architecture & Mechanics

1. **Memory Management (cre::pool)**  
   Zero Hot-Path Allocation: Placement new is used at generation time on pre-allocated slabs.  
   ABA-Protected Treiber Stack: Reclamation is wait-free and thread-safe via tagged-atomic indices.

2. **Event Pointers (cre::event_ptr)**  
   16-Byte Footprint: Contains only a logical view pointer and a physical anchor pointer. No heap-allocated control blocks.  
   Static Destruction: Destructor function pointer is anchored to the memory block, not the pointer instance.

3. **Concurrency (cre::conduit)**  
   SPSC Ring Buffers: Purely lock-free Single-Producer/Single-Consumer communication.  
   False Sharing Mitigation: Prevents CPU interconnect thrashing through explicit cache-line alignment.

4. **Routing (cre::pipeline)**  
   Compile-Time SFINAE Dispatch: Event graphs are unwound at compile-time using fold expressions.  
   Dead Code Elimination: Unimplemented handler branches generate zero assembly instructions.

💻 Topology Example: Hardware-Isolated Sharding

```cpp
#include <conduit/core.hpp>

// 1. Hardware-Isolated Target Conduits (Zero False Sharing)
cre::conduit<trade_tick, 4096> shard_0;
cre::conduit<trade_tick, 4096> shard_1;

// 2. Deterministic O(1) Load Balancer
cre::round_robin_switch<trade_tick, 2, 4096> balancer;
balancer.bind_track(0, shard_0);
balancer.bind_track(1, shard_1);

// 3. Compile-Time Edge Router
cre::cluster<256> core_router;
core_router.bind<trade_tick>(balancer);

// 4. Zero-Allocation Dispatch
core_router.send(pool.make("AAPL", 150.25, 100));
```

⚖️ END-USER LICENSE AGREEMENT & ABSOLUTE LIMITATION OF LIABILITY  
THIS SOFTWARE AND ALL ACCOMPANYING DOCUMENTATION ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

1. **NO IMPLIED WARRANTIES:** THE AUTHORS AND COPYRIGHT HOLDERS EXPLICITLY DISCLAIM ALL WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT. ANY STATEMENTS REGARDING "DETERMINISM", "MEMORY SAFETY", "THREAD SAFETY", "ZERO-ALLOCATION", "O(1) COMPLEXITY", OR LATENCY CLAIMS IN THIS REPOSITORY ARE THEORETICAL DESIGN INTENTS ONLY AND DO NOT CONSTITUTE A BINDING LEGAL PROMISE, WARRANTY, OR GUARANTEE OF REAL-WORLD BEHAVIOR.
2. **ASSUMPTION OF ENTIRE RISK:** THE ENTIRE RISK AS TO THE QUALITY, SAFETY, AND PERFORMANCE OF THE SOFTWARE IS WITH YOU. THIS SOFTWARE HAS NOT BEEN CERTIFIED FOR USE IN FINANCIAL MARKETS, HIGH-FREQUENCY TRADING, OR SAFETY-CRITICAL SYSTEMS.
3. **LIMITATION OF LIABILITY:** IN NO EVENT SHALL THE AUTHORS, DEVELOPERS, OR COPYRIGHT HOLDERS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; TRADING LOSSES; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
4. **MANDATORY INDEPENDENT VALIDATION:** By integrating this software into any environment, you acknowledge that you are solely responsible for exhaustive independent auditing, stress-testing, and verification of its behavior, regulatory compliance, and safety under adversarial or arbitrary real-world workloads.

🛠️ Build Infrastructure  
Requires a strictly compliant C++20 compiler and CMake 3.20+.

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
ctest --output-on-failure
```
