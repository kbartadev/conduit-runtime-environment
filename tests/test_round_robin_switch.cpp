#include <gtest/gtest.h>

#include <atomic>

#include "axiom/axiom.hpp"

using namespace axiom;

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

    switch_node.handle(p.make(nullptr, 100));  // → Track 0
    switch_node.handle(p.make(nullptr, 200));  // → Track 1
    switch_node.handle(p.make(nullptr, 300));  // → Track 0

    EXPECT_EQ(track_1.pop(p)->id, 100);
    EXPECT_EQ(track_1.pop(p)->id, 300);
    EXPECT_EQ(track_1.pop(p), nullptr);

    EXPECT_EQ(track_2.pop(p)->id, 200);
    EXPECT_EQ(track_2.pop(p), nullptr);
}

// ============================================================================
// TEST 2 — Deterministic drop semantics when a track is full.
// If the conduit rejects the push, the event_ptr destructor MUST reclaim
// memory immediately. No leaks, no retries, no undefined behavior.
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_drops_events_deterministically_on_full_track) {
    std::atomic<int> alive{0};
    pool<task> p(10);
    conduit<task, 2> track;  // Effective capacity = 1

    round_robin_switch<task, 1, 2> switch_node;
    switch_node.bind_track(0, track);

    switch_node.handle(p.make(&alive, 1));  // Accepted
    EXPECT_EQ(alive.load(), 1);

    // Adversarial: track is full → event must be dropped instantly.
    switch_node.handle(p.make(&alive, 2));
    EXPECT_EQ(alive.load(), 1);  // Second event died immediately

    auto popped = track.pop(p);
    EXPECT_EQ(popped->id, 1);
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
