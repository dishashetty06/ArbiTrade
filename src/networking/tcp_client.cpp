#include "networking/tcp_client.hpp"
#include "common/logger.hpp"

#include <cstring>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

namespace arbitrade
{

    TcpClient::TcpClient()
        : wsa_initialized_(false),
          connect_socket_(INVALID_SOCKET)
    {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0)
        {
            Logger::error("TcpClient: WSAStartup failed with error: " + std::to_string(result));
            return;
        }
        wsa_initialized_ = true;
    }

    TcpClient::~TcpClient()
    {
        cleanup_all();
    }

    void TcpClient::cleanup_all()
    {
        if (connect_socket_ != INVALID_SOCKET)
        {
            shutdown(connect_socket_, SD_SEND);
            closesocket(connect_socket_);
            connect_socket_ = INVALID_SOCKET;
        }
        if (wsa_initialized_)
        {
            WSACleanup();
            wsa_initialized_ = false;
        }
    }

    bool TcpClient::connect_to(const std::string &host, unsigned short port,
                               int max_attempts, int retry_delay_ms)
    {
        if (!wsa_initialized_)
        {
            Logger::error("TcpClient: cannot connect_to, WSAStartup never succeeded.");
            return false;
        }

        connect_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (connect_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpClient: socket() failed with error: " + std::to_string(WSAGetLastError()));
            return false;
        }

        sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0)
        {
            Logger::error("TcpClient: inet_pton failed for host: " + host);
            cleanup_all();
            return false;
        }

        Logger::info("TcpClient: connecting to " + host + ":" + std::to_string(port) + " ...");

        int attempts = 0;
        int connect_result = SOCKET_ERROR;

        while (attempts < max_attempts)
        {
            connect_result = connect(connect_socket_,
                                     reinterpret_cast<sockaddr *>(&server_addr),
                                     sizeof(server_addr));
            if (connect_result != SOCKET_ERROR)
            {
                break;
            }
            attempts++;
            Logger::warn("TcpClient: connect() attempt " + std::to_string(attempts) +
                         " failed with error: " + std::to_string(WSAGetLastError()) +
                         ". Retrying in " + std::to_string(retry_delay_ms) + "ms...");

            // Bounded retry backoff only — this pauses the single calling
            // thread; it does not spawn a new thread of execution.
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }

        if (connect_result == SOCKET_ERROR)
        {
            Logger::error("TcpClient: could not connect after " + std::to_string(max_attempts) + " attempts.");
            cleanup_all();
            return false;
        }

        Logger::info("TcpClient: connected successfully.");
        return true;
    }

    bool TcpClient::send_all(const char *buffer, int length)
    {
        if (connect_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpClient: send_all() called with no active connection.");
            return false;
        }

        int total_sent = 0;
        while (total_sent < length)
        {
            int sent = send(connect_socket_, buffer + total_sent, length - total_sent, 0);
            if (sent == SOCKET_ERROR)
            {
                Logger::error("TcpClient: send() failed with error: " + std::to_string(WSAGetLastError()));
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

} // namespace nexustrade