#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstring>

#include "common/types.hpp"

#pragma comment(lib, "ws2_32.lib")

using arbitrade::TickData;

namespace
{

    constexpr unsigned short LISTEN_PORT = 8888;

    struct SymbolOrderBook
    {
        std::unordered_map<std::string, TickData> exchange_ticks;
    };

    bool recv_all(SOCKET sock, char *buffer, int length)
    {
        int total_received = 0;
        while (total_received < length)
        {
            int received = recv(sock, buffer + total_received, length - total_received, 0);
            if (received == 0)
            {
                std::cout << "[Agent] Connection closed by replayer." << std::endl;
                return false;
            }
            if (received == SOCKET_ERROR)
            {
                std::cerr << "[Agent] recv() failed with error: " << WSAGetLastError() << std::endl;
                return false;
            }
            total_received += received;
        }
        return true;
    }

    // Optimized Arbitrage Checker: O(E) time complexity (where E is number of exchanges).
    void check_arbitrage_for_symbol(const std::string &symbol, const SymbolOrderBook &book)
    {
        if (book.exchange_ticks.size() < 2)
        {
            return; // at least two exchanges to find an arbitrage
        }

        const TickData *best_bid_tick = nullptr;
        const TickData *best_ask_tick = nullptr;

        // Find the highest bid and the lowest ask for this specific symbol
        for (const auto &[exchange_name, tick] : book.exchange_ticks)
        {
            if (!best_bid_tick || tick.bid_price > best_bid_tick->bid_price)
            {
                best_bid_tick = &tick;
            }
            if (!best_ask_tick || tick.ask_price < best_ask_tick->ask_price)
            {
                best_ask_tick = &tick;
            }
        }

        if (best_bid_tick && best_ask_tick &&
            std::strcmp(best_bid_tick->exchange, best_ask_tick->exchange) != 0) // different exchanges,valid pointers
        {

            if (best_bid_tick->bid_price > best_ask_tick->ask_price) // spread is +ve
            {
                double spread = best_bid_tick->bid_price - best_ask_tick->ask_price;
                double max_volume = std::min(best_bid_tick->bid_amount, best_ask_tick->ask_amount);
                double estimated_profit = spread * max_volume;

                std::cout << " " << std::endl;
                std::cout << "[ARBITRAGE ALERT] " << symbol << std::endl;
                std::cout << "  BUY  on " << std::setw(8) << std::left << best_ask_tick->exchange
                          << " @ ask " << std::fixed << std::setprecision(2) << best_ask_tick->ask_price
                          << " (avail: " << best_ask_tick->ask_amount << ")" << std::endl;
                std::cout << "  SELL on " << std::setw(8) << std::left << best_bid_tick->exchange
                          << " @ bid " << best_bid_tick->bid_price
                          << " (avail: " << best_bid_tick->bid_amount << ")" << std::endl;
                std::cout << "  Spread: " << spread
                          << " | Max Volume: " << max_volume
                          << " | Est. Profit: $" << estimated_profit << std::endl;
                std::cout << " " << std::endl;
            }
        }
    }

}
// namespace
int main()
{
    std::cout << "[Agent] ArbiTrade Trading Agent starting..." << std::endl;

    WSADATA wsa_data;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0)
    {
        std::cerr << "[Agent] WSAStartup failed with error: " << wsa_result << std::endl;
        return 1;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET)
    {
        std::cerr << "[Agent] socket() failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    int reuse_opt = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse_opt), sizeof(reuse_opt));

    sockaddr_in service_addr;
    std::memset(&service_addr, 0, sizeof(service_addr));
    service_addr.sin_family = AF_INET;
    service_addr.sin_addr.s_addr = INADDR_ANY;
    service_addr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_socket, reinterpret_cast<sockaddr *>(&service_addr), sizeof(service_addr)) == SOCKET_ERROR)
    {
        std::cerr << "[Agent] bind() failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "[Agent] listen() failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "[Agent] Listening on port " << LISTEN_PORT
              << " for incoming replayer connection..." << std::endl;

    sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    SOCKET client_socket = accept(listen_socket,
                                  reinterpret_cast<sockaddr *>(&client_addr),
                                  &client_addr_len);

    if (client_socket == INVALID_SOCKET)
    {
        std::cerr << "[Agent] accept() failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    std::cout << "[Agent] Replayer connected from " << client_ip
              << ":" << ntohs(client_addr.sin_port) << std::endl;

    std::unordered_map<std::string, SymbolOrderBook> market_state;
    int ticks_received = 0;

    while (true)
    {
        TickData incoming_tick;
        char *tick_buffer = reinterpret_cast<char *>(&incoming_tick);

        if (!recv_all(client_socket, tick_buffer, sizeof(TickData)))
        {
            break;
        }

        ticks_received++;
        std::cout << "[Agent] Received tick #" << ticks_received << " -> ";
        incoming_tick.print();

        market_state[incoming_tick.symbol].exchange_ticks[incoming_tick.exchange] = incoming_tick;

        check_arbitrage_for_symbol(incoming_tick.symbol, market_state[incoming_tick.symbol]); // only check for updated symbol
    }

    std::cout << "[Agent] Stream ended. Total ticks processed: " << ticks_received << std::endl;

    closesocket(client_socket);
    closesocket(listen_socket);
    WSACleanup();

    return 0;
}