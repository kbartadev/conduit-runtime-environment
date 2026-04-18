#include <gtest/gtest.h>

#include <atomic>

#include "axiom_conduit/core.hpp"

using namespace axiom;

struct load_event : allocated_event<load_event, 42> {};

// Event type that increments/decrements an external counter on lifetime changes.
// Used to prove that unmatched events are still fully reclaimed.
struct ghost_event : allocated_event<ghost_event, 13> {
    std::atomic<int>* alive_counter;
    ghost_event(std::atomic<int>* c) : alive_counter(c) {
        if (alive_counter) alive_counter->fetch_add(1);
    }
    ~ghost_event() {
        if (alive_counter) alive_counter->fetch_sub(1);
    }
};

// Handler that only accepts load_event.
// Any other event type must be silently ignored by the pipeline.
struct specific_handler : handler_base<specific_handler> {
    void on(event_ptr<load_event>&) { FAIL() << "Should not be called!"; }
};

// ============================================================================
// TEST: Pipelines must ignore unmatched event types AND still reclaim memory.
// No crashes, no undefined behavior, no leaks.
// ============================================================================
TEST(PipelineEdgeCases, pipeline_safely_ignores_unmatched_events_and_reclaims_memory) {
    std::atomic<int> alive{0};
    pool<ghost_event> p(5);

    specific_handler sh;
    pipeline<specific_handler> pipe(sh);

    {
        auto ev = p.make(&alive);
        EXPECT_EQ(alive.load(), 1);

        // Dispatch an event type that the handler does not accept.
        // Expected behavior: pipeline walks the handler list, finds no match,
        // performs no calls, and returns immediately with zero side effects.
        pipe.dispatch(ev);

        // The event must still be alive until the end of this scope.
        EXPECT_EQ(alive.load(), 1);
    }

    // Leaving the scope returns the event to the pool.
    // Destructor must run exactly once → alive counter returns to zero.
    EXPECT_EQ(alive.load(), 0);
}
