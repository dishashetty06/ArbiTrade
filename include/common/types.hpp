#pragma once

#include <cstring>
#include <cstdint>
#include <string>
#include <iostream>
#include <iomanip>

namespace arbitrade
{

// Fixed binary layout (1-byte packing) so this struct can be memcpy'd
// directly onto a TCP socket and reconstructed bit-for-bit on the other
// end, with zero serialization/parsing overhead at runtime.
#pragma pack(push, 1)
    struct TickData
    {
        int64_t timestamp; // Unix epoch milliseconds, from the source market data
        char exchange[16]; // Null-terminated exchange name (e.g. "Binance")
        char symbol[16];   // Null-terminated trading pair (e.g. "BTCUSDT")
        double bid_price;
        double bid_amount;
        double ask_price;
        double ask_amount;
        uint64_t sequence_number;          // Monotonic counter assigned by the replayer per tick sent
        uint64_t replay_send_timestamp_ns; // High-resolution timestamp captured immediately before send()

        TickData()
            : timestamp(0),
              bid_price(0.0),
              bid_amount(0.0),
              ask_price(0.0),
              ask_amount(0.0),
              sequence_number(0),
              replay_send_timestamp_ns(0)
        {
            std::memset(exchange, 0, sizeof(exchange));
            std::memset(symbol, 0, sizeof(symbol));
        }

        void set_exchange(const std::string &ex)
        {
            std::memset(exchange, 0, sizeof(exchange));
            std::strncpy(exchange, ex.c_str(), sizeof(exchange) - 1);
        }

        void set_symbol(const std::string &sym)
        {
            std::memset(symbol, 0, sizeof(symbol));
            std::strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        }

        void print() const
        {
            std::cout << "[seq:" << sequence_number << "] [" << timestamp << "] "
                      << std::left << std::setw(8) << exchange << " | "
                      << std::left << std::setw(8) << symbol << " | "
                      << "BID: " << std::fixed << std::setprecision(2)
                      << std::right << std::setw(12) << bid_price
                      << " (" << bid_amount << ") | "
                      << "ASK: " << std::right << std::setw(12) << ask_price
                      << " (" << ask_amount << ")"
                      << std::endl;
        }
    };
#pragma pack(pop)

    // 8 (timestamp) + 16 (exchange) + 16 (symbol) + 32 (4 doubles)
    // + 8 (sequence_number) + 8 (replay_send_timestamp_ns) = 88 bytes.
    static_assert(sizeof(TickData) == 8 + 16 + 16 + (8 * 4) + 8 + 8,
                  "TickData layout drifted from expected packed size — "
                  "replayer/agent wire format would desync.");

// Output of the strategy layer: a fully-formed description of one
// detected cross-exchange arbitrage opportunity. Packed for the same
// reason as TickData — if this ever needs to cross a process boundary
// (e.g. to an execution engine in a later phase) its layout is already
// wire-safe without any further changes.
#pragma pack(push, 1)
    struct TradeSignal
    {
        char buy_exchange[16];  // Venue where we would BUY (hit the ask)
        char sell_exchange[16]; // Venue where we would SELL (hit the bid)
        char symbol[16];
        double buy_price;
        double sell_price;
        double spread;
        double executable_volume;
        double estimated_profit;
        uint64_t sequence_number; // sequence_number of the tick that triggered this detection

        TradeSignal()
            : buy_price(0.0),
              sell_price(0.0),
              spread(0.0),
              executable_volume(0.0),
              estimated_profit(0.0),
              sequence_number(0)
        {
            std::memset(buy_exchange, 0, sizeof(buy_exchange));
            std::memset(sell_exchange, 0, sizeof(sell_exchange));
            std::memset(symbol, 0, sizeof(symbol));
        }
    };
#pragma pack(pop)

} // namespace arbitrade