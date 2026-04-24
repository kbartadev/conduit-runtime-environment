/**
 * @file telemetry_gateway.hpp
 * @author Kristóf Barta
 * @copyright Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace cre::supplemental {

    // ============================================================
    // TELEMETRY GATEWAY
    // Dedicated HTTP server running on a background thread exposing 
    // core telemetry via JSON, with zero impact on the hot path.
    // ============================================================

    template <typename SlabAllocator, typename Conduit, typename TimingWheel>
    class telemetry_gateway {
        uint16_t port_;
        int server_fd_{-1};
        std::atomic<bool> is_running_{false};

        // References to core components
        const SlabAllocator& memory_pool_;
        const Conduit& hot_conduit_;
        const TimingWheel& timing_wheel_;

        // Simple initialization timestamp for uptime calculation
        std::chrono::time_point<std::chrono::steady_clock> start_time_;

    public:
        telemetry_gateway(uint16_t port, const SlabAllocator& pool, const Conduit& conduit, const TimingWheel& wheel)
            : port_(port), memory_pool_(pool), hot_conduit_(conduit), timing_wheel_(wheel) {}

        ~telemetry_gateway() { stop(); }

        void start() {
            start_time_ = std::chrono::steady_clock::now();

#if defined(_WIN32)
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
            server_fd_ = socket(AF_INET, SOCK_STREAM, 0);

            if (server_fd_ < 0) throw std::runtime_error("CONDUIT: Telemetry socket creation failed");

            int opt = 1;
            setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port_);

            if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
                throw std::runtime_error("CONDUIT: Telemetry bind failed on port " + std::to_string(port_));
            }

            if (listen(server_fd_, 10) < 0) { // Small backlog, low traffic expected
                throw std::runtime_error("CONDUIT: Telemetry listen failed");
            }

            is_running_ = true;
            std::cout << "[CONDUIT Control Plane] Telemetry Dashboard listening on http://localhost:" << port_ << "/metrics\n";
            
            // Launch the background worker thread
            std::thread(&telemetry_gateway::worker_loop, this).detach();
        }

        void stop() {
            is_running_ = false;
#if defined(_WIN32)
            if (server_fd_ >= 0) closesocket(server_fd_);
            WSACleanup();
#else
            if (server_fd_ >= 0) close(server_fd_);
#endif
        }

    private:
        void worker_loop() {
            while (is_running_) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                
                // Blocks this dedicated thread (Core execution remains undisturbed)
                int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) continue;

                // Simplified HTTP read: wait for incoming request
                char buffer[1024];
                recv(client_fd, buffer, sizeof(buffer), 0); 

                // 1. Zero-cost sampling from the Core (Hardware L3 Cache sync)
                auto uptime_sec = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_).count();

                size_t free_blocks = memory_pool_.get_free_count();
                size_t total_blocks = memory_pool_.get_total_count();
                size_t conduit_size = hot_conduit_.approx_size();
                uint64_t current_tick = timing_wheel_.get_current_tick();

                // 2. Build JSON response
                std::string json = "{\n";
                json += "  \"system\": \"CONDUIT Event Engine\",\n";
                json += "  \"uptime_seconds\": " + std::to_string(uptime_sec) + ",\n";
                json += "  \"memory_pool\": {\n";
                json += "    \"total_blocks\": " + std::to_string(total_blocks) + ",\n";
                json += "    \"free_blocks\": " + std::to_string(free_blocks) + ",\n";
                json += "    \"utilization_percent\": " + std::to_string(100.0 - ((double)free_blocks / total_blocks * 100.0)) + "\n";
                json += "  },\n";
                json += "  \"hot_conduit\": {\n";
                json += "    \"approximate_queue_depth\": " + std::to_string(conduit_size) + "\n";
                json += "  },\n";
                json += "  \"timing_wheel\": {\n";
                json += "    \"current_tick\": " + std::to_string(current_tick) + "\n";
                json += "  }\n";
                json += "}\n";

                // 3. Assemble and dispatch HTTP Response
                std::string http_response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Connection: close\r\n"
                    "Content-Length: " + std::to_string(json.length()) + "\r\n"
                    "\r\n" + json;

                send(client_fd, http_response.c_str(), http_response.length(), 0);

#if defined(_WIN32)
                closesocket(client_fd);
#else
                close(client_fd);
#endif
            }
        }
    };
} // namespace cre::supplemental