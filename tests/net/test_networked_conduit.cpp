#include <cassert>
#include <chrono>
#include <iostream>

// Core AXIOM includes
#include "axiom_conduit/core.hpp"
#include "axiom_conduit/net/networked_conduit.hpp"

// Windows-specific TCP setup
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

using namespace axiom;
using namespace axiom::net;

// 1. AXIOM Event Definition
struct tick_data {
    uint64_t sequence;
    double price;
};

struct tick_event : allocated_event<tick_event, 4096> {
    static constexpr uint32_t TYPE_ID = 0xA1B2C3D4;
    tick_data data;
};

// 2. Node B Sink
struct tick_sink : handler_base<tick_sink> {
    uint64_t expected_seq = 0;
    size_t received_count = 0;

    void on(event_ptr<tick_event>& ev) {
        if (ev->data.sequence != expected_seq) {
            std::cerr << "[FATAL] Sequence mismatch! Expected: " << expected_seq
                      << " Got: " << ev->data.sequence << "\n";
            std::abort();
        }
        expected_seq++;
        received_count++;
    }
};

// PHYSICAL TCP LOOPBACK SETUP (Windows / WinSock2)
void setup_loopback_sockets(os_socket_t& fd_A, os_socket_t& fd_B) {
#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    os_socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // OS assigns a free port

    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listener, 1);

    int addrlen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    // Node A (Client) connects
    fd_A = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ::connect(fd_A, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    // Node B (Server) accepts
    fd_B = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);

    // HFT tuning: Disable Nagle (TCP_NODELAY) and enable Non-blocking (FIONBIO)
    int flag = 1;
    u_long mode = 1;
    ::setsockopt(fd_A, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    ::setsockopt(fd_B, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    ::ioctlsocket(fd_A, FIONBIO, &mode);
    ::ioctlsocket(fd_B, FIONBIO, &mode);
#endif
}

int main() {
    std::cout << "[AXIOM] Starting Networked Conduit Integration Test...\n";

    os_socket_t fd_A = INVALID_SOCKET;
    os_socket_t fd_B = INVALID_SOCKET;
    setup_loopback_sockets(fd_A, fd_B);

    if (fd_A == INVALID_SOCKET || fd_B == INVALID_SOCKET) {
        std::cerr << "[FATAL] Failed to create physical TCP loopback.\n";
        return 1;
    }

    // INITIALIZE NODE A (Producer)
    runtime_domain<tick_event> domain_A;
    networked_conduit<tick_event, 1024> conduit_A;
    conduit_A.bind_socket(fd_A);

    // INITIALIZE NODE B (Consumer)
    runtime_domain<tick_event> domain_B;
    tick_sink sink_B;
    networked_conduit<tick_event, 1024> conduit_B;
    conduit_B.bind_socket(fd_B);

    constexpr size_t TOTAL_EVENTS = 1'000'000;
    size_t events_pushed = 0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // THE TIGHT POLL LOOP
    while (sink_B.received_count < TOTAL_EVENTS) {
        // SAFETY BRAKE: If the physical connection drops, exit immediately
        if (!conduit_A.is_alive() || !conduit_B.is_alive()) {
            std::cerr << "[FATAL] A conduit died during routing! Connection lost.\n";
            std::abort();
        }

        // 1. Node A: Produce Event
        if (events_pushed < TOTAL_EVENTS) {
            auto ev = domain_A.make<tick_event>();
            if (ev) {
                ev->data.sequence = events_pushed;
                ev->data.price = 150.50 + (events_pushed * 0.01);

                if (conduit_A.push(ev)) {
                    events_pushed++;
                }
            }
        }

        // 2. O(1) OS Polling
        conduit_A.poll_tx();
        conduit_B.poll_rx(domain_B, sink_B);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed = end_time - start_time;

    std::cout << "[SUCCESS] 1,000,000 events routed deterministically across TCP boundary.\n";
    std::cout << "Elapsed Time: " << elapsed.count() / 1000.0 << " ms ("
              << (TOTAL_EVENTS / (elapsed.count() / 1000000.0)) << " msg/sec)\n";

    return 0;
}
