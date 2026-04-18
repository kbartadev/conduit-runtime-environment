#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

// 1. Pull in the AXIOM core (Domain, memory management)
#include "axiom_conduit/core.hpp"

// 2. Pull in the specific modules required in THIS file
#include "axiom_conduit/net/networked_conduit.hpp"
#include "axiom_conduit/runtime/node_runtime.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#endif

using namespace axiom;
using namespace axiom::net;
using namespace axiom::runtime;

// ========================================================================
// 1. AXIOM EVENT (Trivially Copyable)
// ========================================================================
struct tick_data {
    uint64_t sequence;
    double price;
};

struct tick_event : allocated_event<tick_event, 4096> {
    static constexpr uint32_t TYPE_ID = 0xA1B2C3D4;
    tick_data data;
};

// Global flag for thread synchronization
alignas(64) std::atomic<bool> test_completed{false};

// ========================================================================
// 2. NODE B SINK (Event endpoint)
// ========================================================================
struct tick_sink : handler_base<tick_sink> {
    uint64_t expected_seq = 0;
    size_t received_count = 0;

    // No virtual dispatch; the compiler fully inlines this into node_runtime
    void on(event_ptr<tick_event>& ev) {
        if (ev->data.sequence != expected_seq) {
            std::cerr << "[FATAL] Sequence mismatch! Expected: " << expected_seq
                      << " Got: " << ev->data.sequence << "\n";
            std::abort();
        }

        expected_seq++;
        received_count++;

        // Signal the main thread when we reach 1 million
        if (received_count == 1'000'000) {
            test_completed.store(true, std::memory_order_release);
        }
    }
};

// ========================================================================
// PHYSICAL TCP LOOPBACK SETUP (WinSock2)
// ========================================================================
void setup_loopback_sockets(os_socket_t& fd_A, os_socket_t& fd_B) {
#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    os_socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listener, 1);

    int addrlen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    fd_A = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ::connect(fd_A, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    fd_B = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);

    // HFT tuning: Nagle OFF, Non-blocking ON
    int flag = 1;
    u_long mode = 1;
    ::setsockopt(fd_A, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    ::setsockopt(fd_B, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    ::ioctlsocket(fd_A, FIONBIO, &mode);
    ::ioctlsocket(fd_B, FIONBIO, &mode);
#endif
}

// Orchestrator callback: what should the runtime do when the wire dies?
void on_conduit_dead(networked_conduit<tick_event, 1024>& c) {
    std::cerr << "[ORCHESTRATOR] Conduit explicitly died during runtime loop!\n";
}

int main() {
    std::cout << "[AXIOM] Starting Multi-Core Node Runtime Integration Test...\n";

    os_socket_t fd_A = INVALID_SOCKET;
    os_socket_t fd_B = INVALID_SOCKET;
    setup_loopback_sockets(fd_A, fd_B);

    if (fd_A == INVALID_SOCKET || fd_B == INVALID_SOCKET) {
        std::cerr << "[FATAL] Failed to create physical TCP loopback.\n";
        return 1;
    }

    // ========================================================================
    // THREAD 1: NODE B (Consumer) — Runs on a dedicated core
    // ========================================================================
    runtime_domain<tick_event> domain_B;
    tick_sink sink_B;
    networked_conduit<tick_event, 1024> conduit_B;
    conduit_B.bind_socket(fd_B);

    // Create binding for the runtime
    auto binding_B = bind_conduit(conduit_B, sink_B);
    binding_B.on_dead = on_conduit_dead;

    // Initialize Node Runtime
    node_runtime<decltype(domain_B), decltype(binding_B)> engine_B(domain_B, binding_B);

    // Launch Node B on a separate thread (Thread Affinity: request Core 1)
    std::thread thread_B([&engine_B]() {
        // Ideally this will be physical core 1 (or 2 if 0 is occupied)
        engine_B.run(1);
    });

    // ========================================================================
    // THREAD 0: NODE A (Producer) — Runs on the main thread
    // ========================================================================
    runtime_domain<tick_event> domain_A;
    networked_conduit<tick_event, 1024> conduit_A;
    conduit_A.bind_socket(fd_A);

    // Request OS to pin the main thread to core 0
    pin_thread_to_core(0);

    constexpr size_t TOTAL_EVENTS = 1'000'000;
    size_t events_pushed = 0;

    // Wait 100ms for the OS scheduler to start Node B on the other core
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "[AXIOM] Nodes isolated. Firing 1,000,000 events across core boundary...\n";
    auto start_time = std::chrono::high_resolution_clock::now();

    // Node A hot loop
    while (events_pushed < TOTAL_EVENTS) {
        auto ev = domain_A.make<tick_event>();
        if (ev) {
            ev->data.sequence = events_pushed;
            ev->data.price = 150.50 + (events_pushed * 0.01);

            if (conduit_A.push(ev)) {
                events_pushed++;
            }
        }

        // Continuously push to the wire until the OS buffer fills
        conduit_A.poll_tx();
    }

    // Node A finished sending, but must wait until Node B processes everything
    while (!test_completed.load(std::memory_order_acquire)) {
        conduit_A.poll_tx();  // Continue pumping any remaining data
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed = end_time - start_time;

    // Graceful shutdown
    engine_B.stop();
    if (thread_B.joinable()) {
        thread_B.join();
    }

    std::cout << "[SUCCESS] 1,000,000 events processed by dedicated Node Runtime.\n";
    std::cout << "Data Integrity: 100% Strict FIFO Ordering verified.\n";
    std::cout << "Elapsed Time: " << elapsed.count() / 1000.0 << " ms ("
              << (TOTAL_EVENTS / (elapsed.count() / 1000000.0)) << " msg/sec)\n";

    return 0;
}
