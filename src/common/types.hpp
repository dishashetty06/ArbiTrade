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
        int64_t timestamp; // Unix epoch milliseconds
        char exchange[16]; // Null-terminated exchange name (e.g. "Binance")
        char symbol[16];   // Null-terminated trading pair (e.g. "BTCUSDT")
        double bid_price;
        double bid_amount;
        double ask_price;
        double ask_amount;

        TickData()
            : timestamp(0),
              bid_price(0.0),
              bid_amount(0.0),
              ask_price(0.0),
              ask_amount(0.0)
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
            std::cout << "[" << timestamp << "] "
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

    static_assert(sizeof(TickData) == 8 + 16 + 16 + 8 * 4,
                  "TickData layout drifted from expected packed size — "
                  "replayer/agent wire format would desync.");

}