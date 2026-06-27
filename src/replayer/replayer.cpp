#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdint>

#include "common/types.hpp"
#include "common/logger.hpp"
#include "networking/tcp_client.hpp"

using arbitrade::Logger;
using arbitrade::TcpClient;
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

    // Captures a monotonic, high-resolution timestamp in nanoseconds. Used
    // to stamp each tick right before it goes out on the wire so the agent
    // side can later compute one-way transport latency if needed.
    uint64_t now_nanoseconds()
    {
        auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
    }

} // namespace

int main()
{
    Logger::info("Replayer: NexusTrade Market Data Replayer starting...");

    TcpClient client;
    if (!client.connect_to(SERVER_IP, SERVER_PORT))
    {
        Logger::error("Replayer: could not connect to agent. Is agent.exe running and listening on port " +
                      std::to_string(SERVER_PORT) + "?");
        return 1;
    }

    std::ifstream csv_file(CSV_PATH);
    if (!csv_file.is_open())
    {
        Logger::error(std::string("Replayer: failed to open CSV file at: ") + CSV_PATH +
                      " (run the binary from the project root)");
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

        // Stamp the send timestamp as close to the wire as possible —
        // immediately before send_all(), after all parsing is complete.
        tick.replay_send_timestamp_ns = now_nanoseconds();

        if (!client.send_all(reinterpret_cast<const char *>(&tick), sizeof(TickData)))
        {
            Logger::error("Replayer: transmission failed. Aborting replay.");
            break;
        }

        ticks_sent++;
        Logger::info("Replayer: sent tick #" + std::to_string(ticks_sent));
        tick.print();

        // Pace the replay so console output on both ends stays readable.
        // Single-thread sleep, not a spawned worker thread.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    csv_file.close();

    Logger::info("Replayer: replay complete. Total ticks sent: " + std::to_string(ticks_sent));

    return 0;
}