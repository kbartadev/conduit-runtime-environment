/**
 * @file state_machine.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <array>
#include <cstdint>
#include <iostream>

#include "../core.hpp"

namespace cre::workflow {

// ============================================================
// 14. INVARIANT: WORKFLOW ENGINE (O(1) State Machine)
// Zero allocation, zero map lookup, zero vtable.
// ============================================================

// A single slab storage location for a workflow
template <typename StateEnum, typename Payload>
struct saga_slot {
    StateEnum current_state;
    Payload data;
    bool is_active{false};
};

// The Workflow Engine, which natively integrates into the Unified Dispatch Engine (PATCH 4)
template <typename WorkflowDefinition, size_t MaxConcurrentWorkflows = 65536>
class deterministic_saga : public cre::core::handler_base<
                               deterministic_saga<WorkflowDefinition, MaxConcurrentWorkflows>> {
    using StateEnum = typename WorkflowDefinition::state_type;
    using Payload = typename WorkflowDefinition::payload_type;

    // O(1) preallocated memory to avoid fragmentation.
    // The entire array is contiguous in memory, making it maximally cache‑friendly.
    std::array<saga_slot<StateEnum, Payload>, MaxConcurrentWorkflows> slots_{};

   public:
    // Events are sent here by the PATCH 4 pipeline
    template <typename Event>
    void on(cre::core::event_ptr<Event>& ev) noexcept {
        if (!ev) return;

        // 1. O(1) lookup: the event tells us which workflow it belongs to
        const uint32_t id = ev->workflow_id;

        // Physical protection against overflow
        if (id >= MaxConcurrentWorkflows) {
            std::cerr << "[CONDUIT Warn] Workflow ID out of bounds: " << id << "\n";
            return;
        }

        // 2. O(1) memory access (direct pointer arithmetic)
        auto& slot = slots_[id];

        // 3. O(1) state transition (static dispatch through the Definition)
        // The C++ compiler optimizes this call into a fully flat, inline switch‑case block.
        WorkflowDefinition::transition(slot.current_state, slot.data, slot.is_active, *ev);
    }
};

}  // namespace cre::workflow
