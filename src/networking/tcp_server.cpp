#include "networking/tcp_server.hpp"
#include "common/logger.hpp"

#include <cstring>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace arbitrade
{

    TcpServer::TcpServer()
        : wsa_initialized_(false),
          listen_socket_(INVALID_SOCKET),
          client_socket_(INVALID_SOCKET)
    {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0)
        {
            Logger::error("TcpServer: WSAStartup failed with error: " + std::to_string(result));
            return;
        }
        wsa_initialized_ = true;
    }

    TcpServer::~TcpServer()
    {
        cleanup_all();
    }

    void TcpServer::cleanup_client()
    {
        if (client_socket_ != INVALID_SOCKET)
        {
            closesocket(client_socket_);
            client_socket_ = INVALID_SOCKET;
        }
    }

    void TcpServer::cleanup_all()
    {
        cleanup_client();
        if (listen_socket_ != INVALID_SOCKET)
        {
            closesocket(listen_socket_);
            listen_socket_ = INVALID_SOCKET;
        }
        if (wsa_initialized_)
        {
            WSACleanup();
            wsa_initialized_ = false;
        }
    }

    bool TcpServer::start_listening(unsigned short port)
    {
        if (!wsa_initialized_)
        {
            Logger::error("TcpServer: cannot start_listening, WSAStartup never succeeded.");
            return false;
        }

        listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpServer: socket() failed with error: " + std::to_string(WSAGetLastError()));
            return false;
        }

        int reuse_opt = 1;
        setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&reuse_opt), sizeof(reuse_opt));

        sockaddr_in service_addr;
        std::memset(&service_addr, 0, sizeof(service_addr));
        service_addr.sin_family = AF_INET;
        service_addr.sin_addr.s_addr = INADDR_ANY;
        service_addr.sin_port = htons(port);

        if (bind(listen_socket_, reinterpret_cast<sockaddr *>(&service_addr), sizeof(service_addr)) == SOCKET_ERROR)
        {
            Logger::error("TcpServer: bind() failed with error: " + std::to_string(WSAGetLastError()));
            cleanup_all();
            return false;
        }

        if (listen(listen_socket_, SOMAXCONN) == SOCKET_ERROR)
        {
            Logger::error("TcpServer: listen() failed with error: " + std::to_string(WSAGetLastError()));
            cleanup_all();
            return false;
        }

        Logger::info("TcpServer: listening on port " + std::to_string(port));
        return true;
    }

    bool TcpServer::accept_client()
    {
        if (listen_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpServer: accept_client() called before start_listening() succeeded.");
            return false;
        }

        sockaddr_in client_addr;
        int client_addr_len = sizeof(client_addr);
        client_socket_ = accept(listen_socket_, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);

        if (client_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpServer: accept() failed with error: " + std::to_string(WSAGetLastError()));
            return false;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        Logger::info(std::string("TcpServer: client connected from ") + client_ip +
                     ":" + std::to_string(ntohs(client_addr.sin_port)));

        return true;
    }

    bool TcpServer::recv_all(char *buffer, int length)
    {
        if (client_socket_ == INVALID_SOCKET)
        {
            Logger::error("TcpServer: recv_all() called with no active client connection.");
            return false;
        }

        int total_received = 0;
        while (total_received < length)
        {
            int received = recv(client_socket_, buffer + total_received, length - total_received, 0);
            if (received == 0)
            {
                Logger::info("TcpServer: connection closed by peer.");
                cleanup_client();
                return false;
            }
            if (received == SOCKET_ERROR)
            {
                Logger::error("TcpServer: recv() failed with error: " + std::to_string(WSAGetLastError()));
                cleanup_client();
                return false;
            }
            total_received += received;
        }
        return true;
    }

} // namespace nexustrade