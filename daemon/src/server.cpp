/*
 * Discord PS5 - HTTP server of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/server.hpp"

#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace discord_ps5::daemon
{
namespace
{
constexpr std::size_t kMaxRequestBytes = 4096;
constexpr std::size_t kResponseChunk = 512;
constexpr int kIoTimeoutMs = 500;
constexpr int kBacklog = 4;

// A client that stops talking mid-request must not hold the daemon forever.
void set_io_timeout(int socket, int option) noexcept
{
    timeval timeout{};
    timeout.tv_sec = kIoTimeoutMs / 1000;
    timeout.tv_usec = (kIoTimeoutMs % 1000) * 1000;
    ::setsockopt(socket, SOL_SOCKET, option, &timeout, sizeof(timeout));
}

std::string read_head(int socket) noexcept
{
    std::string head;
    char buffer[kResponseChunk];

    while (head.size() < kMaxRequestBytes)
    {
        const ssize_t got = ::recv(socket, buffer, sizeof(buffer), 0);
        if (got <= 0)
            break;
        head.append(buffer, static_cast<std::size_t>(got));
        if (head.find("\r\n\r\n") != std::string::npos)
            break;
    }
    return head;
}

void send_all(int socket, std::string_view text) noexcept
{
    std::size_t sent = 0;
    while (sent < text.size())
    {
        const ssize_t chunk = ::send(socket, text.data() + sent, text.size() - sent, 0);
        if (chunk <= 0)
            return;
        sent += static_cast<std::size_t>(chunk);
    }
}

RequestLine first_line(const std::string &head) noexcept
{
    if (head.empty())
        return {};

    const std::size_t end = head.find("\r\n");
    const std::size_t length = end == std::string::npos ? head.size() : end;
    return parse_request_line(std::string_view{head.data(), length});
}
} // namespace

Server::~Server()
{
    close();
}

bool Server::listen(std::uint16_t port) noexcept
{
    close();

    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0)
    {
        remember("socket", fd_);
        return false;
    }

    const int reuse = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (::bind(fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        remember("bind", -1);
        close();
        return false;
    }
    if (::listen(fd_, kBacklog) != 0)
    {
        remember("listen", -1);
        close();
        return false;
    }

    port_ = port;
    return true;
}

void Server::poll(const RequestHandler &handler) noexcept
{
    if (fd_ < 0)
        return;

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd_, &readable);

    timeval timeout{};
    if (::select(fd_ + 1, &readable, nullptr, nullptr, &timeout) <= 0)
        return;

    const int client = ::accept(fd_, nullptr, nullptr);
    if (client < 0)
        return;

    set_io_timeout(client, SO_RCVTIMEO);
    set_io_timeout(client, SO_SNDTIMEO);

    const std::string head = read_head(client);

    Response response;
    if (head.empty())
    {
        response.code = 400;
        response.reason = "Bad Request";
        response.body = "empty request\n";
    }
    else
    {
        response = handler(first_line(head));
    }

    send_all(client, render_response(response));

    ::shutdown(client, SHUT_RDWR);
    ::close(client);
}

void Server::close() noexcept
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }
    port_ = 0;
}

bool Server::is_listening() const noexcept
{
    return fd_ >= 0;
}

std::uint16_t Server::port() const noexcept
{
    return port_;
}

std::string_view Server::last_error() const noexcept
{
    return error_;
}

void Server::remember(const char *call, int code) noexcept
{
    std::snprintf(error_, sizeof(error_), "%s: %s", call, std::strerror(errno));
    (void)code;
}
} // namespace discord_ps5::daemon
