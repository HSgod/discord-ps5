/*
 * Accord - TCP as a core::Stream, for the host tests and the console payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform/net/tcp_stream.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace accord::net
{
namespace
{
std::int64_t now_ms() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string error_text(const char *call) noexcept
{
    return std::string{call} + ": " + std::strerror(errno);
}

void set_timeout(int fd, int option, int milliseconds) noexcept
{
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
}
} // namespace

TcpStream::~TcpStream()
{
    close();
}

TcpStream::TcpStream(TcpStream &&other) noexcept : fd_{other.fd_}, error_{std::move(other.error_)}
{
    other.fd_ = -1;
}

TcpStream &TcpStream::operator=(TcpStream &&other) noexcept
{
    if (this != &other)
    {
        close();
        fd_ = other.fd_;
        error_ = std::move(other.error_);
        other.fd_ = -1;
    }
    return *this;
}

bool TcpStream::connect(std::string_view host, std::uint16_t port, int timeout_ms)
{
    close();
    error_.clear();

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *addresses = nullptr;
    const std::string name{host};
    const std::string service = std::to_string(port);
    const int resolved = ::getaddrinfo(name.c_str(), service.c_str(), &hints, &addresses);
    if (resolved != 0)
    {
        error_ = std::string{"getaddrinfo: "} + ::gai_strerror(resolved);
        return false;
    }

    const std::int64_t deadline_ms = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    for (addrinfo *address = addresses; address != nullptr; address = address->ai_next)
    {
        const int fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0)
        {
            error_ = error_text("socket");
            continue;
        }

        // A connect that cannot finish inside the wait must not hold the caller:
        // the socket goes non-blocking for the handshake and back afterwards.
        const int flags = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        bool connected = false;
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0)
        {
            connected = true;
        }
        else if (errno == EINPROGRESS)
        {
            const std::int64_t left = deadline_ms - now_ms();
            pollfd waiting{};
            waiting.fd = fd;
            waiting.events = POLLOUT;
            if (left > 0 && ::poll(&waiting, 1, static_cast<int>(left)) == 1)
            {
                int pending = 0;
                socklen_t size = sizeof(pending);
                ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending, &size);
                if (pending == 0)
                {
                    connected = true;
                }
                else
                {
                    error_ = std::string{"connect: "} + std::strerror(pending);
                }
            }
            else
            {
                error_ = "connect: timed out";
            }
        }
        else
        {
            error_ = error_text("connect");
        }

        if (!connected)
        {
            ::close(fd);
            continue;
        }

        ::fcntl(fd, F_SETFL, flags);
        const int yes = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        ::freeaddrinfo(addresses);
        fd_ = fd;
        return true;
    }

    ::freeaddrinfo(addresses);
    if (error_.empty())
        error_ = "connect: no address could be reached";
    return false;
}

core::StreamResult TcpStream::read(std::uint8_t *data, std::size_t size, int timeout_ms) noexcept
{
    if (fd_ < 0)
        return {core::StreamStatus::failed, 0};

    // Zero means "look, do not wait", and that is the one wait the kernel cannot
    // express through SO_RCVTIMEO, where zero reads as "no timeout at all" and
    // would block forever. MSG_DONTWAIT says it exactly. Anything shorter than a
    // millisecond is not worth a round trip to the socket layer.
    const bool look_only = timeout_ms <= 0;
    set_timeout(fd_, SO_RCVTIMEO, look_only ? 1 : timeout_ms);

    while (true)
    {
        const ssize_t got = ::recv(fd_, data, size, look_only ? MSG_DONTWAIT : 0);
        if (got > 0)
            return {core::StreamStatus::ok, static_cast<std::size_t>(got)};
        if (got == 0)
            return {core::StreamStatus::closed, 0};
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return {core::StreamStatus::timeout, 0};
        error_ = error_text("recv");
        return {core::StreamStatus::failed, 0};
    }
}

core::StreamResult TcpStream::write(const std::uint8_t *data, std::size_t size,
                                    int timeout_ms) noexcept
{
    if (fd_ < 0)
        return {core::StreamStatus::failed, 0};

    set_timeout(fd_, SO_SNDTIMEO, timeout_ms > 0 ? timeout_ms : 1);

    std::size_t sent = 0;
    while (sent < size)
    {
        const ssize_t chunk = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
        if (chunk > 0)
        {
            sent += static_cast<std::size_t>(chunk);
            continue;
        }
        if (chunk < 0 && errno == EINTR)
            continue;
        if (chunk < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            error_ = "send: timed out";
        }
        else
        {
            error_ = error_text("send");
        }
        // Nothing here reports how much of a frame went out: a peer that has
        // half of it cannot be talked to any more anyway.
        return {core::StreamStatus::failed, sent};
    }

    return {core::StreamStatus::ok, sent};
}

void TcpStream::close() noexcept
{
    if (fd_ >= 0)
    {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

bool TcpStream::is_open() const noexcept
{
    return fd_ >= 0;
}
} // namespace accord::net
