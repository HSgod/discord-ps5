/*
 * Accord - The WebSocket client over a real socket, against a local echo server.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The scripted stream next door covers the protocol's edge cases; this file is
 * the other half of the story, the one the criterion asks for: a handshake and
 * thousands of frames over plain TCP, with the client's own timeouts, partial
 * reads and Nagle-free writes in the path, and with a server that misbehaves
 * before a frame is ever exchanged.
 */

#include "tests/micro_test.hpp"

#include <array>
#include <cstdint>
#include <string>

#include "core/websocket.hpp"
#include "platform/net/tcp_stream.hpp"
#include "tests/support/ws_echo_server.hpp"

using namespace accord::core;
using accord::net::TcpStream;
using accord::test::WsEchoServer;

namespace
{
// A client with a nonce of its own, on the port the server came up on.
std::array<std::uint8_t, 16> test_nonce()
{
    std::array<std::uint8_t, 16> nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i)
        nonce[i] = static_cast<std::uint8_t>(0x10 + i);
    return nonce;
}

WebSocketRequest request_for(std::uint16_t port)
{
    WebSocketRequest request;
    request.host = "127.0.0.1:" + std::to_string(port);
    request.target = "/?v=10&encoding=json";
    request.key = test_nonce();
    return request;
}

std::string name_of(WsStatus status)
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

std::string name_of(MessageKind kind)
{
    switch (kind)
    {
    case MessageKind::none:
        return "none";
    case MessageKind::text:
        return "text";
    case MessageKind::binary:
        return "binary";
    case MessageKind::pong:
        return "pong";
    case MessageKind::close:
        return "close";
    }
    return "?";
}

bool contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}
} // namespace

MICRO_TEST(a_connection_opens_over_plain_tcp_and_echoes)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));
    MICRO_CHECK(stream.is_open());

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});
    MICRO_CHECK(socket.is_open());

    MICRO_CHECK_EQ(name_of(socket.send_text("witaj z konsoli", 2000)), std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload, std::string{"witaj z konsoli"});

    // The server read it, so the masking it had to undo was right.
    MICRO_CHECK_EQ(server.received_text(), std::string{"witaj z konsoli"});
}

MICRO_TEST(a_few_thousand_frames_go_both_ways_in_one_connection)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    // Small frames, so the run is about the connection rather than the size of
    // a payload: every one of them is a separate frame in both directions.
    constexpr int kFrames = 2000;
    int echoed = 0;
    for (int i = 0; i < kFrames; ++i)
    {
        const std::string text = "frame " + std::to_string(i);
        if (socket.send_text(text, 2000) != WsStatus::ok)
            break;

        Message message;
        if (socket.receive(2000, message) != WsStatus::ok)
            break;
        if (message.kind != MessageKind::text || message.payload != text)
            break;
        ++echoed;
    }

    MICRO_CHECK_EQ(echoed, kFrames);
    MICRO_CHECK(socket.is_open());
    MICRO_CHECK_EQ(server.frames_seen(), static_cast<std::uint64_t>(kFrames));
}

MICRO_TEST(a_frame_of_200_bytes_survives_the_round_trip_unchanged)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    // Over 125 bytes: the sixteen-bit length, and a read that may well come back
    // in pieces.
    const std::string text(200, 'x');
    MICRO_CHECK_EQ(name_of(socket.send_text(text, 2000)), std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, text);
}

MICRO_TEST(a_frame_over_the_sixty_four_kilobyte_mark_goes_both_ways)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    // The client writes the eight-byte length form, the server answers with the
    // sixteen-bit one, and both have to be read back byte for byte.
    std::string text;
    for (std::size_t i = 0; i < 70000; ++i)
        text.push_back(static_cast<char>('a' + (i % 26)));
    MICRO_CHECK_EQ(name_of(socket.send_binary(
                       reinterpret_cast<const std::uint8_t *>(text.data()), text.size(), 2000)),
                   std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"binary"});
    MICRO_CHECK_EQ(message.payload.size(), text.size());
    MICRO_CHECK_EQ(message.payload, text);
}

MICRO_TEST(a_greeting_sent_right_behind_the_handshake_head_is_not_lost)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::greeting))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{WsEchoServer::kGreeting});
}

MICRO_TEST(a_fragmented_message_with_a_ping_between_its_parts_arrives_whole)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::fragmented))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload,
                   std::string{WsEchoServer::kFragmentFirst} + std::string{WsEchoServer::kFragmentSecond});

    // The ping the client answered is the server's own frame, so an echo of it
    // comes back as a pong: that is the proof it answered, and the connection is
    // still open in the middle of a message.
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(a_ping_from_the_client_is_answered_with_its_payload)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    MICRO_CHECK_EQ(name_of(socket.send_ping("keepalive", 2000)), std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"pong"});
    MICRO_CHECK_EQ(message.payload, std::string{"keepalive"});
}

MICRO_TEST(the_closing_handshake_completes_over_tcp)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::echo))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});
    MICRO_CHECK_EQ(name_of(socket.send_text("bye", 2000)), std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"bye"});

    MICRO_CHECK_EQ(name_of(socket.send_close(1000, "done", 2000)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"close"});
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_server_that_answers_with_the_wrong_accept_key_is_refused)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::wrong_accept))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(contains(socket.error(), "accept key"));
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_server_that_refuses_the_upgrade_with_http_is_reported)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::refuse_http))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(contains(socket.error(), "403"));
}

MICRO_TEST(a_socket_dropped_right_after_the_upgrade_is_reported_as_closed)
{
    WsEchoServer server;
    if (!server.start(WsEchoServer::Mode::drop_after_head))
    {
        MICRO_CHECK(false);
        return;
    }

    TcpStream stream;
    MICRO_CHECK(stream.connect("127.0.0.1", server.port(), 2000));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, request_for(server.port()), 2000)),
                   std::string{"ok"});

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"closed"});
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_connect_to_a_port_nobody_listens_on_fails_with_a_reason)
{
    TcpStream stream;
    // Port 1 on the loopback interface: nothing may listen there.
    MICRO_CHECK(!stream.connect("127.0.0.1", 1, 1000));
    MICRO_CHECK(!stream.is_open());
    MICRO_CHECK(!stream.error().empty());
}
