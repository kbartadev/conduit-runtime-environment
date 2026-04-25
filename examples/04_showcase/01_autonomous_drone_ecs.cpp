/**
 * @file 01_autonomous_drone_ecs.cpp
 * @brief Universal Event-Driven Architecture (Sensor Fusion & Robotics)
 * * This example demonstrates the power of the CRE library in an Autonomous Drone
 *   navigation system processing 10,000+ events per second.
 * * Features:
 * 1. L0: Hardware interrupts (Inheritance)
 * 2. L1: Safety-critical interface (Abstract Base)
 * 3. L2: Sensor-fusion event (Composition & Pool)
 * 4. Mutation: Wind compensation computed during flight
 */

#include <iostream>
#include <iomanip>
#include <string>
#include "conduit/core.hpp"

using namespace cre;

// ============================================================================
// 1. THE VERTICAL MATRIX: Hardware, Safety, and Sensor Data
// ============================================================================

// L0: Hardware layer (Raw data directly from the sensor)
struct hardware_interrupt {
    uint64_t capture_ns;
    int      sensor_id;
};

// L1: Flight Safety Interface (Pure virtual)
struct safety_critical_system {
    virtual ~safety_critical_system() = default;
    virtual bool is_hardware_healthy() const noexcept = 0;
};

// L2: The Navigation Vector (The Matrix Event)
// Combines hardware timestamp and safety validation in a single block.
struct navigation_vector : extended_event<navigation_vector, 128,
                                         hardware_interrupt,
                                         safety_critical_system> {
    double velocity_x;
    double velocity_y;
    double altitude;
    bool   sensor_ok;

    navigation_vector(int s_id, double vx, double vy, double alt, bool healthy)
        : velocity_x(vx), velocity_y(vy), altitude(alt), sensor_ok(healthy) {
        this->capture_ns = 1682390400000000000ULL;
        this->sensor_id = s_id;
    }

    // Implementing the abstract interface
    bool is_hardware_healthy() const noexcept override {
        return sensor_ok;
    }
};

// ============================================================================
// 2. CONCEPTS: Recognizing Physical Parameters
// ============================================================================

// Any event (camera, lidar, radar) from which velocity can be computed
template <typename T>
concept HasVelocity = requires(T a) { a.velocity_x; a.velocity_y; };

// ============================================================================
// 3. THE PIPELINE HANDLERS: Data Processing System (ECS)
// ============================================================================

// Stage 1: Blackbox Logger – Reads only the hardware layer (CONST)
struct blackbox_logger : handler_base<blackbox_logger> {
    void on(const event_ptr<hardware_interrupt>& ev) {
        std::cout << "[L0 BLACKBOX] Sensor #" << ev->sensor_id
                  << " fired at " << ev->capture_ns << " ns\n";
    }
};

// Stage 2: Flight Safety (Safety Watchdog) – Stops the drone (BOOL)
struct safety_watchdog : handler_base<safety_watchdog> {
    bool on(const event_ptr<safety_critical_system>& ev) {
        if (!ev->is_hardware_healthy()) {
            std::cout << "[L1 SAFETY] CRITICAL FAULT! Sensor malfunction. Dropping vector!\n";
            return false; // SHORT-CIRCUIT: Processing stops!
        }
        std::cout << "[L1 SAFETY] Hardware OK.\n";
        return true;
    }
};

// Stage 3: Collision Avoidance – Duck-Typing (CONCEPT)
struct collision_avoidance : handler_base<collision_avoidance> {
    template <HasVelocity E>
    bool on(const event_ptr<E>& ev) {
        double speed_squared = (ev->velocity_x * ev->velocity_x)
                             + (ev->velocity_y * ev->velocity_y);
        if (speed_squared > 10000.0) { // Too fast
            std::cout << "[L2 RADAR] WARNING: Approach too fast! Engaging airbrakes.\n";
            return false;
        }
        return true;
    }
};

// Stage 4: Wind Compensation (Environmental mutation – NON-CONST)
struct wind_compensator : handler_base<wind_compensator> {
    void on(event_ptr<navigation_vector>& ev) { // May modify the event!
        double wind_resistance_x = -2.5;
        ev->velocity_x += wind_resistance_x; // Runtime mutation
        std::cout << "[L2 PHYSICS] Applied wind drift. New X velocity: "
                  << ev->velocity_x << " m/s\n";
    }
};

// Stage 5: Motor Controller (Physical actuator – LEAF)
struct motor_actuator : handler_base<motor_actuator> {
    void on(const event_ptr<navigation_vector>& ev) {
        std::cout << "[L2 MOTORS] Actuating rotors. Target altitude: "
                  << ev->altitude << "m\n";
    }
};

// ============================================================================
// 4. THE ORCHESTRATION
// ============================================================================

int main() {
    std::cout << "=== CRE Autonomous Flight Controller ===\n\n";

    // 1. Zero-allocation memory pool (Garbage collector cannot freeze the drone)
    pool<navigation_vector> flight_pool(256);

    // 2. Systems (Handlers)
    blackbox_logger     blackbox;
    safety_watchdog     safety;
    collision_avoidance radar;
    wind_compensator    physics;
    motor_actuator      motors;

    // 3. The Pipeline (The drone's "Brain")
    pipeline<blackbox_logger, safety_watchdog, collision_avoidance,
             wind_compensator, motor_actuator>
        flight_computer(blackbox, safety, radar, physics, motors);

    std::cout << "--- SCENARIO 1: Normal Flight Vector ---\n";
    // Parameters: SensorID, Vel_X, Vel_Y, Altitude, Healthy
    flight_computer.dispatch(flight_pool.make(10, 15.0, 5.0, 120.0, true));
    // Passes through: Blackbox -> Safety OK -> Radar OK -> Wind apply -> Motors actuate.

    std::cout << "\n--- SCENARIO 2: Hardware Malfunction ---\n";
    flight_computer.dispatch(flight_pool.make(11, 0.0, 0.0, 50.0, false));
    // Stops at Safety. Motors do not receive faulty commands.

    std::cout << "\n--- SCENARIO 3: Collision Course (Overspeed) ---\n";
    flight_computer.dispatch(flight_pool.make(12, 150.0, 0.0, 80.0, true));
    // Stops at Radar before reaching the motors.

    return 0;
}
