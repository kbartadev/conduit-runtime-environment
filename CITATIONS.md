# Technical & Algorithmic Dossier: Conduit Runtime Environment (CRE)

The Conduit Runtime Environment (CRE) is engineered upon rigorously vetted computer science paradigms and processor-specific hardware optimizations. This document explicitly traces the methodologies governing the engine's **$mathcal{O}(1)$** physical guarantees directly to their implementation in the source code.

### I. Memory Architecture & Lock-Free Allocation

* **$mathcal{O}(1)$ Slab Memory Management (`cre::pool`)**: The engine manages memory through a union-based slab allocator to ensure standard-compliant memory aliasing and prevent the latency of default-constructing millions of objects during startup [cite: 1719-1721, 1746-1750]. Raw memory is mapped directly to physical RAM, leveraging placement `new` exclusively at the time of event generation [cite: 1783-1784].
* **Wait-Free Memory Reclamation (Treiber Stack)**: Memory allocation (`pool::make`) and reclamation (`pool::free`) are executed in strict $mathcal{O}(1)$ time via non-blocking atomic operations [cite: 1762-1788, 1796-1808].
    * *Citation: Treiber, R. K. (1986). "Systems programming: Coping with parallelism." IBM Almaden Research Center.*
* **ABA-Protected Free-List**: To prevent the ABA problem inherent in lock-free stacks, the pool utilizes a 64-bit atomic state (`free_head_`). This implementation employs a tagged-pointer strategy where the upper 32 bits contain an incrementing version tag and the lower 32 bits store the index of the next available cell [cite: 1763, 1773-1774, 1803-1804].
* **Intrusive Static Destruction**: Bypassing the 24-byte bloat and indirect-branch penalties of standard type-erasure (e.g., `std::shared_ptr`), the system utilizes a 16-byte `event_ptr` [cite: 1595-1597]. The polymorphic destruction route is stored as a static function pointer (`release_to_pool`) embedded in the physical memory block header, ensuring branch-predictable cleanup without virtual tables [cite: 1730-1731, 1677].

### II. Concurrency & Hardware Physics

* **SPSC Wait-Free Transport (`cre::conduit`)**: Cross-thread data transfer is physically restricted to Single-Producer/Single-Consumer (SPSC) models, eliminating the need for OS-level Mutex locking or conditional variables [cite: 1829-1835]. The conduit utilizes atomic `write_idx_` and `read_idx_` variables to guarantee wait-free execution [cite: 1819-1820].
    * *Citation: Herlihy, M. (1991). "Wait-free synchronization." ACM.*
* **False Sharing Mitigation (L1 Cache Padding)**: CPU core contention over shared memory lines is neutralized by explicitly aligning atomic indices to 64-byte boundaries (`alignas(CACHE_LINE_SIZE)`) [cite: 1539, 1819-1820]. This prevents the "ping-pong" effect where the CPU interconnect is forced to flush L1 cache lines between cores.
    * *Citation: Intel® 64 and IA-32 Architectures Optimization Reference Manual.*
* **Bitwise Masking Mathematics**: For optimized index wrapping, the system uses logical bitwise AND operations (`idx & (Size - 1)`) instead of 15-cycle modulo division (%), ensuring index resets execute in a single CPU cycle [cite: 108-109, 1831].

### III. Template Metaprogramming & Compile-Time Routing

* **Compile-Time Topological Unwinding**: The `cre::pipeline` utilizes fold expressions and recursive template meta-programming (`traverse_and_call`) to resolve multiple-inheritance event graphs [cite: 1910, 1934-1940]. If a handler does not subscribe to a specific layer of an event, the compiler discards that branch entirely, resulting in zero-overhead dispatch [cite: 1972-1984].
* **Deterministic Pipeline Short-Circuiting**: The `call_on_if_exists` mechanic supports boolean return types from handlers [cite: 1973-1976]. This allows a stage to stop the entire pipeline execution (e.g., for risk management) without any additional branch overhead in the success path [cite: 372-376].
* **Non-Owning Base-Class Views**: To allow handlers to process base-class abstractions without prematurely reclaiming memory, CRE implements a `non_owning_tag` for `event_ptr` [cite: 1586, 1661-1663]. This facilitates safe hierarchical traversal (`traverse_and_call`) across the event's `base_types` [cite: 1937-1940].
* **Static Identity & Zero RTTI**: Events carry a compile-time fixed ID and use `cre::extends` (powered by `std::tuple` of base pointers) for structural hierarchy [cite: 1560-1563]. This ensures zero-overhead dispatch and replaces dynamic type inspection with static identity resolution [cite: 194-196, 1580].

#### IV. Third-Party Dependencies & Test Frameworks

* The CRE utilizes the following open-source frameworks strictly for unit testing, structural validation, and microbenchmarking. These dependencies are NOT linked into the proprietary production binaries.

* **Google Test (gtest)**
    * *Usage*: Unit testing of the deterministic memory pool and hierarchical event dispatching .
    * *License*: BSD-3-Clause License.
    * *Copyright*: Copyright 2008, Google Inc. All rights reserved.

* **Google Benchmark**
    * *Usage*: Microbenchmarking O(1) allocation latency and pipeline throughput .
    * *License*: Apache License 2.0.
    * *Copyright*: Copyright 2015, Google Inc. All rights reserved.

### V. Benchmark Validation

Performance metrics were captured on a standard bare-metal environment to validate the $mathcal{O}(1)$ invariants [cite: 27-42]:
* **BM_Conduit_Push**: 1.72 ns mean latency [cite: 38-41].
* **BM_Conduit_FullFlux**: 12.2 ns mean latency [cite: 40-41].
* **Hardware**: AMD Ryzen 5 9600X (12 X 3893 MHz CPU) [cite: 30-35].

### VI. Licensing & Professional Attribution

* **Author**: Kristóf Barta [cite: 1508-1509]
* **Copyright**: © 2026 Kristóf Barta. All rights reserved [cite: 1509-1510].
* **License**: Dual-licensed under the **GNU Affero General Public License v3 (AGPLv3)** and **Proprietary Commercial License** [cite: 1510-1511].