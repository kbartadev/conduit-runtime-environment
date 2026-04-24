#include <gtest/gtest.h>

#include <type_traits>

#include "conduit/core.hpp"

using namespace cre;

struct dummy_event : allocated_event<dummy_event, 10> {
    int data;
};

// ============================================================================
// 1. INVARIANT: Global new/delete must be forbidden for allocated_event types.
//    This ensures:
//      - No heap fallback
//      - No fragmentation
//      - No bypass of the deterministic slab allocator
//    We test this via SFINAE: if `new T()` is well-formed, the invariant is broken.
// ============================================================================
template <typename T, typename = void>
struct is_new_allocatable : std::false_type {};

template <typename T>
struct is_new_allocatable<T, std::void_t<decltype(new T())>> : std::true_type {};

TEST(TypeSafetyInvariants, allocated_event_forbids_global_heap_allocation) {
    // If this fails, someone re-enabled global operator new/delete on allocated_event.
    EXPECT_FALSE(is_new_allocatable<dummy_event>::value);
}

// ============================================================================
// 2. INVARIANT: event_ptr enforces strict unique ownership.
//    - Not copyable (prevents aliasing, double frees, and ownership ambiguity)
//    - Must be move-constructible (ownership transfer is required for routing)
// ============================================================================
TEST(TypeSafetyInvariants, event_ptr_enforces_strict_unique_ownership) {
    EXPECT_FALSE(std::is_copy_constructible_v<event_ptr<dummy_event>>);
    EXPECT_FALSE(std::is_copy_assignable_v<event_ptr<dummy_event>>);

    // Move semantics are mandatory for deterministic routing.
    EXPECT_TRUE(std::is_move_constructible_v<event_ptr<dummy_event>>);
}
