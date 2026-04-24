#include <gtest/gtest.h>

#include "conduit/core.hpp"

using namespace cre;

struct massive_event : allocated_event<massive_event, 99> {};

struct math_event : allocated_event<math_event, 42> {
    int value = 0;
};

// Handler 1: adds +5
struct adder : handler_base<adder> {
    void on(event_ptr<math_event>& ev) { ev->value += 5; }
};

// Handler 2: multiplies by 2
struct multiplier : handler_base<multiplier> {
    void on(event_ptr<math_event>& ev) { ev->value *= 2; }
};

// Handler 3: expects a different event type — must NEVER be invoked
struct dummy : handler_base<dummy> {
    void on(event_ptr<massive_event>&) { FAIL() << "Type-system routing failed!"; }
};

// ============================================================================
// TEST 1 — Fold-expression semantics: mutation order must be exact.
// The pipeline must execute handlers strictly in declaration order,
// skipping handlers whose type signature does not match.
// ============================================================================
TEST(PipelineSemantics, fold_expression_maintains_exact_mutation_order) {
    pool<math_event> p(10);
    adder add;
    multiplier mult;
    dummy dum;

    // Order: Add → Dummy (skipped) → Multiply
    pipeline<adder, dummy, multiplier> pipe(add, dum, mult);

    auto ev = p.make();
    pipe.dispatch(ev);

    // (0 + 5) * 2 = 10. Any deviation in order yields the wrong result.
    EXPECT_EQ(ev->value, 10);
}

// ============================================================================
// TEST 2 — Short-circuit semantics: consuming the event must halt the chain.
// Once a handler resets the event_ptr, no subsequent handler may run.
// ============================================================================
TEST(PipelineSemantics, pipeline_safely_short_circuits_if_event_is_consumed) {
    pool<math_event> p(10);

    struct consumer : handler_base<consumer> {
        void on(event_ptr<math_event>& ev) {
            ev.reset();  // Consumes the event — ownership collapses here.
        }
    };

    struct panicker : handler_base<panicker> {
        void on(event_ptr<math_event>&) { FAIL() << "Executed after consumption!"; }
    };

    consumer cons;
    panicker pan;
    pipeline<consumer, panicker> pipe(cons, pan);

    auto ev = p.make();
    pipe.dispatch(ev);  // panicker must never execute.

    SUCCEED();
}
