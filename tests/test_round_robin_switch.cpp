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
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_distributes_events_evenly_across_tracks) {
    pool<task> p(10);
    conduit<task, 10> track_1;
    conduit<task, 10> track_2;

    round_robin_switch<task, 2, 10> switch_node;
    switch_node.bind_track(0, track_1);
    switch_node.bind_track(1, track_2);

    // FIX: Store into a variable so it is not a temporary (lvalue)
    auto ev1 = p.make(nullptr, 100);
    switch_node.on(ev1);  // use .on instead of .handle

    auto ev2 = p.make(nullptr, 200);
    switch_node.on(ev2);

    auto ev3 = p.make(nullptr, 300);
    switch_node.on(ev3);

    EXPECT_EQ(track_1.pop(p)->id, 100);
    EXPECT_EQ(track_1.pop(p)->id, 300);
    EXPECT_EQ(track_1.pop(p), nullptr);

    EXPECT_EQ(track_2.pop(p)->id, 200);
    EXPECT_EQ(track_2.pop(p), nullptr);
}

// ============================================================================
// TEST 2 — Deterministic drop semantics when a track is full.
// ============================================================================
TEST(RoundRobinSwitchTest, round_robin_switch_drops_events_deterministically_on_full_track) {
    std::atomic<int> alive{ 0 };
    pool<task> p(10);
    conduit<task, 2> track; // Capacity 2, but due to the ring buffer only 1 fits

    round_robin_switch<task, 1, 2> switch_node;
    switch_node.bind_track(0, track);

    auto ev1 = p.make(&alive, 1);
    switch_node.on(ev1);
    EXPECT_EQ(alive.load(), 1);

    // Second event: the track is full, on() does not take ownership,
    // ev2 is destroyed at the end of the scope, alive returns to 1.
    {
        auto ev2 = p.make(&alive, 2);
        switch_node.on(ev2);
    }
    EXPECT_EQ(alive.load(), 1);

    auto popped = track.pop(p);
    EXPECT_EQ(popped->id, 1);
}

// ============================================================================
// TEST 3 — Round‑robin poller must poll tracks evenly.
// ============================================================================
TEST(RoundRobinPollerTest, round_robin_poller_polls_evenly_across_tracks) {
    pool<task> p(10);
    conduit<task, 10> track_1;
    conduit<task, 10> track_2;

    // .release() stays here because the conduit stores raw pointers
    track_1.push(p.make(nullptr, 1).release());
    track_1.push(p.make(nullptr, 2).release());
    track_2.push(p.make(nullptr, 3).release());

    round_robin_poller<task, 2, 10> poller;
    poller.bind_track(0, track_1);
    poller.bind_track(1, track_2);

    EXPECT_EQ(poller.poll(p)->id, 1);
    EXPECT_EQ(poller.poll(p)->id, 3);
    EXPECT_EQ(poller.poll(p)->id, 2);
    EXPECT_EQ(poller.poll(p), nullptr);
}
