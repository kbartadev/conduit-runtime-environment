#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "axiom_conduit/core.hpp"

using namespace axiom;

// ============================================================================
// 1. COMPOSITION‑BASED EVENT (“Inheritance”)
//    No OOP hierarchy, no vtables — just explicit structural layering.
// ============================================================================
struct base_layer {
    int id;
};
struct derived_layer {
    int x;
};
struct specialized_layer {
    int y;
};

// A single fixed‑size memory block containing all “ancestor” layers.
// Deterministic layout, zero fragmentation, zero dynamic polymorphism.
struct complex_event : allocated_event<complex_event, 42> {
    base_layer base;
    derived_layer derived;
    specialized_layer spec;

    complex_event(int i, int x, int y) : base{i}, derived{x}, spec{y} {}
};

// ============================================================================
// 2. MULTI‑OVERLOAD HANDLER
//    Each layer has its own overload — clean separation of logic.
// ============================================================================
struct cascading_handler : handler_base<cascading_handler> {
    std::vector<std::string> log;

    void on(base_layer& b) { log.push_back("base:" + std::to_string(b.id)); }

    void on(derived_layer& d) { log.push_back("derived:" + std::to_string(d.x)); }

    void on(specialized_layer& s) { log.push_back("specialized:" + std::to_string(s.y)); }

    // Main entry point from the pipeline.
    // Explicit Control:
    // The strict execution order is defined *here*, not inferred.
    void on(event_ptr<complex_event>& ev) {
        if (!ev) return;

        // The compiler inlines this into a single, strictly ordered block.
        on(ev->base);
        on(ev->derived);
        on(ev->spec);
    }
};

// ============================================================================
// 3. TEST — Execution must follow the exact composition order.
//    One handler, three overloads, perfect deterministic sequencing.
// ============================================================================
TEST(SingleHandlerCascade, executes_internal_overloads_in_strict_composition_order) {
    pool<complex_event> p(5);
    cascading_handler h;
    pipeline<cascading_handler> pipe(h);

    auto ev = p.make(10, 20, 30);
    pipe.dispatch(ev);

    ASSERT_EQ(h.log.size(), 3);
    EXPECT_EQ(h.log[0], "base:10");
    EXPECT_EQ(h.log[1], "derived:20");
    EXPECT_EQ(h.log[2], "specialized:30");
}
