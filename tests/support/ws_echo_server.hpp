/*
 * Accord - A local WebSocket server for the tests that need a real socket.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The in-memory stream covers the protocol's edge cases; this exists for the
 * other half of the story -- a handshake and a few thousand frames over TCP,
 * with the client's own read timeouts, partial reads and Nagle-free writes in
 * the path -- and for the ways a server can misbehave before a frame is ever
 * exchanged (wrong accept key, an HTTP error, a socket dropped at once).
 *
 * It serves one connection on an ephemeral loopback port, in its own thread.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

namespace accord::test
{
class WsEchoServer
{
  public:
    enum class Mode
    {
        echo,            // handshake, then echo; pings answered, close answered
        greeting,        // a text frame sent right behind the handshake head
        fragmented,      // a fragmented message with a ping between its fragments
        wrong_accept,    // 101, but the accept key does not match the nonce
        refuse_http,     // "HTTP/1.1 403 Forbidden", then the socket closes
        drop_after_head, // 101, then the socket closes at once
    };

    static constexpr std::string_view kGreeting = "hello from the server";
    static constexpr std::string_view kFragmentFirst = "first half, ";
    static constexpr std::string_view kFragmentSecond = "second half";

    WsEchoServer() = default;
    WsEchoServer(const WsEchoServer &) = delete;
    WsEchoServer &operator=(const WsEchoServer &) = delete;
    ~WsEchoServer();

    // Binds 127.0.0.1:0 and starts serving. False means no port came up and
    // error() says why.
    bool start(Mode mode);

    std::uint16_t port() const noexcept
    {
        return port_;
    }
    const std::string &error() const noexcept
    {
        return error_;
    }

    // Shuts the connection and the listener down and joins the thread. Safe to
    // call twice, and safe when start() was never called.
    void stop() noexcept;

    // The text frames the client sent, joined; what the echo came from.
    const std::string &received_text() const noexcept
    {
        return received_text_;
    }
    // How many frames the server read after the handshake.
    std::uint64_t frames_seen() const noexcept
    {
        return frames_seen_;
    }

  private:
    void serve() noexcept;
    bool read_head(std::string *head) noexcept;
    bool send_all(std::string_view bytes) noexcept;
    void serve_frames() noexcept;

    int listener_ = -1;
    int client_ = -1;
    std::uint16_t port_ = 0;
    Mode mode_ = Mode::echo;
    std::string error_;
    std::string received_text_;
    std::uint64_t frames_seen_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};
} // namespace accord::test
