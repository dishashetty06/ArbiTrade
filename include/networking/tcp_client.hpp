#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

namespace arbitrade
{

    // RAII-compliant TCP client wrapper around WinSock2. Owns the connection
    // socket and handles a bounded retry loop on connect() so the replayer
    // can be launched slightly before or after the agent without failing
    // outright.
    class TcpClient
    {
    public:
        TcpClient();
        ~TcpClient();

        TcpClient(const TcpClient &) = delete;
        TcpClient &operator=(const TcpClient &) = delete;

        // Attempts to connect to host:port, retrying up to `max_attempts`
        // times with `retry_delay_ms` between attempts. Returns true once
        // connected.
        bool connect_to(const std::string &host, unsigned short port,
                        int max_attempts = 10, int retry_delay_ms = 500);

        // Sends exactly `length` bytes from `buffer`, looping until the full
        // payload has been written to the socket. Returns false on error.
        bool send_all(const char *buffer, int length);

        bool is_connected() const { return connect_socket_ != INVALID_SOCKET; }

    private:
        void cleanup_all();

        bool wsa_initialized_;
        SOCKET connect_socket_;
    };

} // namespace nexustrade