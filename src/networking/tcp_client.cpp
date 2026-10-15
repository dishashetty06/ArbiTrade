#include "networking/tcp_client.hpp"
#include "common/logger.hpp"

#include <cstring>
#include <thread>
#include <chrono>

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

    void TcpClient::disconnect()
    {
        if (connect_socket_ != INVALID_SOCKET)
        {
            shutdown(connect_socket_, SD_SEND);
            closesocket(connect_socket_);
            connect_socket_ = INVALID_SOCKET;
        }
    }

    void TcpClient::cleanup_all()
    {
        disconnect();
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

        // Drop any previous (possibly dead) connection so connect_to() can
        // be called again to reconnect.
        disconnect();

        sockaddr_in server_addr;
        std::memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0)
        {
            Logger::error("TcpClient: inet_pton failed for host: " + host);
            return false;
        }

        Logger::info("TcpClient: connecting to " + host + ":" + std::to_string(port) + " ...");

        for (int attempt = 1; attempt <= max_attempts; ++attempt)
        {
            // A socket whose connect() failed is in an unspecified state
            // (POSIX requires closing it), so every attempt gets a fresh one.
            connect_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (connect_socket_ == INVALID_SOCKET)
            {
                Logger::error("TcpClient: socket() failed with error: " + std::to_string(WSAGetLastError()));
                return false;
            }

            if (connect(connect_socket_,
                        reinterpret_cast<sockaddr *>(&server_addr),
                        sizeof(server_addr)) != SOCKET_ERROR)
            {
                Logger::info("TcpClient: connected successfully.");
                return true;
            }

            int err = WSAGetLastError();
            disconnect();

            if (attempt == max_attempts)
            {
                break;
            }

            Logger::warn("TcpClient: connect() attempt " + std::to_string(attempt) +
                         " failed with error: " + std::to_string(err) +
                         ". Retrying in " + std::to_string(retry_delay_ms) + "ms...");

            // Bounded retry backoff only — this pauses the single calling
            // thread; it does not spawn a new thread of execution.
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }

        Logger::error("TcpClient: could not connect after " + std::to_string(max_attempts) + " attempts.");
        return false;
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
                disconnect();
                return false;
            }
            total_sent += sent;
        }
        return true;
    }

    bool TcpClient::recv_all(char *buffer, int length)
    {
        if (connect_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpClient: recv_all() called with no active connection.");
            return false;
        }

        int total_received = 0;
        while (total_received < length)
        {
            int received = recv(connect_socket_, buffer + total_received, length - total_received, 0);
            if (received == 0)
            {
                Logger::error("TcpClient: connection closed by peer during recv_all().");
                disconnect();
                return false;
            }
            if (received == SOCKET_ERROR)
            {
                Logger::error("TcpClient: recv() failed with error: " + std::to_string(WSAGetLastError()));
                disconnect();
                return false;
            }
            total_received += received;
        }
        return true;
    }

} // namespace arbitrade