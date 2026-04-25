/**
 * @file 02_cxx20_concepts.cpp
 * @brief Structural recognition using C++20 Concepts.
 */
#include <iostream>
#include "conduit/core.hpp"

using namespace cre;

// Two completely independent events (no common base!)
struct market_trade : allocated_event<market_trade, 10> {
    double price;
    market_trade(double p) : price(p) {}
};
struct fx_quote : allocated_event<fx_quote, 11> {
    double price;
    fx_quote(double p) : price(p) {}
};

// The C++20 Concept: Anything that has a "price" member variable
template <typename T>
concept HasPrice = requires(T a) { a.price; };

// The handler that "latches onto" the concept
struct pricing_analytics : handler_base<pricing_analytics> {
    template <HasPrice E>
    void on(const event_ptr<E>& ev) {
        std::cout << "Analytics processed price: $" << ev->price << "\n";
    }
};

int main() {
    pool<market_trade> trade_pool(5);
    pool<fx_quote>     quote_pool(5);

    pricing_analytics analytics;
    pipeline<pricing_analytics> pipe(analytics);

    auto trade = trade_pool.make(452.10);
    auto quote = quote_pool.make(1.05);

    // The same pipeline and handler consume the different types seamlessly!
    pipe.dispatch(trade);
    pipe.dispatch(quote);

    return 0;
}
