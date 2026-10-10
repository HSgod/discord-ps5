/*
 * Accord - HTTP server of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/server.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace accord::daemon
{
namespace
{
constexpr std::size_t kMaxRequestBytes = 4096;
constexpr std::size_t kResponseChunk = 512;
constexpr int kSendTimeoutMs = 500;
constexpr int kBacklog = 4;

// Monotonic, so a clock step cannot stretch a budget. Only the socket code
// needs it; the budget itself takes the time as an argument.
std::int64_t now_us() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// A client that stops talking mid-request must not hold the daemon forever.
void set_timeout(int socket, int option, int milliseconds) noexcept
{
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(socket, SOL_SOCKET, option, &timeout, sizeof(timeout));
}

// Reads the request head under a total budget: each recv waits no longer than
// what is left of it, so the loop ends after RequestBudget::kBudgetUs at most,
// however slowly the client types.
std::string read_head(int socket) noexcept
{
    std::string head;
    char buffer[kResponseChunk];

    const RequestBudget budget{now_us()};
    while (head.size() < kMaxRequestBytes)
    {
        const int step_ms = budget.next_timeout_ms(now_us());
        if (step_ms == 0)
            break;

        set_timeout(socket, SO_RCVTIMEO, step_ms);
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

std::int64_t RequestBudget::remaining_us(std::int64_t now_us) const noexcept
{
    const std::int64_t left = kBudgetUs - (now_us - started_us_);
    return left > 0 ? left : 0;
}

bool RequestBudget::expired(std::int64_t now_us) const noexcept
{
    return remaining_us(now_us) == 0;
}

int RequestBudget::next_timeout_ms(std::int64_t now_us) const noexcept
{
    const std::int64_t left = remaining_us(now_us);
    if (left == 0)
        return 0;

    // Rounded up, so the last fraction of a millisecond still gets one try
    // rather than ending the read a moment early.
    const std::int64_t milliseconds = (left + 999) / 1000;
    return static_cast<int>(milliseconds < kMaxStepMs ? milliseconds : kMaxStepMs);
}

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

    set_timeout(client, SO_SNDTIMEO, kSendTimeoutMs);

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
} // namespace accord::daemon
