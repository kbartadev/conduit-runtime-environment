/**
 * @file durable_sink.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <fcntl.h>

#include <iostream>
#include <system_error>

#include "../core.hpp"

#if defined(_WIN32)
#include <io.h>
#define OS_WRITE _write
#define OS_OPEN _open
#define OS_CLOSE _close
#define OS_O_CREAT _O_CREAT
#define OS_O_WRONLY _O_WRONLY
#define OS_O_APPEND _O_APPEND
#define OS_O_BINARY _O_BINARY
#else
#include <unistd.h>
#define OS_WRITE ::write
#define OS_OPEN ::open
#define OS_CLOSE ::close
#define OS_O_CREAT O_CREAT
#define OS_O_WRONLY O_WRONLY
#define OS_O_APPEND O_APPEND
#define OS_O_BINARY 0  // POSIX has no separate binary mode
#endif

namespace cre::storage {

// ============================================================
// 11. INVARIANT: DURABLE MESSAGE BROKER LAYER
// Append-only binary log. Must run exclusively on a dedicated I/O thread!
// ============================================================

template <typename Event>
class durable_sink {
    int fd_{-1};
    uint64_t bytes_written_{0};

   public:
    // Open the file in append-only, binary mode.
    explicit durable_sink(const char* filepath) {
        fd_ = OS_OPEN(filepath, OS_O_CREAT | OS_O_WRONLY | OS_O_APPEND | OS_O_BINARY, 0666);
        if (fd_ == -1) {
            std::cerr << "[CONDUIT Fatal] Failed to open durable log: " << filepath << "\n";
            std::terminate();  // CONDUIT physics: an I/O failure at startup is fatal.
        }
    }

    ~durable_sink() {
        if (fd_ != -1) {
            OS_CLOSE(fd_);
        }
    }

    // This function receives events from the Conduit.
    // IMPORTANT: This is invoked by the NodeRuntime on the I/O thread!
    void on(cre::event_ptr<Event>& ev) noexcept {
        if (!ev || fd_ == -1) return;

        // 1. Zero serialization: Since Event is POD (Plain Old Data),
        // we write raw bytes directly from memory.
        const auto* raw_data = reinterpret_cast<const char*>(ev.get());
        const size_t size = sizeof(Event);

        // 2. Blocking OS call.
        // The Compute Node does not feel this, because it already released the event
        // into the conduit in O(1).
        int written = OS_WRITE(fd_, raw_data, size);

        if (written == size) {
            bytes_written_ += size;
        } else {
            // Handle partial or failed writes (e.g., disk full)
            // In production, this is where we notify the Orchestrator Node.
        }

        // 3. O(1) Memory Return:
        // As soon as 'ev' goes out of scope, memory returns lock-free
        // to the Core pool (Layer 2), ready for immediate reuse.
    }

    [[nodiscard]] uint64_t total_bytes_written() const noexcept { return bytes_written_; }
};

}  // namespace cre::storage
