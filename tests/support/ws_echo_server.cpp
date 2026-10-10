/*
 * Accord - A local WebSocket server for the tests that need a real socket.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/support/ws_echo_server.hpp"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "core/websocket.hpp"

namespace accord::test
{
namespace
{
constexpr std::size_t kMaxHead = 16 * 1024;
constexpr std::size_t kReadChunk = 4096;
constexpr int kReadTimeoutMs = 500;

std::string lower_ascii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    return out;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
        text.remove_suffix(1);
    return text;
}

// The one header this server needs off the request. The name is compared
// without case, the value is kept exactly as it arrived: the accept key is
// computed from the base64 text, where case carries meaning.
std::string header_value(std::string_view head, std::string_view name)
{
    const std::string wanted = lower_ascii(name);
    std::size_t at = head.find("\r\n");
    if (at == std::string_view::npos)
        return {};
    at += 2;
    while (at < head.size())
    {
        const std::size_t end = head.find("\r\n", at);
        if (end == std::string_view::npos || end == at)
            return {};
        const std::string_view line = head.substr(at, end - at);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && lower_ascii(line.substr(0, colon)) == wanted)
            return std::string{trim(line.substr(colon + 1))};
        at = end + 2;
    }
    return {};
}

// Server-side frames are never masked, so this is the short version of what the
// client has to do.
std::string frame(std::uint8_t opcode, std::string_view payload, bool fin = true)
{
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
    const std::size_t size = payload.size();
    if (size < 126)
    {
        out.push_back(static_cast<char>(size));
    }
    else if (size <= 0xffff)
    {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((size >> 8) & 0xff));
        out.push_back(static_cast<char>(size & 0xff));
    }
    else
    {
        out.push_back(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> shift) & 0xff));
    }
    out.append(payload);
    return out;
}

void set_timeout(int fd, int option, int milliseconds)
{
    timeval timeout{};
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, option, &timeout, sizeof(timeout));
}
} // namespace

WsEchoServer::~WsEchoServer()
{
    stop();
}

bool WsEchoServer::start(Mode mode)
{
    stop();
    mode_ = mode;
    stopping_.store(false);

    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
    {
        error_ = std::string{"socket: "} + std::strerror(errno);
        return false;
    }

    const int reuse = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        error_ = std::string{"bind: "} + std::strerror(errno);
        stop();
        return false;
    }
    if (::listen(listener_, 1) != 0)
    {
        error_ = std::string{"listen: "} + std::strerror(errno);
        stop();
        return false;
    }

    socklen_t size = sizeof(address);
    if (::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) != 0)
    {
        error_ = std::string{"getsockname: "} + std::strerror(errno);
        stop();
        return false;
    }
    port_ = ntohs(address.sin_port);

    worker_ = std::thread{[this] { serve(); }};
    return true;
}

void WsEchoServer::stop() noexcept
{
    stopping_.store(true);
    if (listener_ >= 0)
    {
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        listener_ = -1;
    }
    if (client_ >= 0)
    {
        ::shutdown(client_, SHUT_RDWR);
        ::close(client_);
        client_ = -1;
    }
    if (worker_.joinable())
        worker_.join();
}

bool WsEchoServer::send_all(std::string_view bytes) noexcept
{
    if (client_ < 0)
        return false;

    set_timeout(client_, SO_SNDTIMEO, 1000);
    std::size_t sent = 0;
    while (sent < bytes.size())
    {
        const ssize_t chunk = ::send(client_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (chunk > 0)
        {
            sent += static_cast<std::size_t>(chunk);
            continue;
        }
        if (chunk < 0 && errno == EINTR)
            continue;
        return false;
    }
    return true;
}

bool WsEchoServer::read_head(std::string *head) noexcept
{
    std::string collected;
    char buffer[kReadChunk];

    while (collected.find("\r\n\r\n") == std::string::npos)
    {
        if (collected.size() > kMaxHead)
            return false;
        set_timeout(client_, SO_RCVTIMEO, kReadTimeoutMs);
        const ssize_t got = ::recv(client_, buffer, sizeof(buffer), 0);
        if (got > 0)
        {
            collected.append(buffer, static_cast<std::size_t>(got));
            continue;
        }
        if (got == 0)
            return false;
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        return false;
    }

    *head = std::move(collected);
    return true;
}

void WsEchoServer::serve_frames() noexcept
{
    std::string pending;

    // Reads until `count` bytes sit in pending. False means the connection is
    // going away.
    const auto fill = [this, &pending](std::size_t count) {
        while (pending.size() < count)
        {
            if (stopping_.load())
                return false;
            char buffer[kReadChunk];
            set_timeout(client_, SO_RCVTIMEO, kReadTimeoutMs);
            const ssize_t got = ::recv(client_, buffer, sizeof(buffer), 0);
            if (got > 0)
            {
                pending.append(buffer, static_cast<std::size_t>(got));
                continue;
            }
            if (got == 0)
                return false;
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        return true;
    };

    while (!stopping_.load())
    {
        // The head: two bytes, then the extended length if the first byte says
        // there is one, then the payload. The client always masks, so the
        // payload comes with its four-byte key in front of it.
        if (!fill(2))
            return;

        const std::uint8_t first = static_cast<std::uint8_t>(pending[0]);
        const std::uint8_t second = static_cast<std::uint8_t>(pending[1]);
        const std::uint8_t opcode = first & 0x0f;
        const bool masked = (second & 0x80) != 0;
        const std::uint64_t marker = second & 0x7f;

        std::size_t head = 2;
        if (marker == 126 || marker == 127)
        {
            head = marker == 126 ? 4 : 10;
            if (!fill(head))
                return;
        }

        std::uint64_t length = marker;
        if (marker == 126)
            length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(pending[2])) << 8) |
                     static_cast<std::uint8_t>(pending[3]);
        else if (marker == 127)
        {
            length = 0;
            for (std::size_t i = 2; i < 10; ++i)
                length = (length << 8) | static_cast<std::uint8_t>(pending[i]);
        }

        const std::size_t key_size = masked ? 4 : 0;
        const std::size_t whole = head + key_size + static_cast<std::size_t>(length);
        if (!fill(whole))
            return;

        std::string payload = pending.substr(head + key_size, static_cast<std::size_t>(length));
        if (masked)
        {
            const std::uint8_t *key = reinterpret_cast<const std::uint8_t *>(pending.data() + head);
            for (std::size_t i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<char>(static_cast<std::uint8_t>(payload[i]) ^ key[i % 4]);
        }
        pending.erase(0, whole);
        ++frames_seen_;

        if (opcode == 0x8)
        {
            send_all(frame(0x8, payload));
            return;
        }
        if (opcode == 0x9)
        {
            send_all(frame(0xa, payload));
            continue;
        }
        if (opcode == 0xa)
            continue;

        // Only data frames reach this point -- ping, pong and close are answered
        // above -- so the frame goes straight back, fragment flags and all.
        received_text_.append(payload);
        if (!send_all(frame(opcode, payload, (first & 0x80) != 0)))
            return;
    }
}

void WsEchoServer::serve() noexcept
{
    client_ = ::accept(listener_, nullptr, nullptr);
    if (client_ < 0)
        return;

    std::string head;
    if (!read_head(&head))
        return;

    const std::string key = header_value(head, "Sec-WebSocket-Key");

    if (mode_ == Mode::refuse_http)
    {
        send_all("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        return;
    }

    const std::string accept =
        mode_ == Mode::wrong_accept ? std::string{"AAAAAAAAAAAAAAAAAAAAAAAAAAA="}
                                    : accord::core::sec_websocket_accept(key);

    std::string response;
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + accept + "\r\n";
    response += "\r\n";
    if (!send_all(response))
        return;

    if (mode_ == Mode::drop_after_head)
    {
        // The point of this mode: the socket goes away without a close frame.
        ::shutdown(client_, SHUT_RDWR);
        ::close(client_);
        client_ = -1;
        return;
    }

    if (mode_ == Mode::greeting)
        send_all(frame(0x1, kGreeting));

    if (mode_ == Mode::fragmented)
    {
        send_all(frame(0x1, kFragmentFirst, false));
        send_all(frame(0x9, "between")); // a ping in the middle of a message
        send_all(frame(0x0, kFragmentSecond, true));
    }

    serve_frames();
}
} // namespace accord::test
