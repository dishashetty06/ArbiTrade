#pragma once

#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <chrono>
#include <ctime>

namespace arbitrade
{

    // Lightweight, header-only, static logger. Replaces raw std::cout calls
    // scattered across the codebase with three intent-revealing entry
    // points. Deliberately has no internal state and no locking — the
    // project is single-threaded by design (no std::thread anywhere), so
    // there is no contention to guard against.
    class Logger
    {
    public:
        static void info(const std::string &message)
        {
            write(std::cout, "INFO ", message);
        }

        static void warn(const std::string &message)
        {
            write(std::cout, "WARN ", message);
        }

        static void error(const std::string &message)
        {
            write(std::cerr, "ERROR", message);
        }

    private:
        static void write(std::ostream &out, const char *level, const std::string &message)
        {
            out << "[" << timestamp_string() << "] [" << level << "] " << message << std::endl;
        }

        // Wall-clock HH:MM:SS.mmm timestamp for log readability. Uses the
        // MSVC-safe localtime_s rather than the deprecated std::localtime.
        static std::string timestamp_string()
        {
            using namespace std::chrono;

            auto now = system_clock::now();
            auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
            std::time_t t = system_clock::to_time_t(now);
            std::tm tm_buf{};

            localtime_s(&tm_buf, &t);

            std::ostringstream oss;
            oss << std::put_time(&tm_buf, "%H:%M:%S")
                << "." << std::setfill('0') << std::setw(3) << ms.count();
            return oss.str();
        }
    };

}