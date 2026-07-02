#include <iostream>
#include <string>
#include <optional>
#include <cstring>
#include <stdexcept>

#include "common/types.hpp"
#include "common/logger.hpp"
#include "networking/tcp_server.hpp"
#include "networking/tcp_client.hpp"
#include "orderbook/order_book.hpp"
#include "strategy/spread_detector.hpp"

using arbitrade::LockRequest;
using arbitrade::LockResponse;
using arbitrade::Logger;
using arbitrade::OrderBook;
using arbitrade::SpreadDetector;
using arbitrade::TcpClient;
using arbitrade::TcpServer;
using arbitrade::TickData;
using arbitrade::TradeSignal;

namespace
{

    constexpr const char *LOCK_SERVER_IP = "127.0.0.1";
    constexpr unsigned short LOCK_SERVER_PORT = 7777;
    constexpr uint64_t LOCK_DURATION_MS = 50;

    // Formats a detected TradeSignal into a human-readable alert block,
    // tagged with this agent's identity so multi-agent logs stay readable.
    void log_trade_signal(const std::string &agent_name, const TradeSignal &signal)
    {
        Logger::info(" ");
        Logger::info("[" + agent_name + "] [SIGNAL] " + std::string(signal.symbol) +
                     " (triggered by seq #" + std::to_string(signal.sequence_number) + ")");
        Logger::info("  BUY  on " + std::string(signal.buy_exchange) +
                     " @ ask " + std::to_string(signal.buy_price));
        Logger::info("  SELL on " + std::string(signal.sell_exchange) +
                     " @ bid " + std::to_string(signal.sell_price));
        Logger::info("  Spread: " + std::to_string(signal.spread) +
                     " | Max Volume: " + std::to_string(signal.executable_volume) +
                     " | Est. Profit: " + std::to_string(signal.estimated_profit));
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        std::cerr << "Usage: agent.exe <DATA_PORT> <AGENT_NAME>" << std::endl;
        return 1;
    }

    unsigned short data_port;
    try
    {
        int parsed_port = std::stoi(argv[1]);
        if (parsed_port <= 0 || parsed_port > 65535)
        {
            throw std::out_of_range("DATA_PORT must be between 1 and 65535");
        }
        data_port = static_cast<unsigned short>(parsed_port);
    }
    catch (const std::exception &e)
    {
        Logger::error(std::string("Agent: invalid DATA_PORT argument '") + argv[1] + "': " + e.what());
        return 1;
    }

    std::string agent_name = argv[2];

    Logger::info("Agent: '" + agent_name + "' starting...");

    // --- Establish the persistent connection to the lock server FIRST.
    // If the lock server isn't reachable, there's no point listening for
    // market data we'd never be able to act on. ---
    TcpClient lock_client;
    if (!lock_client.connect_to(LOCK_SERVER_IP, LOCK_SERVER_PORT))
    {
        Logger::error("Agent: '" + agent_name + "' could not connect to lock server at " +
                      LOCK_SERVER_IP + ":" + std::to_string(LOCK_SERVER_PORT) +
                      ". Is lock_server.exe running?");
        return 1;
    }
    Logger::info("Agent: '" + agent_name + "' established persistent connection to lock server.");

    // --- Open the data-stream listener for the replayer to connect to. ---
    TcpServer data_server;
    if (!data_server.start_listening(data_port))
    {
        Logger::error("Agent: '" + agent_name + "' failed to start listening on port " +
                      std::to_string(data_port));
        return 1;
    }

    Logger::info("Agent: '" + agent_name + "' waiting for replayer connection on port " +
                 std::to_string(data_port) + "...");

    if (!data_server.accept_client())
    {
        Logger::error("Agent: '" + agent_name + "' failed to accept incoming connection.");
        return 1;
    }

    OrderBook book;
    SpreadDetector detector;
    int ticks_received = 0;

    while (true)
    {
        TickData incoming_tick;
        char *tick_buffer = reinterpret_cast<char *>(&incoming_tick);

        if (!data_server.recv_all(tick_buffer, sizeof(TickData)))
        {
            break;
        }

        ticks_received++;
        Logger::info("[" + agent_name + "] received tick #" + std::to_string(ticks_received));
        incoming_tick.print();

        // Step 1: fold the new tick into the order book's top-of-book state.
        book.update(incoming_tick);

        // Step 2: ask the strategy layer whether this update created (or
        // revealed) a crossed-market arbitrage opportunity.
        std::optional<TradeSignal> signal = detector.detect(
            book, std::string(incoming_tick.symbol), incoming_tick.sequence_number);

        if (!signal.has_value())
        {
            continue;
        }

        const TradeSignal &sig = signal.value();
        log_trade_signal(agent_name, sig);

        // Step 3: race for the lock on this symbol before acting on the
        // signal — another agent may have detected the same crossed
        // market and be racing us for it right now.
        LockRequest lock_request;
        std::memset(lock_request.symbol, 0, sizeof(lock_request.symbol));
        std::strncpy(lock_request.symbol, sig.symbol, sizeof(lock_request.symbol) - 1);
        lock_request.duration_ms = LOCK_DURATION_MS;

        if (!lock_client.send_all(reinterpret_cast<const char *>(&lock_request), sizeof(LockRequest)))
        {
            Logger::error("Agent: '" + agent_name + "' failed to send LockRequest to lock server.");
            continue;
        }

        LockResponse lock_response;
        if (!lock_client.recv_all(reinterpret_cast<char *>(&lock_response), sizeof(LockResponse)))
        {
            Logger::error("Agent: '" + agent_name + "' failed to receive LockResponse from lock server.");
            continue;
        }

        if (lock_response.success)
        {
            Logger::info("[TRADE EXECUTED] " + agent_name + " secured the lock!");
        }
        else
        {
            Logger::info("[MISSED] " + agent_name + " was too slow.");
        }
    }

    Logger::info("Agent: '" + agent_name + "' stream ended. Total ticks processed: " +
                 std::to_string(ticks_received));

    return 0;
}