#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "conduit/core.hpp"

using namespace cre;

// --- Event type ------------------------------------------------------------
// Static identity (ID = 30), no RTTI, no dynamic inspection.
// Carries immutable trade metadata and a mutable amount field.
struct financial_trade : allocated_event<financial_trade, 30> {
    int trade_id;
    double amount;
    financial_trade(int id, double a) : trade_id(id), amount(a) {}
};

// --- Independent handler stages --------------------------------------------

// 1. Validation stage: records the trade ID, no mutation.
struct validator_stage : handler_base<validator_stage> {
    std::vector<std::string> log;
    void on(event_ptr<financial_trade>& ev) {
        if (ev) log.push_back("validated:" + std::to_string(ev->trade_id));
    }
};

// 2. Risk stage: performs a risk adjustment and mutates the event payload.
struct risk_stage : handler_base<risk_stage> {
    std::vector<std::string> log;
    void on(event_ptr<financial_trade>& ev) {
        if (ev) {
            log.push_back("risk_checked");
            ev->amount *= 0.9;  // Apply 10% deduction.
        }
    }
};

// 3. Audit stage: observes the final amount after all prior mutations.
struct audit_stage : handler_base<audit_stage> {
    std::vector<std::string> log;
    void on(event_ptr<financial_trade>& ev) {
        if (ev) {
            log.push_back("audited_amount:" + std::to_string(static_cast<int>(ev->amount)));
        }
    }
};

// ============================================================================
// TEST GOAL: Verify that the pipeline executes all matching handlers
//            strictly in declaration order AND that mutations propagate
//            deterministically through the chain.
// ============================================================================
TEST(FullDispatchChain, executes_all_matching_handlers_in_declaration_order) {
    pool<financial_trade> trade_pool(10);

    validator_stage val;
    risk_stage risk;
    audit_stage aud;

    // Pipeline order: Validator → Risk → Audit.
    // This order is compile-time fixed and fully inlined.
    pipeline<validator_stage, risk_stage, audit_stage> pipe(val, risk, aud);

    auto trade = trade_pool.make(777, 100.0);

    // Push the event through the entire chain.
    pipe.dispatch(trade);

    // Each stage must have executed exactly once.
    ASSERT_EQ(val.log.size(), 1);
    EXPECT_EQ(val.log[0], "validated:777");

    ASSERT_EQ(risk.log.size(), 1);
    EXPECT_EQ(risk.log[0], "risk_checked");

    ASSERT_EQ(aud.log.size(), 1);

    // Mutation proof:
    // Risk stage applies 10% deduction → 100 * 0.9 = 90.
    // If the order were incorrect, Audit would still see 100.
    EXPECT_EQ(aud.log[0], "audited_amount:90");
}
