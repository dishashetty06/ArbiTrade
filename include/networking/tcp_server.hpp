#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

namespace arbitrade
{

    // RAII-compliant TCP server wrapper around WinSock2. Owns the listening
    // socket and the accepted client socket. All socket lifecycle management
    // (WSAStartup/WSACleanup, closesocket) is handled internally so calling
    // code (agent.cpp) never touches a SOCKET handle directly — it never
    // even needs to #include <winsock2.h>.
    class TcpServer
    {
    public:
        TcpServer();
        ~TcpServer();

        // Non-copyable: a SOCKET handle cannot be safely duplicated via copy.
        TcpServer(const TcpServer &) = delete;
        TcpServer &operator=(const TcpServer &) = delete;

        // Binds and listens on the given port. Returns true on success.
        bool start_listening(unsigned short port);

        // Blocks until a client connects. Returns true once a client socket
        // has been accepted and is ready for recv_all().
        bool accept_client();

        // Reads exactly `length` bytes into `buffer`, blocking until the
        // full payload has arrived (or the connection drops). Returns false
        // on disconnect or socket error.
        bool recv_all(char *buffer, int length);

        // True once WSAStartup + socket + bind + listen all succeeded.
        bool is_listening() const { return listen_socket_ != INVALID_SOCKET; }

        // True once a client has been accepted and the connection is open.
        bool is_client_connected() const { return client_socket_ != INVALID_SOCKET; }

    private:
        void cleanup_client();
        void cleanup_all();

        bool wsa_initialized_;
        SOCKET listen_socket_;
        SOCKET client_socket_;
    };

} // namespace arbitrade