#include <gtest/gtest.h>
#include <atomic>
#include "axiom_conduit/core.hpp"

using namespace axiom;

struct simple_event : allocated_event<simple_event, 5> {
    std::atomic<int>* counter;
    simple_event(std::atomic<int>* c) : counter(c) {
        if (counter) counter->fetch_add(1);
    }
    ~simple_event() {
        if (counter) counter->fetch_sub(1);
    }
};

TEST(EventPtrTest, event_ptr_automatically_reclaims_memory_on_scope_exit) {
    std::atomic<int> alive{0};
    pool<simple_event> p(5);
    
    {
        event_ptr<simple_event> ptr = p.make(&alive);
        EXPECT_EQ(alive.load(), 1);
    } 

    EXPECT_EQ(alive.load(), 0);
}

TEST(EventPtrTest, event_ptr_explicit_reset_triggers_deterministic_reclamation) {
    std::atomic<int> alive{0};
    pool<simple_event> p(5);
    
    auto ptr = p.make(&alive);
    EXPECT_EQ(alive.load(), 1);
    
    ptr.reset(); // Explicitly drop event
    
    EXPECT_EQ(alive.load(), 0);
    EXPECT_EQ(ptr.get(), nullptr);
}

TEST(EventPtrTest, event_ptr_move_semantics_preserve_unique_ownership) {
    std::atomic<int> alive{0};
    pool<simple_event> p(5);
    
    event_ptr<simple_event> ptr1 = p.make(&alive);
    event_ptr<simple_event> ptr2 = std::move(ptr1);

    EXPECT_EQ(ptr1.get(), nullptr);
    EXPECT_NE(ptr2.get(), nullptr);
    EXPECT_EQ(alive.load(), 1); // No duplicate increments/decrements
}