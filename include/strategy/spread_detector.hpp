#pragma once

#include <optional>
#include <string>
#include <cstdint>

#include "common/types.hpp"
#include "orderbook/order_book.hpp"

namespace arbitrade
{

    // Pure decision logic: given the current state of an OrderBook, decide
    // whether a crossed-market arbitrage opportunity exists for a symbol.
    // This class never touches a socket and never prints to the console —
    // it only inspects state and returns a result, which keeps it trivially
    // unit-testable in isolation from networking and I/O.
    class SpreadDetector
    {
    public:
        SpreadDetector() = default;

        // Inspects every pair of exchanges the OrderBook currently knows
        // about for `symbol` and returns the single best (highest-profit)
        // crossed-market opportunity found, if any. `latest_sequence_number`
        // is threaded through into the resulting TradeSignal purely for
        // downstream traceability (e.g. logging which tick triggered it).
        std::optional<TradeSignal> detect(const OrderBook &book,
                                          const std::string &symbol,
                                          uint64_t latest_sequence_number) const;
    };

} // namespace arbitrade