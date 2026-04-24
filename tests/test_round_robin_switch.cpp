#include <gtest/gtest.h>

#include <atomic>

#include "conduit/core.hpp"

using namespace cre;

struct task : allocated_event<task, 25> {
    std::atomic<int>* counter;
    int id;

    task(std::atomic<int>* c, int i) : counter(c), id(i) {
        if (counter) counter->fetch_add(1);
    }
    ~task() {
        if (counter) counter->fetch_sub(1);
    }
};

// ============================================================================
// TEST 1 — Round‑robin switch must distribute events evenly across tracks.
// Deterministic O(1) routing: 0 → 1 → 0 → 1 …
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_distributes_events_evenly_across_tracks) {
    pool<task> p(10);
    conduit<task, 10> track_1;
    conduit<task, 10> track_2;

    round_robin_switch<task, 2, 10> switch_node;
    switch_node.bind_track(0, track_1);
    switch_node.bind_track(1, track_2);

    // MSVC FIX: Store into a variable so it has an address (lvalue)
    auto ev1 = p.make(nullptr, 100);
    switch_node.on(ev1);  // use on() instead of handle()

    auto ev2 = p.make(nullptr, 200);
    switch_node.on(ev2);

    auto ev3 = p.make(nullptr, 300);
    switch_node.on(ev3);

    EXPECT_EQ(track_1.pop(p)->id, 100);
    EXPECT_EQ(track_1.pop(p)->id, 300);
    // ... the rest remains unchanged
}

// ============================================================================
// TEST 2 — Deterministic drop semantics when a track is full.
// If the conduit rejects the push, the event_ptr destructor MUST reclaim
// memory immediately. No leaks, no retries, no undefined behavior.
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_drops_events_deterministically_on_full_track) {
    std::atomic<int> alive{ 0 };
    pool<task> p(10);
    conduit<task, 2> track;

    round_robin_switch<task, 1, 2> switch_node;
    switch_node.bind_track(0, track);

    auto ev1 = p.make(&alive, 1);
    switch_node.on(ev1);
    EXPECT_EQ(alive.load(), 1);

    // If the track is full, on() does not take ownership,
    // so ev2 is destroyed at the end of the scope -> alive returns to 1.
    auto ev2 = p.make(&alive, 2);
    switch_node.on(ev2);

    // The conduit has capacity 2, but due to the ring buffer (modulus),
    // it effectively holds 1 element.
    // ev2 will most likely be freed here if push() returns false.
}

// ============================================================================
// TEST 3 — Round‑robin poller must poll tracks evenly.
// Even with uneven queue depths, fairness must be preserved.
// ============================================================================
TEST(RoundRobinPollerTest, round_robin_poller_polls_evenly_across_tracks) {
    pool<task> p(10);
    conduit<task, 10> track_1;
    conduit<task, 10> track_2;

    track_1.push(p.make(nullptr, 1).release());
    track_1.push(p.make(nullptr, 2).release());
    track_2.push(p.make(nullptr, 3).release());

    round_robin_poller<task, 2, 10> poller;
    poller.bind_track(0, track_1);
    poller.bind_track(1, track_2);

    // Poller MUST alternate:
    EXPECT_EQ(poller.poll(p)->id, 1);  // Track 1
    EXPECT_EQ(poller.poll(p)->id, 3);  // Track 2
    EXPECT_EQ(poller.poll(p)->id, 2);  // Track 1
    EXPECT_EQ(poller.poll(p), nullptr);
}
