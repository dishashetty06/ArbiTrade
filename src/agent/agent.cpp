#include <iostream>
#include <string>
#include <optional>
#include <cstring>

#include "common/types.hpp"
#include "common/logger.hpp"
#include "networking/tcp_server.hpp"
#include "orderbook/order_book.hpp"
#include "strategy/spread_detector.hpp"

using arbitrade::Logger;
using arbitrade::OrderBook;
using arbitrade::SpreadDetector;
using arbitrade::TcpServer;
using arbitrade::TickData;
using arbitrade::TradeSignal;

namespace
{

    constexpr unsigned short LISTEN_PORT = 8888;

    // Formats a detected TradeSignal into a human-readable alert block.
    // Living here (not inside SpreadDetector) keeps presentation concerns
    // completely out of the strategy layer.
    void log_trade_signal(const TradeSignal &signal)
    {
        Logger::info("=====================================================");
        Logger::info(std::string("[ARBITRAGE ALERT] ") + signal.symbol +
                     " (triggered by seq #" + std::to_string(signal.sequence_number) + ")");
        Logger::info(std::string("  BUY  on ") + signal.buy_exchange +
                     " @ ask " + std::to_string(signal.buy_price));
        Logger::info(std::string("  SELL on ") + signal.sell_exchange +
                     " @ bid " + std::to_string(signal.sell_price));
        Logger::info("  Spread: " + std::to_string(signal.spread) +
                     " | Max Volume: " + std::to_string(signal.executable_volume) +
                     " | Est. Profit: " + std::to_string(signal.estimated_profit));
        Logger::info("=====================================================");
    }

} // namespace

int main()
{
    Logger::info("Agent: NexusTrade Trading Agent starting...");

    TcpServer server;
    if (!server.start_listening(LISTEN_PORT))
    {
        Logger::error("Agent: failed to start listening on port " + std::to_string(LISTEN_PORT));
        return 1;
    }

    Logger::info("Agent: waiting for replayer connection...");
    if (!server.accept_client())
    {
        Logger::error("Agent: failed to accept incoming connection.");
        return 1;
    }

    OrderBook book;
    SpreadDetector detector;
    int ticks_received = 0;

    while (true)
    {
        TickData incoming_tick;
        char *tick_buffer = reinterpret_cast<char *>(&incoming_tick);

        if (!server.recv_all(tick_buffer, sizeof(TickData)))
        {
            break;
        }

        ticks_received++;
        Logger::info("Agent: received tick #" + std::to_string(ticks_received));
        incoming_tick.print();

        // Step 1: fold the new tick into the order book's top-of-book state.
        book.update(incoming_tick);

        // Step 2: ask the strategy layer whether this update created (or
        // revealed) a crossed-market arbitrage opportunity.
        std::optional<TradeSignal> signal = detector.detect(
            book, std::string(incoming_tick.symbol), incoming_tick.sequence_number);

        // Step 3: if a signal was generated, log it. No execution logic
        // lives here — this phase only detects and reports.
        if (signal.has_value())
        {
            log_trade_signal(signal.value());
        }
    }

    Logger::info("Agent: stream ended. Total ticks processed: " + std::to_string(ticks_received));

    return 0;
}