#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "axiom/axiom.hpp"

using namespace axiom;

// ============================================================================
// 1. COMPOSITION-BASED “INHERITANCE” (Memory-safe structural hierarchy)
//    No OOP inheritance, no vtables, no fragmentation — just clean,
//    explicit data layering with deterministic layout.
// ============================================================================
struct level_1_data {
    int id;
};
struct level_2_data {
    int extra_x;
};
struct level_3_data {
    int extra_y;
};

// Base-level event: carries only Level 1 data.
struct base_event : allocated_event<base_event, 1> {
    level_1_data l1;
    base_event(int i) : l1{i} {}
};

// Mid-level event: extends the structure with Level 2 data.
struct derived_event : allocated_event<derived_event, 2> {
    level_1_data l1;
    level_2_data l2;
    derived_event(int i, int x) : l1{i}, l2{x} {}
};

// Most specialized event: includes all three layers.
struct specialized_event : allocated_event<specialized_event, 3> {
    level_1_data l1;
    level_2_data l2;
    level_3_data l3;
    specialized_event(int i, int x, int y) : l1{i}, l2{x}, l3{y} {}
};

// ============================================================================
// 2. C++20 CONCEPTS — zero‑cost structural matching.
//    These replace dynamic_cast and SFINAE gymnastics with clean,
//    compile‑time structural constraints.
// ============================================================================
template <typename T>
concept HasLevel1 = requires(T a) { a.l1; };
template <typename T>
concept HasLevel2 = requires(T a) {
    a.l1;
    a.l2;
};
template <typename T>
concept HasLevel3 = requires(T a) {
    a.l1;
    a.l2;
    a.l3;
};

// ============================================================================
// 3. HIERARCHICAL PROCESSORS
//    Each processor handles events that satisfy its structural concept.
//    Execution order is explicit and deterministic.
// ============================================================================
struct base_processor : handler_base<base_processor> {
    std::vector<std::string>& log;
    base_processor(std::vector<std::string>& l) : log(l) {}

    // Matches any event containing Level 1 data.
    template <HasLevel1 Ev>
    void on(event_ptr<Ev>& ev) {
        if (ev) log.push_back("base_logic_executed");
    }
};

struct derived_processor : handler_base<derived_processor> {
    std::vector<std::string>& log;
    derived_processor(std::vector<std::string>& l) : log(l) {}

    // Matches any event containing Level 1 + Level 2 data.
    template <HasLevel2 Ev>
    void on(event_ptr<Ev>& ev) {
        if (ev) log.push_back("derived_logic_executed");
    }
};

struct specialized_processor : handler_base<specialized_processor> {
    std::vector<std::string>& log;
    specialized_processor(std::vector<std::string>& l) : log(l) {}

    // Matches any event containing Level 1 + Level 2 + Level 3 data.
    template <HasLevel3 Ev>
    void on(event_ptr<Ev>& ev) {
        if (ev) log.push_back("specialized_logic_executed");
    }
};

// ============================================================================
// 4. TEST: Execution follows conceptual inheritance order.
//    More general handlers fire first, more specialized ones later.
//    No RTTI, no dynamic dispatch — pure compile‑time structural matching.
// ============================================================================
TEST(HierarchicalExecution, pipeline_executes_in_conceptual_inheritance_order) {
    pool<base_event> pool_base(5);
    pool<derived_event> pool_derived(5);
    pool<specialized_event> pool_spec(5);

    std::vector<std::string> execution_log;

    base_processor bp(execution_log);
    derived_processor dp(execution_log);
    specialized_processor sp(execution_log);

    // Pipeline order: Base → Derived → Specialized.
    // This order is fixed at compile time and fully inlined.
    pipeline<base_processor, derived_processor, specialized_processor> pipe(bp, dp, sp);

    // --- TEST 1: Base event -------------------------------------------------
    auto ev_base = pool_base.make(10);
    pipe.dispatch(ev_base);

    ASSERT_EQ(execution_log.size(), 1);
    EXPECT_EQ(execution_log[0], "base_logic_executed");  // Only base-level logic applies.

    execution_log.clear();

    // --- TEST 2: Derived event ----------------------------------------------
    auto ev_derived = pool_derived.make(10, 20);
    pipe.dispatch(ev_derived);

    ASSERT_EQ(execution_log.size(), 2);
    // Structural “inheritance” ordering: base → derived.
    EXPECT_EQ(execution_log[0], "base_logic_executed");
    EXPECT_EQ(execution_log[1], "derived_logic_executed");

    execution_log.clear();

    // --- TEST 3: Fully specialized event ------------------------------------
    auto ev_spec = pool_spec.make(10, 20, 30);
    pipe.dispatch(ev_spec);

    ASSERT_EQ(execution_log.size(), 3);
    // Full hierarchy executes in perfect order.
    EXPECT_EQ(execution_log[0], "base_logic_executed");
    EXPECT_EQ(execution_log[1], "derived_logic_executed");
    EXPECT_EQ(execution_log[2], "specialized_logic_executed");
}
