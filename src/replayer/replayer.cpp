#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <stdexcept>

#include "common/types.hpp"
#include "common/logger.hpp"
#include "networking/tcp_client.hpp"

using arbitrade::Logger;
using arbitrade::TcpClient;
using arbitrade::TickData;

namespace
{

    constexpr const char *SERVER_IP = "127.0.0.1";

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
            Logger::warn("Replayer: skipping malformed line (expected 7 fields, got " +
                         std::to_string(fields.size()) + "): " + line);
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
            Logger::error(std::string("Replayer: failed to parse line: ") + line + " (" + e.what() + ")");
            return false;
        }

        return true;
    }

    uint64_t now_nanoseconds()
    {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    }

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: replayer.exe <CSV_PATH> <PORT_1> [PORT_2] [PORT_3] ..." << std::endl;
        return 1;
    }

    std::string csv_path = argv[1];

    std::vector<unsigned short> target_ports;
    for (int i = 2; i < argc; ++i)
    {
        try
        {
            int parsed = std::stoi(argv[i]);
            if (parsed <= 0 || parsed > 65535)
            {
                throw std::out_of_range("port must be between 1 and 65535");
            }
            target_ports.push_back(static_cast<unsigned short>(parsed));
        }
        catch (const std::exception &e)
        {
            Logger::error(std::string("Replayer: invalid port argument '") + argv[i] + "': " + e.what());
            return 1;
        }
    }

    Logger::info("Replayer: NexusTrade/ArbiTrade Market Data Replayer starting, targeting " +
                 std::to_string(target_ports.size()) + " agent(s)...");

    // Each TcpClient is non-copyable, so the broadcast list is a vector
    // of owning pointers rather than a vector of TcpClient by value.
    std::vector<std::unique_ptr<TcpClient>> agent_clients;
    agent_clients.reserve(target_ports.size());

    for (unsigned short port : target_ports)
    {
        auto client = std::make_unique<TcpClient>();
        if (client->connect_to(SERVER_IP, port))
        {
            Logger::info("Replayer: connected to agent on port " + std::to_string(port));
            agent_clients.push_back(std::move(client));
        }
        else
        {
            Logger::error("Replayer: failed to connect to agent on port " + std::to_string(port) +
                          ". It will be excluded from this replay.");
        }
    }

    if (agent_clients.empty())
    {
        Logger::error("Replayer: no agents reachable on any provided port. Aborting.");
        return 1;
    }

    std::ifstream csv_file(csv_path);
    if (!csv_file.is_open())
    {
        Logger::error("Replayer: failed to open CSV file at: " + csv_path);
        return 1;
    }

    std::string line;
    bool is_header = true;
    int ticks_sent = 0;
    uint64_t sequence = 0;

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

        sequence++;
        tick.sequence_number = sequence;
        tick.replay_send_timestamp_ns = now_nanoseconds();

        // Fan out this tick to every still-connected agent. Single
        // thread, sequential sends — this is the TCP equivalent of a UDP
        // broadcast given the "no std::thread" constraint, not a true
        // simultaneous send, but every agent receives the same tick
        // before the replayer advances to the next one.
        std::vector<size_t> dead_client_indices;

        for (size_t i = 0; i < agent_clients.size(); ++i)
        {
            if (!agent_clients[i]->send_all(reinterpret_cast<const char *>(&tick), sizeof(TickData)))
            {
                Logger::error("Replayer: send to an agent failed; removing it from the broadcast list.");
                dead_client_indices.push_back(i);
            }
        }

        for (auto it = dead_client_indices.rbegin(); it != dead_client_indices.rend(); ++it)
        {
            agent_clients.erase(agent_clients.begin() + static_cast<long>(*it));
        }

        if (agent_clients.empty())
        {
            Logger::error("Replayer: all agent connections lost. Aborting replay.");
            break;
        }

        ticks_sent++;
        Logger::info("Replayer: broadcast tick #" + std::to_string(ticks_sent) +
                     " to " + std::to_string(agent_clients.size()) + " agent(s)");
        tick.print();

        // Pace the replay so console output across all agents stays
        // readable. Single-thread sleep, not a spawned worker thread.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    csv_file.close();

    Logger::info("Replayer: replay complete. Total ticks sent: " + std::to_string(ticks_sent));

    return 0;
}