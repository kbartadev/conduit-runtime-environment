#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "axiom/axiom.hpp"

using namespace axiom;

struct packet : allocated_event<packet, 10> {
    int seq;
    packet(int s) : seq(s) {}
};

TEST(ConduitTest, conduit_preserves_strict_fifo_ordering) {
    pool<packet> p(10);
    conduit<packet, 5> c;

    EXPECT_TRUE(c.push(p.make(1).release()));
    EXPECT_TRUE(c.push(p.make(2).release()));
    EXPECT_TRUE(c.push(p.make(3).release()));

    auto p1 = c.pop(p);
    auto p2 = c.pop(p);
    auto p3 = c.pop(p);

    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);

    // Strict FIFO ordering must be preserved across all operations.
    EXPECT_EQ(p1->seq, 1);
    EXPECT_EQ(p2->seq, 2);
    EXPECT_EQ(p3->seq, 3);
}

TEST(ConduitTest, conduit_drops_push_when_full_and_prevents_overwrite) {
    pool<packet> p(10);
    conduit<packet, 3> c;  // Capacity 3 ring buffer; effective usable capacity is 2.

    EXPECT_TRUE(c.push(p.make(1).release()));
    EXPECT_TRUE(c.push(p.make(2).release()));

    // Adversarial push into a full conduit.
    // Must be rejected to guarantee no overwrite and preserve FIFO integrity.
    auto overflow = p.make(3);
    EXPECT_FALSE(c.push(overflow.get()));

    // The earliest element must still be intact.
    auto p1 = c.pop(p);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1->seq, 1);
}

TEST(ConduitTest, conduit_returns_null_when_empty) {
    pool<packet> p(10);
    conduit<packet, 5> c;

    // Empty conduit must return nullptr on pop. No ghost events.
    auto ev = c.pop(p);
    EXPECT_EQ(ev.get(), nullptr);
}

TEST(ConduitTest, conduit_spsc_concurrent_flux_preserves_integrity) {
    pool<packet> p(100000);
    conduit<packet, 1024> c;
    const int target_messages = 500000;

    std::atomic<int> received_count{0};
    std::atomic<bool> producer_done{false};

    std::thread producer([&]() {
        for (int i = 0; i < target_messages; ++i) {
            auto ev = p.make(i);
            // Spin-wait backpressure: producer waits until space is available.
            while (!c.push(ev.get())) {
                std::this_thread::yield();
            }
            ev.release();  // Ownership transferred to the conduit.
        }
        producer_done = true;
    });

    std::thread consumer([&]() {
        int expected_seq = 0;
        // Continue until producer is done AND all messages have been consumed.
        while (!producer_done.load() || received_count.load() < target_messages) {
            if (auto ev = c.pop(p)) {
                // Sequence must remain strictly monotonic.
                EXPECT_EQ(ev->seq, expected_seq);
                expected_seq++;
                received_count++;
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received_count.load(), target_messages);
}
