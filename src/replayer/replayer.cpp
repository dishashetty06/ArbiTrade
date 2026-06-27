#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

#include "common/types.hpp"

#pragma comment(lib, "ws2_32.lib")

using arbitrade::TickData;

namespace
{
    constexpr const char *SERVER_IP = "127.0.0.1";
    constexpr unsigned short SERVER_PORT = 8888;
    constexpr const char *CSV_PATH = "data/market_data.csv";

    std::vector<std::string> split_csv_line(const std::string &line)
    {
        std::vector<std::string> fields;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, ','))
        {
            fields.push_back(field);
        }
        return fields;
    }

    bool parse_tick(const std::string &line, TickData &out_tick)
    {
        std::vector<std::string> fields = split_csv_line(line);
        if (fields.size() != 7)
        {
            std::cerr << "[Replayer] Skipping malformed line (expected 7 fields, got "
                      << fields.size() << "): " << line << std::endl;
            return false;
        }

        try
        {
            out_tick.timestamp = std::stoll(fields[0]);
            out_tick.set_exchange(fields[1]);
            out_tick.set_symbol(fields[2]);
            out_tick.bid_price = std::stod(fields[3]);
            out_tick.bid_amount = std::stod(fields[4]);
            out_tick.ask_price = std::stod(fields[5]);
            out_tick.ask_amount = std::stod(fields[6]);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Replayer] Failed to parse line: " << line
                      << " (" << e.what() << ")" << std::endl;
            return false;
        }

        return true;
    }

    bool send_all(SOCKET sock, const char *buffer, int length)
    {
        int total_sent = 0;
        while (total_sent < length)
        {
            int sent = send(sock, buffer + total_sent, length - total_sent, 0);
            if (sent == SOCKET_ERROR)
            {
                std::cerr << "[Replayer] send() failed with error: "
                          << WSAGetLastError() << std::endl;
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

} // namespace

int main()
{
    std::cout << "[Replayer] NexusTrade Market Data Replayer starting..." << std::endl;

    WSADATA wsa_data;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0)
    {
        std::cerr << "[Replayer] WSAStartup failed with error: " << wsa_result << std::endl;
        return 1;
    }

    SOCKET connect_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connect_socket == INVALID_SOCKET)
    {
        std::cerr << "[Replayer] socket() failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0)
    {
        std::cerr << "[Replayer] inet_pton failed for address: " << SERVER_IP << std::endl;
        closesocket(connect_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "[Replayer] Connecting to agent at " << SERVER_IP
              << ":" << SERVER_PORT << " ..." << std::endl;

    // Retry loop: gives the agent process a moment to start listening
    constexpr int max_connect_attempts = 10;
    int connect_attempts = 0;
    int connect_result = SOCKET_ERROR;

    while (connect_attempts < max_connect_attempts)
    {
        connect_result = connect(connect_socket,
                                 reinterpret_cast<sockaddr *>(&server_addr),
                                 sizeof(server_addr));
        if (connect_result != SOCKET_ERROR)
        {
            break;
        }
        connect_attempts++;
        std::cerr << "[Replayer] connect() attempt " << connect_attempts
                  << " failed with error: " << WSAGetLastError()
                  << ". Retrying in 500ms..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    if (connect_result == SOCKET_ERROR)
    {
        std::cerr << "[Replayer] Could not connect to agent after "
                  << max_connect_attempts
                  << " attempts. Is agent.exe running and listening on port "
                  << SERVER_PORT << "?" << std::endl;
        closesocket(connect_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "[Replayer] Connected to agent successfully." << std::endl;

    std::ifstream csv_file(CSV_PATH);
    if (!csv_file.is_open())
    {
        std::cerr << "[Replayer] Failed to open CSV file at: " << CSV_PATH
                  << " (run the binary from the project root)" << std::endl;
        closesocket(connect_socket);
        WSACleanup();
        return 1;
    }

    std::string line;
    bool is_header = true;
    int ticks_sent = 0;

    while (std::getline(csv_file, line))
    {
        if (line.empty())
        {
            continue;
        }
        if (is_header)
        {
            is_header = false;
            continue;
        }

        TickData tick;
        if (!parse_tick(line, tick))
        {
            continue;
        }

        if (!send_all(connect_socket, reinterpret_cast<const char *>(&tick), sizeof(TickData)))
        {
            std::cerr << "[Replayer] Transmission failed. Aborting replay." << std::endl;
            break;
        }

        ticks_sent++;
        std::cout << "[Replayer] Sent tick #" << ticks_sent << " -> ";
        tick.print();

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    csv_file.close();

    std::cout << "[Replayer] Replay complete. Total ticks sent: " << ticks_sent << std::endl;

    shutdown(connect_socket, SD_SEND);
    closesocket(connect_socket);
    WSACleanup();

    return 0;
}