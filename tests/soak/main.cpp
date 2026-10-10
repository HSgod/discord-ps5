/*
 * Accord - the ten-minute soak: one WebSocket connection, kept open on purpose.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The criterion for T3.3 is not a pass or a fail, it is a log: a client that
 * holds a connection for ten minutes without the peer going away and without a
 * timeout turning into a disconnect. So this prints a line as it goes -- the
 * elapsed time, the frames exchanged, the bytes and the largest pause seen --
 * and one line at the end that says whether the connection survived.
 *
 * It runs against the echo server of the tests, over the same loopback TCP and
 * the same TcpStream the tests use, so what it exercises is the real thing and
 * not a mock. Usage: build/ws-soak [minutes]
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "core/websocket.hpp"
#include "platform/net/tcp_stream.hpp"
#include "tests/support/ws_echo_server.hpp"

namespace
{
using accord::core::Message;
using accord::core::MessageKind;
using accord::core::WebSocket;
using accord::core::WebSocketRequest;
using accord::core::WsStatus;
using accord::net::TcpStream;
using accord::test::WsEchoServer;

constexpr int kTickMs = 2000;      // a frame every two seconds
constexpr int kReportEvery = 15;   // one log line every thirty seconds
constexpr int kReplyWaitMs = 5000; // the peer answers within this, or it is gone

const char *name_of(WsStatus status)
{
    switch (status)
    {
    case WsStatus::ok:
        return "ok";
    case WsStatus::timeout:
        return "timeout";
    case WsStatus::closed:
        return "closed";
    case WsStatus::protocol_error:
        return "protocol_error";
    case WsStatus::transport_error:
        return "transport_error";
    case WsStatus::too_large:
        return "too_large";
    }
    return "?";
}

long milliseconds_between(std::chrono::steady_clock::time_point from,
                          std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}
} // namespace

int main(int argc, char **argv)
{
    const double minutes = argc > 1 ? std::atof(argv[1]) : 10.0;
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::milliseconds{
                                         static_cast<long long>(minutes * 60.0 * 1000.0)};

    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        std::printf("SOAK FAILED: the echo server did not start: %s\n", server.error().c_str());
        return 1;
    }

    TcpStream stream;
    if (!stream.connect("127.0.0.1", server.port(), 5000))
    {
        std::printf("SOAK FAILED: %s\n", stream.error().c_str());
        return 1;
    }

    WebSocketRequest request;
    request.host = "127.0.0.1:" + std::to_string(server.port());
    request.target = "/?v=10&encoding=json";
    for (std::size_t i = 0; i < request.key.size(); ++i)
        request.key[i] = static_cast<std::uint8_t>(i * 7 + 3);

    WebSocket socket;
    if (socket.connect(stream, request, 5000) != WsStatus::ok)
    {
        std::printf("SOAK FAILED: the handshake did not complete: %s\n", socket.error().c_str());
        return 1;
    }

    std::printf("SOAK START: 127.0.0.1:%u, handshake done after %ld ms, running for %.1f minutes\n",
                static_cast<unsigned>(server.port()),
                milliseconds_between(started, std::chrono::steady_clock::now()), minutes);

    std::uint64_t frames = 0;
    std::uint64_t bytes = 0;
    long longest_wait_ms = 0;
    int tick = 0;
    bool failed = false;
    std::string failure;

    while (std::chrono::steady_clock::now() < deadline && !failed)
    {
        const auto tick_started = std::chrono::steady_clock::now();

        // Two kinds of traffic, so both directions of the protocol stay in use:
        // a ping the server answers at once, and a text frame it echoes back.
        const std::string text = "soak frame " + std::to_string(tick);
        const bool as_text = (tick % 3) == 0;
        const WsStatus sent = as_text ? socket.send_text(text, kReplyWaitMs)
                                      : socket.send_ping(text, kReplyWaitMs);
        if (sent != WsStatus::ok)
        {
            failed = true;
            failure = std::string{"sending frame "} + std::to_string(tick) + ": " + name_of(sent) +
                      " (" + socket.error() + ")";
            break;
        }

        Message message;
        const WsStatus received = socket.receive(kReplyWaitMs, message);
        if (received != WsStatus::ok)
        {
            failed = true;
            failure = std::string{"waiting for frame "} + std::to_string(tick) + ": " +
                      name_of(received) + " (" + socket.error() + ")";
            break;
        }

        const bool right_kind =
            as_text ? message.kind == MessageKind::text : message.kind == MessageKind::pong;
        if (!right_kind || message.payload != text)
        {
            failed = true;
            failure = std::string{"frame "} + std::to_string(tick) + " came back wrong";
            break;
        }

        ++frames;
        bytes += message.payload.size();

        const long wait_ms = milliseconds_between(tick_started, std::chrono::steady_clock::now());
        if (wait_ms > longest_wait_ms)
            longest_wait_ms = wait_ms;

        ++tick;
        if (tick % kReportEvery == 0)
            std::printf("SOAK %6ld ms: %llu frames, %llu bytes, the connection is open\n",
                        milliseconds_between(started, std::chrono::steady_clock::now()),
                        static_cast<unsigned long long>(frames),
                        static_cast<unsigned long long>(bytes));

        std::this_thread::sleep_for(std::chrono::milliseconds{kTickMs});
    }

    const long elapsed_ms = milliseconds_between(started, std::chrono::steady_clock::now());
    const bool still_open = socket.is_open();

    if (socket.send_close(1000, "soak done", kReplyWaitMs) != WsStatus::ok)
    {
        failed = true;
        failure = "the closing frame could not be sent";
    }
    else
    {
        Message message;
        const WsStatus closed = socket.receive(kReplyWaitMs, message);
        if (closed != WsStatus::ok || message.kind != MessageKind::close)
        {
            failed = true;
            failure = std::string{"the closing handshake did not complete: "} + name_of(closed);
        }
    }

    if (failed)
    {
        std::printf("SOAK FAILED after %ld ms and %llu frames: %s\n", elapsed_ms,
                    static_cast<unsigned long long>(frames), failure.c_str());
        return 1;
    }

    std::printf("SOAK DONE: %ld ms (%.1f minutes), %llu frames, %llu bytes, the connection was "
                "open the whole time (%s), longest frame wait %ld ms, clean close\n",
                elapsed_ms, elapsed_ms / 60000.0, static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(bytes), still_open ? "yes" : "no", longest_wait_ms);
    return elapsed_ms >= static_cast<long>(minutes * 60.0 * 1000.0) ? 0 : 1;
}
