#include <array>
#include <iostream>

#include "axiom_conduit/core.hpp"

using namespace axiom;

struct trade_tick : allocated_event<trade_tick, 70> {
    int symbol_id;
    double price;
    trade_tick(int id, double p) : symbol_id(id), price(p) {}
};

// Stateful handler with cache-line isolation.
// alignas(CACHE_LINE_SIZE) ensures the handler's internal state never shares
// a cache line with the memory pool or conduits — no false sharing, no bleed.
struct alignas(CACHE_LINE_SIZE) order_book_handler : handler_base<order_book_handler> {
    // Internal state: last observed prices per symbol.
    std::array<double, 10> last_prices{};
    int total_trades = 0;

    void on(event_ptr<trade_tick>& ev) {
        if (!ev) return;

        // O(1) state update for the affected symbol.
        if (ev->symbol_id >= 0 && ev->symbol_id < 10) {
            last_prices[ev->symbol_id] = ev->price;
            total_trades++;
        }
    }

    void print_snapshot() const {
        std::cout << "--- Order Book Snapshot (" << total_trades << " trades) ---\n";
        for (int i = 0; i < 3; ++i) {
            std::cout << "Symbol " << i << " last price: $" << last_prices[i] << "\n";
        }
    }
};

int main() {
    runtime_domain<trade_tick> domain;
    order_book_handler ob_handler;
    pipeline<order_book_handler> pipe(ob_handler);

    pipe.dispatch(domain.make<trade_tick>(0, 150.5));
    pipe.dispatch(domain.make<trade_tick>(1, 2800.0));
    pipe.dispatch(domain.make<trade_tick>(0, 151.0));  // Updates symbol 0.

    ob_handler.print_snapshot();  // Query the business state.
    return 0;
}
