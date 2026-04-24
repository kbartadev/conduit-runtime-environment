#include <gtest/gtest.h>

#include "conduit/core.hpp"

using namespace cre;

struct load_event : allocated_event<load_event, 55> {
    int source_track;
    load_event(int s) : source_track(s) {}
};

// ============================================================================
// TEST — Round‑robin poller must prevent starvation even under asymmetric load.
// The poller MUST alternate between tracks regardless of queue depth.
// This is a fairness invariant: no track may monopolize the poller.
// ============================================================================
TEST(PollerEdgeCases, poller_prevents_starvation_under_asymmetric_load) {
    pool<load_event> p(100);
    conduit<load_event, 10> track_1;
    conduit<load_event, 10> track_2;

    round_robin_poller<load_event, 2, 10> poller;
    poller.bind_track(0, track_1);
    poller.bind_track(1, track_2);

    // Asymmetric load:
    // Track 1 contains 3 events, Track 2 contains only 1.
    track_1.push(p.make(1).release());
    track_1.push(p.make(1).release());
    track_1.push(p.make(1).release());

    track_2.push(p.make(2).release());

    // EXPECTATION:
    // The poller MUST alternate between tracks:
    //   1st poll → Track 1
    //   2nd poll → Track 2 (even though Track 1 still has more)
    //   3rd poll → Track 1 again
    //
    // This proves starvation‑free fairness under uneven load.

    auto ev1 = poller.poll(p);
    EXPECT_EQ(ev1->source_track, 1);

    auto ev2 = poller.poll(p);
    EXPECT_EQ(ev2->source_track, 2);

    auto ev3 = poller.poll(p);
    EXPECT_EQ(ev3->source_track, 1);
}
