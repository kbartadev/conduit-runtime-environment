/**
 * @file 04_ultimate_logic_synthetis.cpp
 * @brief Enterprise HFT Matrix: Inheritance, Interfaces, Concepts & Mutation.
 * * This single file demonstrates all capabilities of the CRE library:
 * 1. Wire Layer: Hardware timestamps (Inheritance)
 * 2. Compliance Layer: Pure virtual interfaces (Abstract Base)
 * 3. Risk Layer: C++20 “Duck-typing” (Concepts)
 * 4. Accounting Layer: Runtime mutation of events (Non-const dispatch)
 * 5. Execution: Zero-cost, short-circuit pipeline.
 */

#include <iostream>
#include <iomanip>
#include <string>
#include "conduit/core.hpp"

using namespace cre;

// ============================================================================
// 1. THE VERTICAL MATRIX: Inheritance and Interface Layers
// ============================================================================

// L0: Network Metadata (Hardware layer)
struct wire_metadata {
    uint64_t capture_ns;
    std::string ip_address;
};

// L1: Regulatory Interface (Pure virtual Base)
struct compliance_interface {
    virtual ~compliance_interface() = default;
    virtual bool is_kyc_cleared() const noexcept = 0;
};

// L2: The Final HFT Event (From the memory pool)
// Multiple inheritance for the Matrix, in a single 128‑byte block!
struct direct_market_order : extended_event<direct_market_order, 128,
    wire_metadata,
    compliance_interface> {
    uint32_t order_id;
    double   price;
    double   quantity;
    bool     kyc_approved;

    direct_market_order(const char* ip, uint32_t id, double p, double q, bool kyc)
        : order_id(id), price(p), quantity(q), kyc_approved(kyc) {
        // Initializing bases inside the constructor body (MSVC compatibility)
        this->capture_ns = 1682390400000000000ULL;
        this->ip_address = ip;
    }

    // Implementing the abstract interface
    bool is_kyc_cleared() const noexcept override {
        return kyc_approved;
    }
};

// ============================================================================
// 2. CONCEPTS: Structural Discovery
// ============================================================================

// Any event from which financial exposure can be computed
template <typename T>
concept HasExposure = requires(T a) { a.price; a.quantity; };

// ============================================================================
// 3. THE PIPELINE HANDLERS: 5 Different Engineering Patterns
// ============================================================================

// Stage 1: Latency Monitor (Reads only the network layer – CONST)
struct latency_monitor : handler_base<latency_monitor> {
    void on(const event_ptr<wire_metadata>& ev) {
        std::cout << "[L0 INFRA] Packet from " << ev->ip_address
            << " | TS: " << ev->capture_ns << " ns\n";
    }
};

// Stage 2: Compliance Auditor (Calls the abstract interface – BOOL SHORT‑CIRCUIT)
struct compliance_auditor : handler_base<compliance_auditor> {
    bool on(const event_ptr<compliance_interface>& ev) {
        if (!ev->is_kyc_cleared()) {
            std::cout << "[L1 COMPLIANCE] REJECTED: KYC validation failed.\n";
            return false; // Stops the pipeline!
        }
        std::cout << "[L1 COMPLIANCE] APPROVED: KYC valid.\n";
        return true;
    }
};

// Stage 3: Risk Gatekeeper (Concept‑based filtering – DUCK‑TYPING)
struct risk_gatekeeper : handler_base<risk_gatekeeper> {
    template <HasExposure E>
    bool on(const event_ptr<E>& ev) {
        double exposure = ev->price * ev->quantity;
        if (exposure > 1'000'000.0) {
            std::cout << "[L2 RISK] REJECTED: Exposure too high ($"
                << std::fixed << std::setprecision(2) << exposure << ")\n";
            return false;
        }
        std::cout << "[L2 RISK] APPROVED: Exposure within limits.\n";
        return true;
    }
};

// Stage 4: Accounting (Runtime Mutation – NON‑CONST)
struct accounting_processor : handler_base<accounting_processor> {
    void on(event_ptr<direct_market_order>& ev) { // NO CONST!
        double exchange_fee = ev->quantity * 0.01;
        ev->price += exchange_fee; // Mutate price with fee
        std::cout << "[L2 ACCOUNTING] Added exchange fee. New routing price: $"
            << ev->price << "\n";
    }
};

// Stage 5: Execution Engine (The leaf executor)
struct execution_engine : handler_base<execution_engine> {
    void on(const event_ptr<direct_market_order>& ev) {
        std::cout << "[L2 EXECUTION] Routing Order #" << ev->order_id
            << " to matching engine.\n";
    }
};

// ============================================================================
// 4. THE ORCHESTRATION
// ============================================================================

int main() {
    std::cout << "=== Conduit Ultimate Logic Synthesis ===\n\n";

    // 1. Memory preallocation
    pool<direct_market_order> engine_pool(256);

    // 2. Instantiate handlers
    latency_monitor       infra;
    compliance_auditor    auditor;
    risk_gatekeeper       risk;
    accounting_processor  accounting;
    execution_engine      exec;

    // 3. Define the pipeline (Horizontal chain)
    pipeline<latency_monitor, compliance_auditor, risk_gatekeeper, accounting_processor, execution_engine>
        matrix_pipe(infra, auditor, risk, accounting, exec);

    std::cout << "--- SCENARIO 1: Valid Retail Order ---\n";
    auto valid_order = engine_pool.make("10.0.1.55", 1001, 150.0, 100.0, true);
    matrix_pipe.dispatch(valid_order);
    // Passes all 5 stages: Logs, Approves, Approves, Adds fee, Executes.

    std::cout << "\n--- SCENARIO 2: Toxic Whale Order (Risk Breach) ---\n";
    auto toxic_order = engine_pool.make("10.0.1.99", 1002, 200.0, 10000.0, true);
    matrix_pipe.dispatch(toxic_order);
    // Stops at stage 3 (Risk). Accounting and Execution do not run.

    std::cout << "\n--- SCENARIO 3: Illegal Order (KYC Failed) ---\n";
    auto illegal_order = engine_pool.make("192.168.1.1", 1003, 50.0, 10.0, false);
    matrix_pipe.dispatch(illegal_order);
    // Stops at stage 2 (Compliance). No further processing.

    return 0;
}
