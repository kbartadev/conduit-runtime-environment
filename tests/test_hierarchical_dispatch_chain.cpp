/**
 * @file test_hierarchical_dispatch_chain.cpp
 * @brief Validation of vertical event propagation across type hierarchies.
 */

#include <gtest/gtest.h>
#include "conduit/core.hpp"

namespace cre::test {

    // --- Domain Definitions ---
    struct network_frame {
        uint32_t sequence_id;
    };

    struct transport_packet : extends<network_frame> {
        uint16_t port;
    };

    struct security_alert : extends<allocated_event<security_alert, 200>, transport_packet> {
        uint8_t threat_level;
    };

    // --- Handler Implementation ---
    struct forensics_engine : handler_base<forensics_engine> {
        int frames_traced = 0;
        int packets_inspected = 0;
        int alerts_processed = 0;

        // Layer 1: Network abstraction
        void on(const event_ptr<network_frame>& ev) {
            frames_traced++;
        }

        // Layer 2: Transport abstraction
        void on(const event_ptr<transport_packet>& ev) {
            packets_inspected++;
        }

        // Layer 3: Specific Security Event
        void on(const event_ptr<security_alert>& ev) {
            alerts_processed++;
        }
    };

    TEST(VerticalDispatchTest, validates_top_down_unwinding) {
        pool<security_alert> alert_pool(16);
        forensics_engine engine;
        pipeline<forensics_engine> pipe(engine);

        auto ev = alert_pool.make();
        ev->threat_level = 5;

        // A dispatch egyetlen hívással végigviszi az eseményt a hierarchián:
        // security_alert -> transport_packet -> network_frame
        pipe.dispatch(ev);

        EXPECT_EQ(engine.alerts_processed, 1);
        EXPECT_EQ(engine.packets_inspected, 1);
        EXPECT_EQ(engine.frames_traced, 1);
    }
}