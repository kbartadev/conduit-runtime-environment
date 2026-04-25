/**
 * @file 03_zero_allocation_hotpath.cpp
 * @brief Enterprise Hot-Path: Zero-Allocation Dispatch & Const-Correctness
 * * * This example demonstrates the deepest foundations of the CRE library:
 * 1. Preallocated memory (Pool) to avoid dynamic allocations.
 * 2. Vertical inheritance (L0 -> L1) with zero copying.
 * 3. Strict memory safety: The pipeline guarantees who may read and who may
 *    modify by using const and non-const references.
 */

#include <iostream>
#include <string_view>
#include "conduit/core.hpp"

using namespace cre;

// ============================================================================
// 1. DATA STACK (Zero-Copy)
// ============================================================================

// L0: Standard FIX (Financial Information eXchange) header
struct fix_header {
    uint32_t seq_num;
    char     msg_type;
};

// L1: Payload (Dedicated event originating from the memory pool)
struct new_order_single : extends<allocated_event<new_order_single, 128>, fix_header> {
    std::string_view symbol;
    double           price;
    uint64_t         internal_ts = 0; // We mutate this later

    new_order_single(uint32_t seq, std::string_view sym, double p)
        : symbol(sym), price(p) {
        this->seq_num = seq;
        this->msg_type = 'D'; // 'D' = New Order Single in the FIX protocol
    }
};

// ============================================================================
// 2. DISPATCH STAGES (Read vs. Mutate)
// ============================================================================

// Stage 1: Sequence Validator (READ-ONLY – safe 'const' reference)
struct sequence_validator : handler_base<sequence_validator> {
    bool on(const event_ptr<fix_header>& ev) {
        if (ev->seq_num == 0) {
            std::cout << "[REJECT] Invalid sequence number: 0\n";
            return false; // Short-circuit
        }
        std::cout << "[VALID] Sequence #" << ev->seq_num << " accepted.\n";
        return true;
    }
};

// Stage 2: Ingress Timestamping (MUTATES – requires 'non-const' reference)
struct hardware_timestamper : handler_base<hardware_timestamper> {
    void on(event_ptr<new_order_single>& ev) {
        ev->internal_ts = 1680000000000ULL; // Simulated hardware clock
        std::cout << "[HW-TAG] Event stamped at " << ev->internal_ts << " ns.\n";
    }
};

// Stage 3: Matching Engine (READ-ONLY – for execution)
struct matching_engine : handler_base<matching_engine> {
    void on(const event_ptr<new_order_single>& ev) {
        std::cout << "[EXEC] Routing " << ev->symbol << " @ $" << ev->price
                  << " to matching core.\n";
    }
};

// ============================================================================
// 3. EXECUTION
// ============================================================================

int main() {
    std::cout << "=== Showcase 01: Zero-Allocation Hot Path ===\n\n";

    pool<new_order_single> memory(1024); // 1024 slots preallocated

    sequence_validator   validator;
    hardware_timestamper stamper;
    matching_engine      engine;

    // Building the pipeline
    pipeline<sequence_validator, hardware_timestamper, matching_engine>
        core_pipeline(validator, stamper, engine);

    std::cout << "--- Tick 1: Valid Order ---\n";
    core_pipeline.dispatch(memory.make(101, "AAPL", 150.25));

    std::cout << "\n--- Tick 2: Corrupted Order ---\n";
    core_pipeline.dispatch(memory.make(0, "TSLA", 200.00)); // Seq = 0, will fail!

    return 0;
}
