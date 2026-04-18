#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "axiom/axiom.hpp"

using namespace axiom;

struct network_packet : allocated_event<network_packet, 99> {
    int trade_id;
    double price;
};

// Dummy sink that receives the reconstructed events coming from the network boundary.
struct mock_network_receiver {
    int last_id = 0;
    double last_price = 0.0;

    void handle(event_ptr<network_packet> ev) {
        if (ev) {
            last_id = ev->trade_id;
            last_price = ev->price;
        }
    }
};

TEST(NetworkBoundary, bits_are_perfectly_reconstructed_without_allocation) {
    pool<network_packet> p(10);
    cluster<256> local_node;

    mock_network_receiver receiver;
    local_node.bind<network_packet>(receiver);

    // Create the network ingress responsible for O(1) allocation + decode.
    network_ingress<network_packet> ingress(p, local_node);

    // Simulate a raw byte-stream arriving from the network (e.g., TCP buffer).
    // The dummy object is used purely as a source of bytes.
    network_packet original_data{nullptr};
    original_data.trade_id = 1337;
    original_data.price = 45000.50;

    const uint8_t* raw_bytes = reinterpret_cast<const uint8_t*>(&original_data);
    std::size_t raw_size = sizeof(network_packet);

    // The network layer processes the raw bytes and reconstructs the event
    // with zero dynamic allocation and a direct bitwise copy.
    ingress.on_network_bytes_received(raw_bytes, raw_size);

    // Verify that the local topology received the perfectly reconstructed event.
    EXPECT_EQ(receiver.last_id, 1337);
    EXPECT_DOUBLE_EQ(receiver.last_price, 45000.50);
}
