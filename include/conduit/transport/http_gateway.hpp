/**
 * @file http_gateway.hpp
 * @author Kristóf Barta
 * © 2026 Kristóf Barta. All rights reserved.
 *  * PROPRIETARY AND OPEN SOURCE DUAL LICENSE:
 * This software is licensed under the GNU Affero General Public License v3 (AGPLv3).
 * For commercial use, proprietary licensing, and support, please contact the author.
 * See LICENSE and CONTRIBUTING.md for details.
 */

#pragma once

#include "../core/networked_conduit.hpp"
#include "http.hpp"
#include <iostream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace cre::transport {

    // ============================================================
    // CONDUIT TRANSPORT: HTTP GATEWAY NODE
    // Accepts TCP connections, runs the Zero‑Copy Parser,
    // and pushes events into the Core conduit.
    // ============================================================

    template <typename ConduitType, typename AllocatorType>
    class http_gateway {
        uint16_t port_;
        int server_fd_{ -1 };
        bool is_running_{ false };

        ConduitType& core_conduit_;
        AllocatorType& memory_pool_;

        // A static‑size read buffer for bytes arriving from the NIC
        static constexpr size_t READ_BUFFER_SIZE = 65536; // 64 KB per packet

    public:
        http_gateway(uint16_t port, ConduitType& conduit, AllocatorType& pool)
            : port_(port), core_conduit_(conduit), memory_pool_(pool) {
        }

        ~http_gateway() { stop(); }

        void start() {
#if defined(_WIN32)
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
            server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd_ < 0) throw std::runtime_error("CONDUIT: Failed to create socket");

            int opt = 1;
            setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(port_);

            if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
                throw std::runtime_error("CONDUIT: Bind failed on port " + std::to_string(port_));
            }

            if (listen(server_fd_, 10000) < 0) { // 10,000 pending connection backlog
                throw std::runtime_error("CONDUIT: Listen failed");
            }

            is_running_ = true;
            std::cout << "[CONDUIT Gateway] Listening for HTTP traffic on port " << port_ << "...\n";

            // Launch the gateway’s dedicated thread
            std::thread(&http_gateway::accept_loop, this).detach();
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
        void accept_loop() {
            // The network thread’s own dedicated buffer. No heap allocation!
            alignas(64) char read_buffer[READ_BUFFER_SIZE];

            while (is_running_) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);

                // Blocking call (a real HFT system would use epoll / io_uring here)
                int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd < 0) continue;

                // Read raw bytes from the network into the stack buffer
                int bytes_read = recv(client_fd, read_buffer, READ_BUFFER_SIZE, 0);

                if (bytes_read > 0) {
                    // 1. Allocate an event for the Core in O(1)
                    http_request_event* ev = memory_pool_.allocate();
                    if (ev) {
                        ev->connection_id = client_fd;

                        // 2. Run the Zero‑Copy Parser
                        if (http_parser::parse(read_buffer, bytes_read, *ev)) {
                            // 3. Push into the SPSC conduit for the Compute Core in O(1)
                            if (!core_conduit_.push(ev)) {
                                // Core overloaded (Backpressure)
                                memory_pool_.deallocate(ev);
                                send_503(client_fd);
                            }
                        }
                        else {
                            // Malformed HTTP request
                            memory_pool_.deallocate(ev);
                            send_400(client_fd);
                        }
                    }
                    else {
                        // Memory pool exhausted
                        send_503(client_fd);
                    }
                }
                else {
#if defined(_WIN32)
                    closesocket(client_fd);
#else
                    close(client_fd);
#endif
                }
            }
        }

        void send_503(int client_fd) {
            const char* resp = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
            send(client_fd, resp, strlen(resp), 0);
#if defined(_WIN32)
            closesocket(client_fd);
#else
            close(client_fd);
#endif
        }

        void send_400(int client_fd) {
            const char* resp = "HTTP/1.1 400 Bad Request\r\n\r\n";
            send(client_fd, resp, strlen(resp), 0);
#if defined(_WIN32)
            closesocket(client_fd);
#else
            close(client_fd);
#endif
        }
    };

} // namespace cre::transport
