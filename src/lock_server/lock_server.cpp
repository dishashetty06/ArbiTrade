// Bump FD_SETSIZE before including winsock2.h so select() can track more
// than the Windows default of 64 sockets if the agent fleet grows. We
// only expect a handful of agents in this simulator, but this keeps the
// ceiling explicit rather than silently inherited.
#define FD_SETSIZE 64

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdint>

#include "common/types.hpp"
#include "common/logger.hpp"

#pragma comment(lib, "ws2_32.lib")

using arbitrade::LockRequest;
using arbitrade::LockResponse;
using arbitrade::Logger;

namespace
{

    constexpr unsigned short LOCK_SERVER_PORT = 7777;

    // Tracks one connected agent's socket and however many bytes of its
    // current in-flight LockRequest have arrived so far. Because the
    // listening and client sockets are all non-blocking, a single recv()
    // call is not guaranteed to deliver the full 24-byte LockRequest in one
    // shot — this buffer lets the server accumulate partial reads across
    // multiple select() iterations without blocking on any one client.
    struct ClientConnection
    {
        SOCKET sock;
        char recv_buffer[sizeof(LockRequest)];
        int bytes_received;
    };

    // Monotonic millisecond clock used purely for internal lock expiration
    // bookkeeping — never exposed on the wire, so wall-clock drift doesn't
    // matter here.
    uint64_t now_ms()
    {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    // Pure decision logic for one lock request: grant if the symbol is
    // unlocked or its previous lock has expired; deny (without mutating
    // state) otherwise.
    LockResponse process_lock_request(const LockRequest &request,
                                      std::unordered_map<std::string, uint64_t> &active_locks)
    {
        LockResponse response{};
        std::string symbol(request.symbol);
        uint64_t current_time = now_ms();

        auto it = active_locks.find(symbol);
        bool symbol_is_available = (it == active_locks.end()) || (it->second <= current_time);

        if (symbol_is_available)
        {
            active_locks[symbol] = current_time + request.duration_ms;
            response.success = true;
            Logger::info("LockServer: GRANTED lock on " + symbol +
                         " for " + std::to_string(request.duration_ms) + "ms");
        }
        else
        {
            uint64_t remaining_ms = it->second - current_time;
            response.success = false;
            Logger::info("LockServer: DENIED lock on " + symbol +
                         " (already held for another " + std::to_string(remaining_ms) + "ms)");
        }

        return response;
    }

    // Sets a socket to non-blocking mode via WinSock's ioctlsocket. Used for
    // both the listening socket (non-blocking accept) and every accepted
    // client socket (non-blocking recv/send), which is what makes a single
    // select()-driven loop able to service multiple agents without threads.
    bool set_non_blocking(SOCKET sock)
    {
        u_long mode = 1;
        return ioctlsocket(sock, FIONBIO, &mode) == 0;
    }

} // namespace

int main()
{
    Logger::info("LockServer: ArbiTrade Distributed Lock Server starting...");

    WSADATA wsa_data;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0)
    {
        Logger::error("LockServer: WSAStartup failed with error: " + std::to_string(wsa_result));
        return 1;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET)
    {
        Logger::error("LockServer: socket() failed with error: " + std::to_string(WSAGetLastError()));
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
    service_addr.sin_port = htons(LOCK_SERVER_PORT);

    if (bind(listen_socket, reinterpret_cast<sockaddr *>(&service_addr), sizeof(service_addr)) == SOCKET_ERROR)
    {
        Logger::error("LockServer: bind() failed with error: " + std::to_string(WSAGetLastError()));
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        Logger::error("LockServer: listen() failed with error: " + std::to_string(WSAGetLastError()));
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    if (!set_non_blocking(listen_socket))
    {
        Logger::error("LockServer: failed to set listening socket to non-blocking mode.");
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    Logger::info("LockServer: listening on port " + std::to_string(LOCK_SERVER_PORT));

    std::vector<ClientConnection> clients;
    std::unordered_map<std::string, uint64_t> active_locks;

    while (true)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_socket, &read_fds);
        for (const ClientConnection &client : clients)
        {
            FD_SET(client.sock, &read_fds);
        }

        // A finite timeout means the loop wakes periodically even with
        // zero traffic, rather than blocking in select() forever — keeps
        // the server responsive to future additions (e.g. a shutdown
        // flag) without needing a separate thread.
        timeval select_timeout;
        select_timeout.tv_sec = 1;
        select_timeout.tv_usec = 0;

        // nfds is ignored by Windows' select() implementation; passing 0
        // is the conventional no-op value here.
        int activity_count = select(0, &read_fds, nullptr, nullptr, &select_timeout);

        if (activity_count == SOCKET_ERROR)
        {
            Logger::error("LockServer: select() failed with error: " + std::to_string(WSAGetLastError()));
            break;
        }

        if (activity_count == 0)
        {
            continue; // Timed out with no activity — loop and select() again.
        }

        // --- Accept any new agent connections ---
        if (FD_ISSET(listen_socket, &read_fds))
        {
            sockaddr_in client_addr;
            int client_addr_len = sizeof(client_addr);
            SOCKET new_client_sock = accept(listen_socket,
                                            reinterpret_cast<sockaddr *>(&client_addr),
                                            &client_addr_len);

            if (new_client_sock != INVALID_SOCKET)
            {
                if (set_non_blocking(new_client_sock))
                {
                    ClientConnection new_conn{};
                    new_conn.sock = new_client_sock;
                    new_conn.bytes_received = 0;
                    std::memset(new_conn.recv_buffer, 0, sizeof(new_conn.recv_buffer));
                    clients.push_back(new_conn);

                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
                    Logger::info(std::string("LockServer: agent connected from ") + ip_str +
                                 ":" + std::to_string(ntohs(client_addr.sin_port)) +
                                 " (total agents: " + std::to_string(clients.size()) + ")");
                }
                else
                {
                    Logger::error("LockServer: failed to set new client socket to non-blocking; dropping connection.");
                    closesocket(new_client_sock);
                }
            }
        }

        // --- Service every client that has data ready ---
        std::vector<size_t> indices_to_remove;

        for (size_t i = 0; i < clients.size(); ++i)
        {
            ClientConnection &conn = clients[i];

            if (!FD_ISSET(conn.sock, &read_fds))
            {
                continue;
            }

            int bytes_remaining = static_cast<int>(sizeof(LockRequest)) - conn.bytes_received;
            int received = recv(conn.sock, conn.recv_buffer + conn.bytes_received, bytes_remaining, 0);

            if (received == 0)
            {
                Logger::info("LockServer: agent disconnected.");
                closesocket(conn.sock);
                indices_to_remove.push_back(i);
                continue;
            }

            if (received == SOCKET_ERROR)
            {
                int err = WSAGetLastError();
                if (err == WSAEWOULDBLOCK)
                {
                    // select() said readable but the data evaporated
                    // (rare spurious wakeup) — just try again next cycle.
                    continue;
                }
                Logger::error("LockServer: recv() failed with error: " + std::to_string(err));
                closesocket(conn.sock);
                indices_to_remove.push_back(i);
                continue;
            }

            conn.bytes_received += received;

            if (conn.bytes_received < static_cast<int>(sizeof(LockRequest)))
            {
                // Partial request — wait for the rest on a future
                // select() cycle. Common over loopback for tiny structs
                // but handled correctly regardless.
                continue;
            }

            // Full LockRequest has arrived — decode, decide, respond.
            LockRequest request;
            std::memcpy(&request, conn.recv_buffer, sizeof(LockRequest));

            LockResponse response = process_lock_request(request, active_locks);

            const char *response_bytes = reinterpret_cast<const char *>(&response);
            int response_size = static_cast<int>(sizeof(LockResponse));
            int total_sent = 0;
            bool send_failed = false;

            while (total_sent < response_size)
            {
                int sent = send(conn.sock, response_bytes + total_sent, response_size - total_sent, 0);
                if (sent == SOCKET_ERROR)
                {
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK)
                    {
                        // Send buffer momentarily full on a 1-byte
                        // payload is effectively impossible on loopback,
                        // but spin-retry rather than drop the response.
                        continue;
                    }
                    Logger::error("LockServer: send() failed with error: " + std::to_string(err));
                    send_failed = true;
                    break;
                }
                total_sent += sent;
            }

            if (send_failed)
            {
                closesocket(conn.sock);
                indices_to_remove.push_back(i);
                continue;
            }

            // Reset for the next LockRequest on this same persistent
            // connection — the agent keeps reusing one socket for every
            // signal it ever detects.
            conn.bytes_received = 0;
        }

        // Remove dead connections, highest index first so earlier
        // indices in the vector remain valid during erase.
        for (auto it = indices_to_remove.rbegin(); it != indices_to_remove.rend(); ++it)
        {
            clients.erase(clients.begin() + static_cast<long>(*it));
        }
    }

    for (const ClientConnection &client : clients)
    {
        closesocket(client.sock);
    }
    closesocket(listen_socket);
    WSACleanup();

    return 0;
}