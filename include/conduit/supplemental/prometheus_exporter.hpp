/**
 * @file prometheus_exporter.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../supplemental/telemetry.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define closesocket close
#endif

namespace cre::supplemental {

// ============================================================
// SUPPLEMENTAL LAYER: PROMETHEUS HTTP EXPORTER
// Dedicated I/O Node. Only reads, never writes into Core memory.
// ============================================================

class prometheus_exporter {
    uint16_t port_;
    int server_fd_{-1};
    bool is_running_{false};

    // Registered metrics: Name -> Reader function
    std::vector<std::pair<std::string, std::function<uint64_t()>>> counters_;

   public:
    explicit prometheus_exporter(uint16_t port = 8080) : port_(port) {}

    ~prometheus_exporter() { stop(); }

    // Attach an O(1) wrapper to Prometheus
    template <typename Node>
    void register_counter(const std::string& metric_name, const telemetry_wrapper<Node>& target) {
        counters_.emplace_back(metric_name, [&target]() { return target.get_count(); });
    }

    // This function runs on the dedicated I/O thread (spawn_io_node)
    void run() noexcept {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) return;

        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) return;
        if (listen(server_fd_, 3) < 0) return;

        is_running_ = true;
        std::cout << "[CONDUIT Telemetry] Prometheus endpoint listening on port " << port_
                  << "/metrics\n";

        while (is_running_) {
            // 1. Blocking wait for the Prometheus server
            int client_socket = accept(server_fd_, nullptr, nullptr);
            if (client_socket < 0) continue;

            // A real implementation would read the HTTP GET request here,
            // but we immediately send the response, assuming /metrics was requested.

            // 2. Format the response payload
            std::ostringstream payload;
            for (const auto& [name, read_func] : counters_) {
                payload << "# HELP " << name << " CONDUIT processed events.\n";
                payload << "# TYPE " << name << " counter\n";
                // The magic happens here: the reader function reaches across memory with zero locks
                payload << name << " " << read_func() << "\n\n";
            }

            std::string body = payload.str();

            // 3. Add HTTP header
            std::ostringstream http_response;
            http_response << "HTTP/1.1 200 OK\r\n"
                          << "Content-Type: text/plain; version=0.0.4\r\n"
                          << "Content-Length: " << body.size() << "\r\n"
                          << "Connection: close\r\n\r\n"
                          << body;

            std::string final_response = http_response.str();

            // 4. Send to the network and close the socket
            send(client_socket, final_response.c_str(), final_response.size(), 0);
            closesocket(client_socket);
        }
    }

    void stop() noexcept {
        is_running_ = false;
        if (server_fd_ != -1) {
            closesocket(server_fd_);
            server_fd_ = -1;
        }
    }
};

}  // namespace cre::supplemental
