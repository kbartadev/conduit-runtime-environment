/**
 * @file test_horizontal_pipeline_flow.cpp
 * @brief Validation of horizontal event processing across independent subsystems.
 */

#include <gtest/gtest.h>
#include "conduit/core.hpp"

namespace cre::test {

    struct execution_order : allocated_event<execution_order, 10> {
        double price;
        uint32_t volume;
        bool is_validated = false;
        bool risk_cleared = false;
    };

    // Subsystem 1: Structural Integrity
    struct order_validator : handler_base<order_validator> {
        void on(const event_ptr<execution_order>& ev) {
            if (ev->volume > 0) ev->is_validated = true;
        }
    };

    // Subsystem 2: Risk Management
    struct risk_gateway : handler_base<risk_gateway> {
        void on(const event_ptr<execution_order>& ev) {
            if (ev->price < 1000000.0) ev->risk_cleared = true;
        }
    };

    // Subsystem 3: Compliance & Audit
    struct audit_logger : handler_base<audit_logger> {
        int logs_recorded = 0;
        void on(const event_ptr<execution_order>& ev) {
            if (ev->is_validated && ev->risk_cleared) {
                logs_recorded++;
            }
        }
    };

    TEST(HorizontalPipelineTest, validates_sequential_processing_flow) {
        pool<execution_order> order_pool(10);

        order_validator validator;
        risk_gateway risk;
        audit_logger auditor;

        // A rendszerek sorrendje meghatározza a feldolgozás menetét
        pipeline<order_validator, risk_gateway, audit_logger> trade_pipe(validator, risk, auditor);

        auto order = order_pool.make();
        order->price = 550.5;
        order->volume = 100;

        trade_pipe.dispatch(order);

        EXPECT_TRUE(order->is_validated);
        EXPECT_TRUE(order->risk_cleared);
        EXPECT_EQ(auditor.logs_recorded, 1);
    }
}