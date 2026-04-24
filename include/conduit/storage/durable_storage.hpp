/**
 * @file durable_storage.hpp
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
#include <thread>

#include "../core.hpp"

#if defined(_WIN32)
#include <io.h>
#define OS_READ _read
#define OS_OPEN _open
#define OS_CLOSE _close
#define OS_O_RDONLY _O_RDONLY
#define OS_O_BINARY _O_BINARY
#else
#include <unistd.h>
#define OS_READ ::read
#define OS_OPEN ::open
#define OS_CLOSE ::close
#define OS_O_RDONLY O_RDONLY
#define OS_O_BINARY 0
#endif

namespace cre::storage {

// ============================================================
// 11. INVARIANT: DURABLE MESSAGE BROKER LAYER (REPLAY)
// Blocking reader running on a dedicated I/O thread that refills the Core.
// ============================================================

template <typename DomainType, typename TargetConduit, typename Event>
class durable_source {
    DomainType& domain_;
    TargetConduit& conduit_;
    int fd_{-1};
    bool is_running_{false};

   public:
    durable_source(DomainType& domain, TargetConduit& conduit, const char* filepath)
        : domain_(domain), conduit_(conduit) {
        fd_ = OS_OPEN(filepath, OS_O_RDONLY | OS_O_BINARY);
        if (fd_ == -1) {
            std::cerr << "[CONDUIT Fatal] Failed to open durable log for replay: " << filepath
                      << "\n";
            // In production we might not terminate here; we may simply notify the Orchestrator.
        }
    }

    ~durable_source() {
        if (fd_ != -1) {
            OS_CLOSE(fd_);
        }
    }

    // The Replay Loop: runs on a dedicated I/O thread
    void run_replay() noexcept {
        if (fd_ == -1) return;
        is_running_ = true;

        const size_t event_size = sizeof(Event);

        while (is_running_) {
            // 1. Gate priming: request preallocated O(1) empty memory from the Core.
            auto ev = domain_.template make_uninitialized<Event>();

            if (!ev) {
                // Backpressure from the Core: the pool is full, the Core cannot keep up.
                // The I/O thread yields so we don’t overload the system.
                std::this_thread::yield();
                continue;
            }

            // 2. Blocking OS read into raw memory (zero deserialization)
            char* raw_buffer = reinterpret_cast<char*>(ev.get());
            int bytes_read = OS_READ(fd_, raw_buffer, event_size);

            if (bytes_read == event_size) {
                // 3. O(1) push into the conduit. From here the Core takes over.
                while (!conduit_.push(std::move(ev)) && is_running_) {
                    // If the conduit is full, wait (physical backpressure from the network side).
                    std::this_thread::yield();
                }
            } else if (bytes_read == 0) {
                // End of file (EOF). Replay completed.
                std::cout << "[CONDUIT Replay] End of durable log reached.\n";
                break;
            } else {
                // Corrupted file or partial read (a more robust implementation
                // would handle partial reads like networked_conduit does).
                std::cerr << "[CONDUIT Error] Corrupted read during replay.\n";
                break;
            }
        }
        is_running_ = false;
    }

    void stop() noexcept { is_running_ = false; }
};

}  // namespace cre::storage
