# CONDUIT Runtime Environment ⚡  

**CONDUIT** is an event routing and processing framework designed for High-Frequency Trading (HFT).

Built strictly on C++20 Concepts, CONDUIT eliminates Object-Oriented runtime overhead (no RTTI, no virtual tables).

🚀 Quickstart: The Physical Topography

CONDUIT requires explicit architectural definitions. You do not just "push" an event; you define a logical pipeline, bind it to a physical sink, map it to a conduit, and route it through a cluster.

📂 Repository Structure

- /include/conduit/
- /examples/ 
- /tests/
- /docs/
- /benchmarks/

Built for the microsecond. Architected for the nanosecond.

## ⚡ Performance & Determinism
CONDUIT is engineered for high-performance event routing.

| Benchmark               | Latency (Mean) | Iterations   |
|-------------------------|----------------|--------------|
| **BM_Conduit_Push**     | **1.77–1.87 ns** | 407,272,727  |
| **BM_Conduit_FullFlux** | **11.8 ns**      | 64,000,000   |

> **Hardware:** AMD Ryzen 5 9600X (6C/12T @ 3.91 GHz)  
> **Architecture:** Zen 5 (Granite Ridge, TSMC 4 nm)  
> **Instruction Set:** AVX‑512, AVX‑VNNI, FMA3, SHA  
> **Cache Hierarchy:**  
> • L1 Data: 48 KiB
> • L1 Instruction: 32 KiB
> • L2: 1 MiB per core
> • L3: 32 MiB shared
> **Environment:**: Windows 11 Pro (Build 22631), MSVC v143 (Release)

## 💻 Compiler & Platform Compatibility
CONDUIT is a C++20 library, ensuring maximum portability across high-performance environments.

**Current Reference Implementation**

The current reference build targets the following toolchain:

- Compiler: MSVC C++20 (v143) 
- Architecture: Windows x64 
- Standard Library: C++20 STL 

### Roadmap & Cross-Compiler Support
While the core logic relies strictly on standard C++ features (Concepts, Atomics, Memory Barriers), cross-compiler support is being actively integrated into the public roadmap:
- **Clang 15+**
- **GCC 12+**

## ⚖️ Licensing & Commercial Support
CONDUIT is released under the **GNU Affero General Public License v3 (AGPLv3)**. 

### What this means
If you use CONDUIT in a service (even behind a network), you must release your entire source code under the same AGPLv3 license. This ensures the technology remains open and collaborative.

### Commercial Licensing (The Enterprise Path)
For organizations that cannot comply with the AGPLv3 requirements (e.g., High-Frequency Trading firms, proprietary banking backends, closed-source telecom stacks), we offer a **Commercial License**.

For pricing and legal inquiries, please contact: kbartadev@gmail.com
