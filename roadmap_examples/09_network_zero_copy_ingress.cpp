#include <iostream>
#include <vector>

#include "conduit/core.hpp"

using namespace cre;

// Trivial POD-style event used for network ingestion.
// Designed for direct byte-level reconstruction without fragmentation or parsing overhead.
struct tick_data : allocated_event<tick_data, 99> {
    uint32_t instrument_id;
    double bid_price;
    double ask_price;
};

// Business logic that consumes decoded market data events.
struct market_data_handler : handler_base<market_data_handler> {
    void on(event_ptr<tick_data>& ev) {
        if (!ev) return;
        std::cout << "[Market Data] Instrument: " << ev->instrument_id
                  << " | Bid: " << ev->bid_price << " | Ask: " << ev->ask_price << "\n";
    }
};

int main() {
    runtime_domain<tick_data> domain;

    market_data_handler handler;
    pipeline<market_data_handler> pipe(handler);
    bound_sink<decltype(pipe), tick_data> sink(pipe);

    cluster<256> core;
    core.bind<tick_data>(sink);

    // Network ingress responsible for allocation and decoding.
    // Converts raw network bytes into fully-formed events in O(1).
    network_ingress<tick_data> net_listener(domain.get_pool<tick_data>(), core);

    // --- NETWORK SIMULATION (e.g., eBPF / io_uring / socket recv) ---
    std::cout << "--- Simulating incoming network byte stream ---\n";

    // Simulate a raw memory block received from the network.
    tick_data dummy_packet{nullptr};  // Only used to obtain a byte representation.
    dummy_packet.instrument_id = 404;
    dummy_packet.bid_price = 1.050;
    dummy_packet.ask_price = 1.051;

    const uint8_t* raw_network_buffer = reinterpret_cast<const uint8_t*>(&dummy_packet);
    std::size_t buffer_size = sizeof(tick_data);

    // O(1) allocation + zero-copy decode + O(1) routing.
    // No dynamic allocation, no string parsing — just a direct bit copy
    // into hot L1 cache followed by deterministic dispatch.
    net_listener.on_network_bytes_received(raw_network_buffer, buffer_size);

    std::cout << "--- Event successfully reconstructed and processed! ---\n";

    return 0;
}
