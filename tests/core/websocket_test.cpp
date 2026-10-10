/*
 * Accord - Tests for the WebSocket client, through a scripted stream.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Everything a peer can do to a client is written down here as bytes the fake
 * stream hands over, in the order they arrive: the frames, the pieces of frames,
 * and the illegal ones. The socket path has its own, smaller file next door.
 */

#include "tests/micro_test.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/websocket.hpp"
#include "tests/support/memory_stream.hpp"

using namespace accord::core;
using accord::test::MemoryStream;

namespace
{
// The nonce from the RFC's own example, so the key and the accept key below are
// the ones printed in RFC 6455 1.3 and 4.2.2.
constexpr std::array<std::uint8_t, 16> kSampleNonce = {'t', 'h', 'e', ' ', 's', 'a', 'm',
                                                       'p', 'l', 'e', ' ', 'n', 'o', 'n',
                                                       'c', 'e'};
constexpr std::string_view kSampleKey = "dGhlIHNhbXBsZSBub25jZQ==";
constexpr std::string_view kSampleAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

constexpr std::uint8_t kOpContinuation = 0x0;
constexpr std::uint8_t kOpText = 0x1;
constexpr std::uint8_t kOpBinary = 0x2;
constexpr std::uint8_t kOpClose = 0x8;
constexpr std::uint8_t kOpPing = 0x9;
constexpr std::uint8_t kOpPong = 0xa;

constexpr std::uint16_t kCodeProtocolError = 1002;
constexpr std::uint16_t kCodeTooLarge = 1009;

WebSocketRequest sample_request()
{
    WebSocketRequest request;
    request.host = "gateway.example";
    request.target = "/?v=10&encoding=json";
    request.key = kSampleNonce;
    return request;
}

// --- writing the peer's side of the wire ---

// A frame as a server sends it: not masked, unless a test wants to see what the
// client does with one that is.
std::string server_frame(std::uint8_t opcode, std::string_view payload, bool fin = true,
                         bool mask = false)
{
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
    const std::size_t size = payload.size();
    const std::uint8_t masked = mask ? 0x80 : 0x00;
    if (size < 126)
    {
        out.push_back(static_cast<char>(masked | size));
    }
    else if (size <= 0xffff)
    {
        out.push_back(static_cast<char>(masked | 126));
        out.push_back(static_cast<char>((size >> 8) & 0xff));
        out.push_back(static_cast<char>(size & 0xff));
    }
    else
    {
        out.push_back(static_cast<char>(masked | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> shift) & 0xff));
    }

    const std::uint8_t key[4] = {0x21, 0x43, 0x65, 0x87};
    if (mask)
        out.append(reinterpret_cast<const char *>(key), sizeof(key));
    for (std::size_t i = 0; i < size; ++i)
    {
        const char byte = payload[i];
        out.push_back(mask ? static_cast<char>(static_cast<std::uint8_t>(byte) ^ key[i % 4])
                           : byte);
    }
    return out;
}

// A frame head announcing a length the test does not want to carry.
std::string frame_head(std::uint8_t opcode, std::uint64_t length, bool fin = true)
{
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80 : 0x00) | opcode));
    if (length < 126)
    {
        out.push_back(static_cast<char>(length));
    }
    else if (length <= 0xffff)
    {
        out.push_back(static_cast<char>(126));
        out.push_back(static_cast<char>((length >> 8) & 0xff));
        out.push_back(static_cast<char>(length & 0xff));
    }
    else
    {
        out.push_back(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8)
            out.push_back(static_cast<char>((length >> shift) & 0xff));
    }
    return out;
}

// A close frame's payload: the code, then the reason.
std::string close_payload(std::uint16_t code, std::string_view reason = "")
{
    std::string out;
    out.push_back(static_cast<char>((code >> 8) & 0xff));
    out.push_back(static_cast<char>(code & 0xff));
    out.append(reason);
    return out;
}

std::string handshake_response(std::string_view accept = kSampleAccept,
                               std::string_view extra_headers = "")
{
    std::string out;
    out += "HTTP/1.1 101 Switching Protocols\r\n";
    out += "Upgrade: websocket\r\n";
    out += "Connection: Upgrade\r\n";
    out += "Sec-WebSocket-Accept: " + std::string{accept} + "\r\n";
    out += extra_headers;
    out += "\r\n";
    return out;
}

// --- reading the client's side back ---

struct OutFrame
{
    bool ok = false;
    bool fin = false;
    bool masked = false;
    std::uint8_t opcode = 0;
    std::array<std::uint8_t, 4> mask{};
    std::string payload;
    std::size_t size = 0; // the whole frame, head included
};

OutFrame decode_frame(std::string_view bytes, std::size_t at = 0)
{
    OutFrame frame;
    if (bytes.size() < at + 2)
        return frame;

    const std::uint8_t first = static_cast<std::uint8_t>(bytes[at]);
    const std::uint8_t second = static_cast<std::uint8_t>(bytes[at + 1]);
    frame.fin = (first & 0x80) != 0;
    frame.opcode = static_cast<std::uint8_t>(first & 0x0f);
    frame.masked = (second & 0x80) != 0;

    std::uint64_t length = second & 0x7f;
    std::size_t head = 2;
    if (length == 126)
    {
        if (bytes.size() < at + 4)
            return frame;
        length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(bytes[at + 2])) << 8) |
                 static_cast<std::uint8_t>(bytes[at + 3]);
        head = 4;
    }
    else if (length == 127)
    {
        if (bytes.size() < at + 10)
            return frame;
        length = 0;
        for (std::size_t i = 2; i < 10; ++i)
            length = (length << 8) | static_cast<std::uint8_t>(bytes[at + i]);
        head = 10;
    }

    const std::size_t key_size = frame.masked ? 4 : 0;
    if (bytes.size() < at + head + key_size + length)
        return frame;

    if (frame.masked)
        for (std::size_t i = 0; i < 4; ++i)
            frame.mask[i] = static_cast<std::uint8_t>(bytes[at + head + i]);

    frame.payload.resize(static_cast<std::size_t>(length));
    for (std::size_t i = 0; i < length; ++i)
    {
        const char byte = bytes[at + head + key_size + i];
        frame.payload[i] = frame.masked
                               ? static_cast<char>(static_cast<std::uint8_t>(byte) ^ frame.mask[i % 4])
                               : byte;
    }
    frame.size = head + key_size + static_cast<std::size_t>(length);
    frame.ok = true;
    return frame;
}

std::vector<OutFrame> frames_in(std::string_view bytes)
{
    std::vector<OutFrame> frames;
    std::size_t at = 0;
    while (at < bytes.size())
    {
        const OutFrame frame = decode_frame(bytes, at);
        if (!frame.ok)
            break;
        frames.push_back(frame);
        at += frame.size;
    }
    return frames;
}

OutFrame close_frame_in(std::string_view bytes)
{
    for (const OutFrame &frame : frames_in(bytes))
        if (frame.opcode == kOpClose)
            return frame;
    return OutFrame{};
}

std::uint16_t close_code_of(const OutFrame &frame)
{
    if (frame.payload.size() < 2)
        return 0;
    return static_cast<std::uint16_t>((static_cast<std::uint8_t>(frame.payload[0]) << 8) |
                                      static_cast<std::uint8_t>(frame.payload[1]));
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

// --- the two things every test here does ---

// A handshake, answered correctly before the client asks. The request itself is
// cleared off the wire here, so every test that follows looks at frames alone.
bool open_session(MemoryStream &stream, WebSocket &socket)
{
    stream.feed(handshake_response());
    const bool ok = socket.connect(stream, sample_request(), 1000) == WsStatus::ok;
    stream.clear_written();
    return ok;
}

// From the outside every protocol error looks the same: the status, a
// connection that is not open any more, the words that say what went wrong, a
// close frame carrying 1002 on the wire, and `closed` from then on.
void check_protocol_error(MemoryStream &stream, WebSocket &socket, std::string_view keyword)
{
    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"protocol_error"});
    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK(contains(socket.error(), keyword));
    MICRO_CHECK_EQ(close_code_of(close_frame_in(stream.written())),
                   static_cast<std::size_t>(kCodeProtocolError));
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"closed"});
}
} // namespace

// --- the handshake ---

MICRO_TEST(the_handshake_request_is_the_one_the_rfc_describes)
{
    const std::string head = render_handshake(sample_request());

    MICRO_CHECK(head.starts_with("GET /?v=10&encoding=json HTTP/1.1\r\n"));
    MICRO_CHECK(contains(head, "Host: gateway.example\r\n"));
    MICRO_CHECK(contains(head, "Upgrade: websocket\r\n"));
    MICRO_CHECK(contains(head, "Connection: Upgrade\r\n"));
    MICRO_CHECK(contains(head, "Sec-WebSocket-Key: " + std::string{kSampleKey} + "\r\n"));
    MICRO_CHECK(contains(head, "Sec-WebSocket-Version: 13\r\n"));
    MICRO_CHECK(head.ends_with("\r\n\r\n"));

    // The key is the base64 of the raw nonce, not the nonce itself.
    MICRO_CHECK_EQ(sec_websocket_key(kSampleNonce), std::string{kSampleKey});
}

MICRO_TEST(an_empty_target_falls_back_to_the_root)
{
    WebSocketRequest request = sample_request();
    request.target.clear();
    MICRO_CHECK(render_handshake(request).starts_with("GET / HTTP/1.1\r\n"));
}

MICRO_TEST(the_accept_key_matches_the_example_in_the_rfc)
{
    MICRO_CHECK_EQ(sec_websocket_accept(kSampleKey), std::string{kSampleAccept});
}

MICRO_TEST(a_good_answer_opens_the_connection_and_leaves_a_frame_ready)
{
    MemoryStream stream;
    stream.feed(handshake_response());
    stream.feed(server_frame(kOpText, "hello"));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)), std::string{"ok"});
    MICRO_CHECK(socket.is_open());
    MICRO_CHECK(stream.written().starts_with("GET /?v=10&encoding=json HTTP/1.1\r\n"));

    // The frame behind the head was not thrown away with it.
    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload, std::string{"hello"});
}

MICRO_TEST(a_head_that_arrives_in_pieces_still_opens_the_connection)
{
    MemoryStream stream;
    stream.feed(handshake_response());
    stream.set_read_chunk(1);

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)), std::string{"ok"});
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(the_handshake_gives_up_when_nothing_arrives)
{
    MemoryStream stream;
    WebSocket socket;

    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 40)), std::string{"timeout"});
    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK(!socket.error().empty());
}

MICRO_TEST(a_wrong_accept_key_stops_the_connection)
{
    MemoryStream stream;
    stream.feed(handshake_response("AAAAAAAAAAAAAAAAAAAAAAAAAAA="));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK(contains(socket.error(), "accept key"));
}

MICRO_TEST(an_answer_that_is_not_101_stops_the_connection)
{
    MemoryStream stream;
    stream.feed("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(contains(socket.error(), "403"));
}

MICRO_TEST(an_answer_without_the_upgrade_headers_stops_the_connection)
{
    MemoryStream stream;
    stream.feed("HTTP/1.1 101 Switching Protocols\r\nSec-WebSocket-Accept: " +
                std::string{kSampleAccept} + "\r\n\r\n");

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(contains(socket.error(), "upgrade to websocket"));
}

MICRO_TEST(an_extension_that_was_never_offered_stops_the_connection)
{
    // This is the one that matters for compression: a server that decides to
    // deflate on its own would leave the client reading deflated frames as if
    // they were plain ones, so the answer has to be no.
    MemoryStream stream;
    stream.feed(handshake_response(kSampleAccept, "Sec-WebSocket-Extensions: permessage-deflate\r\n"));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(contains(socket.error(), "extension"));
}

MICRO_TEST(a_subprotocol_that_was_never_offered_stops_the_connection)
{
    MemoryStream stream;
    stream.feed(handshake_response(kSampleAccept, "Sec-WebSocket-Protocol: chat\r\n"));

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)),
                   std::string{"protocol_error"});
    MICRO_CHECK(contains(socket.error(), "subprotocol"));
}

MICRO_TEST(a_handshake_that_cannot_be_written_stops_the_connection)
{
    MemoryStream stream;
    stream.set_fail_writes(true);

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)),
                   std::string{"transport_error"});
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_server_that_goes_away_during_the_handshake_is_reported_as_closed)
{
    MemoryStream stream;
    stream.feed_closed();

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 1000)), std::string{"closed"});
}

// --- frames on the way out ---

MICRO_TEST(a_text_frame_goes_out_masked_and_intact)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_text("hello", 1000)), std::string{"ok"});

    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
    {
        const OutFrame &frame = frames.front();
        MICRO_CHECK(frame.fin);
        MICRO_CHECK(frame.masked);
        MICRO_CHECK_EQ(static_cast<int>(frame.opcode), static_cast<int>(kOpText));
        MICRO_CHECK_EQ(frame.payload, std::string{"hello"});
        MICRO_CHECK_EQ(frame.size, stream.written().size());
    }
    // The text is nowhere on the wire as itself, which is what masking means.
    MICRO_CHECK(!contains(stream.written(), "hello"));
}

MICRO_TEST(a_frame_of_200_bytes_uses_the_sixteen_bit_length)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    const std::string text(200, 'x');
    MICRO_CHECK_EQ(name_of(socket.send_text(text, 1000)), std::string{"ok"});

    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
    {
        MICRO_CHECK_EQ(frames.front().payload, text);
        MICRO_CHECK_EQ(frames.front().size, std::size_t{4 + 4 + 200});
    }
    // 0x80 | 126, then the length big-endian in two bytes.
    const std::string &raw = stream.written();
    MICRO_CHECK(raw.size() > 4);
    MICRO_CHECK_EQ(static_cast<int>(static_cast<std::uint8_t>(raw[1])), 0x80 | 126);
    MICRO_CHECK_EQ(static_cast<int>(static_cast<std::uint8_t>(raw[2])), 0);
    MICRO_CHECK_EQ(static_cast<int>(static_cast<std::uint8_t>(raw[3])), 200);
}

MICRO_TEST(a_frame_of_70000_bytes_uses_the_sixty_four_bit_length)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    const std::string text(70000, 'y');
    MICRO_CHECK_EQ(name_of(socket.send_binary(
                       reinterpret_cast<const std::uint8_t *>(text.data()), text.size(), 1000)),
                   std::string{"ok"});

    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
    {
        MICRO_CHECK_EQ(static_cast<int>(frames.front().opcode), static_cast<int>(kOpBinary));
        MICRO_CHECK_EQ(frames.front().payload, text);
        MICRO_CHECK_EQ(frames.front().size, std::size_t{10 + 4 + 70000});
    }
    MICRO_CHECK_EQ(static_cast<int>(static_cast<std::uint8_t>(stream.written()[1])), 0x80 | 127);
}

MICRO_TEST(every_frame_gets_a_fresh_mask)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_text("one", 1000)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(socket.send_text("two", 1000)), std::string{"ok"});

    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{2});
    if (frames.size() == 2)
        // Random, so a repeat is a one-in-four-billion accident rather than
        // something that cannot happen -- which is the point of it.
        MICRO_CHECK(frames[0].mask != frames[1].mask);
}

MICRO_TEST(a_ping_carries_its_payload_and_a_control_frame_over_125_bytes_is_refused)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_ping("are you there", 1000)), std::string{"ok"});
    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
    {
        MICRO_CHECK_EQ(static_cast<int>(frames.front().opcode), static_cast<int>(kOpPing));
        MICRO_CHECK_EQ(frames.front().payload, std::string{"are you there"});
    }

    const std::size_t written_before = stream.written().size();
    const std::string too_long(126, 'x');
    MICRO_CHECK_EQ(name_of(socket.send_ping(too_long, 1000)), std::string{"too_large"});
    MICRO_CHECK_EQ(stream.written().size(), written_before);
    // The call was wrong, the connection is not.
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(text_that_is_not_utf8_is_refused_before_it_goes_out)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_text("\xff\xfe", 1000)), std::string{"protocol_error"});
    MICRO_CHECK_EQ(stream.written().size(), std::size_t{0});
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(a_close_frame_carries_its_code_and_reason)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_close(1000, "bye", 1000)), std::string{"ok"});
    MICRO_CHECK(socket.close_sent());
    MICRO_CHECK(!socket.is_open());

    const OutFrame close = close_frame_in(stream.written());
    MICRO_CHECK(close.ok);
    MICRO_CHECK_EQ(close_code_of(close), static_cast<std::size_t>(1000));
    MICRO_CHECK_EQ(close.payload.substr(2), std::string{"bye"});
}

MICRO_TEST(an_invalid_close_code_is_refused_before_it_goes_out)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    // 1005 means "no code came with the close" and may never be sent itself.
    MICRO_CHECK_EQ(name_of(socket.send_close(1005, "", 1000)), std::string{"protocol_error"});
    MICRO_CHECK_EQ(stream.written().size(), std::size_t{0});
    MICRO_CHECK(!socket.close_sent());
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(nothing_goes_out_after_the_close_frame)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_close(1000, "", 1000)), std::string{"ok"});
    const std::size_t after_close = stream.written().size();

    // A second close frame would itself be a protocol error, so the second call
    // is a no-op rather than another frame.
    MICRO_CHECK_EQ(name_of(socket.send_close(1001, "", 1000)), std::string{"ok"});
    MICRO_CHECK_EQ(stream.written().size(), after_close);

    MICRO_CHECK_EQ(name_of(socket.send_text("late", 1000)), std::string{"closed"});
    MICRO_CHECK_EQ(stream.written().size(), after_close);
    MICRO_CHECK_EQ(frames_in(stream.written()).size(), std::size_t{1});
}

// --- frames on the way in ---

MICRO_TEST(a_text_frame_from_the_server_arrives_as_text)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.feed(server_frame(kOpText, "witaj"));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload, std::string{"witaj"});
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(a_binary_frame_keeps_its_bytes)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    const std::string bytes{"a\0b\xff", 4};
    stream.feed(server_frame(kOpBinary, bytes));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"binary"});
    MICRO_CHECK_EQ(message.payload, bytes);
    MICRO_CHECK_EQ(message.payload.size(), std::size_t{4});
}

MICRO_TEST(a_fragmented_message_arrives_as_one_message)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "one, ", false));
    stream.feed(server_frame(kOpContinuation, "two, ", false));
    stream.feed(server_frame(kOpContinuation, "three", true));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload, std::string{"one, two, three"});
}

MICRO_TEST(a_character_split_between_fragments_is_still_utf8)
{
    // UTF-8 is checked on the message, not on the pieces: a two-byte character
    // arriving as two fragments is legal, and a validator looking at one
    // fragment at a time would call it a protocol error.
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "\xc3", false));
    stream.feed(server_frame(kOpContinuation, "\xa9", true));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"\xc3\xa9"});
}

MICRO_TEST(a_fragmented_binary_message_stays_binary)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpBinary, "ab", false));
    stream.feed(server_frame(kOpContinuation, "cd", true));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"binary"});
    MICRO_CHECK_EQ(message.payload, std::string{"abcd"});
}

MICRO_TEST(a_ping_between_two_fragments_is_answered_and_the_message_still_arrives)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    stream.feed(server_frame(kOpText, "first, ", false));
    stream.feed(server_frame(kOpPing, "still there?"));
    stream.feed(server_frame(kOpContinuation, "second", true));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload, std::string{"first, second"});

    // The ping never reached the caller: it was answered on the spot.
    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
    {
        MICRO_CHECK_EQ(static_cast<int>(frames.front().opcode), static_cast<int>(kOpPong));
        MICRO_CHECK_EQ(frames.front().payload, std::string{"still there?"});
    }
}

MICRO_TEST(a_standalone_ping_is_answered_and_a_pong_reaches_the_caller)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    stream.feed(server_frame(kOpPing, "hi"));
    stream.feed(server_frame(kOpPong, "hello back"));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"pong"});
    MICRO_CHECK_EQ(message.payload, std::string{"hello back"});

    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
        MICRO_CHECK_EQ(static_cast<int>(frames.front().opcode), static_cast<int>(kOpPong));
}

MICRO_TEST(a_message_that_arrives_one_byte_at_a_time_still_arrives)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.set_read_chunk(1);
    stream.feed(server_frame(kOpText, "slowly but surely"));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"slowly but surely"});
}

MICRO_TEST(a_message_split_across_two_reads_that_are_far_apart_still_arrives)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    // The head and half the text, then a wait that runs out, then the rest.
    const std::string frame = server_frame(kOpText, "abcdefgh");
    stream.feed(std::string_view{frame}.substr(0, 5));
    stream.feed_timeout();
    stream.feed(std::string_view{frame}.substr(5));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"abcdefgh"});
}

// --- closing ---

MICRO_TEST(a_close_from_the_peer_is_answered_with_the_same_code)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    stream.feed(server_frame(kOpClose, close_payload(1001, "going away")));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"close"});
    MICRO_CHECK_EQ(message.close_code, static_cast<std::size_t>(1001));
    MICRO_CHECK_EQ(message.close_reason, std::string{"going away"});
    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK(socket.close_sent());

    const OutFrame close = close_frame_in(stream.written());
    MICRO_CHECK(close.ok);
    MICRO_CHECK_EQ(close_code_of(close), static_cast<std::size_t>(1001));

    // The peer has gone, and the next receive says so rather than waiting.
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"closed"});
}

MICRO_TEST(a_close_without_a_code_is_answered_with_1000)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    stream.feed(server_frame(kOpClose, ""));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"close"});
    MICRO_CHECK_EQ(message.close_code, static_cast<std::size_t>(0));

    // 1000 is what goes out when the peer sent no code of its own.
    const OutFrame close = close_frame_in(stream.written());
    MICRO_CHECK(close.ok);
    MICRO_CHECK_EQ(close_code_of(close), static_cast<std::size_t>(1000));
}

MICRO_TEST(a_close_with_a_registered_code_is_accepted)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpClose, close_payload(4000)));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.close_code, static_cast<std::size_t>(4000));
}

MICRO_TEST(a_client_that_closes_first_hears_the_answer_without_sending_a_second_frame)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    MICRO_CHECK_EQ(name_of(socket.send_close(1000, "done", 1000)), std::string{"ok"});
    stream.feed(server_frame(kOpClose, close_payload(1000, "ok")));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"close"});
    MICRO_CHECK_EQ(message.close_reason, std::string{"ok"});

    // One close frame from the client, and it is the one send_close() wrote.
    MICRO_CHECK_EQ(frames_in(stream.written()).size(), std::size_t{1});
}

// --- what a peer may not do ---

MICRO_TEST(a_masked_frame_from_the_server_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "x", true, true));
    check_protocol_error(stream, socket, "masked");
}

MICRO_TEST(a_reserved_bit_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    std::string frame = server_frame(kOpText, "x");
    frame[0] = static_cast<char>(static_cast<std::uint8_t>(frame[0]) | 0x40); // RSV1
    stream.feed(frame);
    check_protocol_error(stream, socket, "reserved bit");
}

MICRO_TEST(an_unknown_opcode_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(0x3, ""));
    check_protocol_error(stream, socket, "unknown opcode");
}

MICRO_TEST(a_continuation_with_no_message_open_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpContinuation, "orphan"));
    check_protocol_error(stream, socket, "no message open");
}

MICRO_TEST(a_new_message_before_the_fragmented_one_finished_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "first", false));
    stream.feed(server_frame(kOpText, "second"));
    check_protocol_error(stream, socket, "before the fragmented one finished");
}

MICRO_TEST(a_fragmented_control_frame_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpPing, "x", false));
    check_protocol_error(stream, socket, "must not be fragmented");
}

MICRO_TEST(a_control_frame_over_125_bytes_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpPing, std::string(126, 'p')));
    check_protocol_error(stream, socket, "125 bytes");
}

MICRO_TEST(a_length_that_is_not_the_shortest_form_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    // Five bytes announced in the sixteen-bit form.
    std::string frame;
    frame += static_cast<char>(0x80 | kOpText);
    frame += static_cast<char>(126);
    frame += static_cast<char>(0);
    frame += static_cast<char>(5);
    frame += "hello";
    stream.feed(frame);
    check_protocol_error(stream, socket, "shortest form");
}

MICRO_TEST(a_sixty_four_bit_length_that_would_fit_in_sixteen_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    std::string frame;
    frame += static_cast<char>(0x80 | kOpText);
    frame += static_cast<char>(127);
    for (int i = 0; i < 7; ++i)
        frame += static_cast<char>(0);
    frame += static_cast<char>(5);
    frame += "hello";
    stream.feed(frame);
    check_protocol_error(stream, socket, "shortest form");
}

MICRO_TEST(a_length_with_its_top_bit_set_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    std::string frame;
    frame += static_cast<char>(0x80 | kOpText);
    frame += static_cast<char>(127);
    frame += static_cast<char>(0x80);
    for (int i = 0; i < 7; ++i)
        frame += static_cast<char>(0);
    stream.feed(frame);
    check_protocol_error(stream, socket, "top bit");
}

MICRO_TEST(a_close_frame_of_one_byte_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpClose, std::string(1, static_cast<char>(0x03))));
    check_protocol_error(stream, socket, "single byte");
}

MICRO_TEST(a_close_code_that_may_never_be_sent_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpClose, close_payload(1005)));
    check_protocol_error(stream, socket, "invalid code");
}

MICRO_TEST(a_close_code_out_of_the_registered_range_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpClose, close_payload(2999)));
    check_protocol_error(stream, socket, "invalid code");
}

MICRO_TEST(a_text_message_that_is_not_utf8_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "\xc3\x28"));
    check_protocol_error(stream, socket, "not UTF-8");
}

MICRO_TEST(a_fragmented_text_message_that_is_not_utf8_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "\xc3", false));
    stream.feed(server_frame(kOpContinuation, "(", true));
    check_protocol_error(stream, socket, "not UTF-8");
}

MICRO_TEST(a_lone_continuation_byte_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "\xff"));
    check_protocol_error(stream, socket, "not UTF-8");
}

MICRO_TEST(a_close_reason_that_is_not_utf8_is_a_protocol_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpClose, close_payload(1000, "\xff")));
    check_protocol_error(stream, socket, "reason is not UTF-8");
}

// --- messages that are too big ---

MICRO_TEST(a_message_over_the_size_limit_is_cut_off_with_1009)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    // The length alone is enough: nothing of the message has to arrive for the
    // client to refuse to make room for it.
    stream.feed(frame_head(kOpText, WebSocket::kMaxMessageBytes + 1));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"too_large"});
    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK_EQ(close_code_of(close_frame_in(stream.written())),
                   static_cast<std::size_t>(kCodeTooLarge));
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"closed"});
}

MICRO_TEST(a_fragmented_message_that_grows_over_the_limit_is_cut_off)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    stream.feed(server_frame(kOpText, "x", false));
    stream.feed(frame_head(kOpContinuation, WebSocket::kMaxMessageBytes));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"too_large"});
    MICRO_CHECK_EQ(close_code_of(close_frame_in(stream.written())),
                   static_cast<std::size_t>(kCodeTooLarge));
}

MICRO_TEST(a_fragmented_message_that_ends_at_the_limit_is_allowed)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    // The boundary itself is legal; only going past it is not.
    const std::string half(WebSocket::kMaxMessageBytes / 2, 'z');
    stream.feed(server_frame(kOpText, half, false));
    stream.feed(server_frame(kOpContinuation, half, true));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(2000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload.size(), WebSocket::kMaxMessageBytes);
}

// --- the transport underneath ---

MICRO_TEST(a_timeout_keeps_the_connection_and_the_half_read_frame)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    const std::string frame = server_frame(kOpText, "abcdef");
    // Only the first half of the six-byte head is there yet.
    stream.feed(std::string_view{frame}.substr(0, 3));

    Message message;
    message.kind = MessageKind::text; // has to be cleared by the call
    MICRO_CHECK_EQ(name_of(socket.receive(40, message)), std::string{"timeout"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"none"});
    MICRO_CHECK(socket.is_open());

    // The rest arrives, and the bytes of the head that were already read are
    // still where they were.
    stream.feed(std::string_view{frame}.substr(3));
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"abcdef"});
}

MICRO_TEST(a_peer_that_goes_away_in_the_middle_of_a_frame_is_reported_as_closed)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(std::string_view{server_frame(kOpText, "abc")}.substr(0, 4));
    stream.feed_closed();

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"closed"});
    MICRO_CHECK(contains(socket.error(), "middle of a frame"));
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_peer_that_goes_away_between_frames_is_reported_as_closed)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpText, "last words"));
    stream.feed_closed();

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"last words"});

    MICRO_CHECK_EQ(name_of(socket.receive(1000, message)), std::string{"closed"});
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_write_that_fails_becomes_a_transport_error)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.set_fail_writes(true);

    MICRO_CHECK_EQ(name_of(socket.send_text("x", 1000)), std::string{"transport_error"});
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(nothing_can_be_sent_or_received_before_connecting)
{
    WebSocket socket;
    Message message;

    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK_EQ(name_of(socket.receive(10, message)), std::string{"closed"});
    MICRO_CHECK_EQ(name_of(socket.send_text("x", 10)), std::string{"closed"});
    MICRO_CHECK_EQ(name_of(socket.send_close(1000, "", 10)), std::string{"closed"});
}

// --- a wait of zero (a poll) ---
//
// The demon's loop asks the socket for what has arrived without waiting for it,
// so a zero wait has to mean "look once, do not sit there". A zero wait that
// reads nothing at all would leave the loop blind on every poll, and a message
// that arrived before the call would never be picked up.

MICRO_TEST(a_frame_that_is_already_there_comes_out_of_a_zero_wait)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.feed(server_frame(kOpText, "already here"));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"text"});
    MICRO_CHECK_EQ(message.payload, std::string{"already here"});
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(a_zero_wait_brings_out_every_frame_that_is_already_there)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.feed(server_frame(kOpText, "first"));
    stream.feed(server_frame(kOpText, "second"));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"first"});
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"second"});
    // Only now is there nothing left to look at.
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"timeout"});
}

MICRO_TEST(a_zero_wait_on_an_empty_stream_answers_at_once)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    const auto started = std::chrono::steady_clock::now();
    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"timeout"});
    const long long spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();

    // Generous on purpose: a loaded machine may take a few milliseconds, but the
    // call may not wait for the thing the caller said it would not wait for.
    MICRO_CHECK(spent < 100);
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"none"});
    MICRO_CHECK(socket.is_open());
}

MICRO_TEST(a_poll_loop_finds_the_message_once_it_arrives)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"timeout"});
    stream.feed_timeout(); // a poll that found nothing on a live socket
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"timeout"});

    stream.feed(server_frame(kOpText, "late"));
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"late"});
}

MICRO_TEST(a_zero_wait_keeps_the_half_of_a_frame_it_looked_at)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    const std::string frame = server_frame(kOpText, "abcdef");
    stream.feed(std::string_view{frame}.substr(0, 3));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"timeout"});

    // The three bytes it did read are still inside, so the rest completes them.
    stream.feed(std::string_view{frame}.substr(3));
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"ok"});
    MICRO_CHECK_EQ(message.payload, std::string{"abcdef"});
}

MICRO_TEST(a_zero_wait_still_answers_a_ping_that_arrived)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));
    stream.clear_written();

    stream.feed(server_frame(kOpPing, "ask"));

    Message message;
    // A ping is not an event for the caller, so the poll answers it and reports
    // that there was nothing to hand over -- with the pong already on the wire.
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"timeout"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"none"});
    MICRO_CHECK(socket.is_open());

    const std::vector<OutFrame> frames = frames_in(stream.written());
    MICRO_CHECK_EQ(frames.size(), std::size_t{1});
    if (frames.size() == 1)
    {
        MICRO_CHECK_EQ(static_cast<int>(frames.front().opcode), static_cast<int>(kOpPong));
        MICRO_CHECK_EQ(frames.front().payload, std::string{"ask"});
    }
}

MICRO_TEST(a_close_frame_already_there_comes_out_of_a_zero_wait)
{
    MemoryStream stream;
    WebSocket socket;
    MICRO_CHECK(open_session(stream, socket));

    stream.feed(server_frame(kOpClose, close_payload(1000, "bye")));

    Message message;
    MICRO_CHECK_EQ(name_of(socket.receive(0, message)), std::string{"ok"});
    MICRO_CHECK_EQ(name_of(message.kind), std::string{"close"});
    MICRO_CHECK_EQ(static_cast<int>(message.close_code), 1000);
    MICRO_CHECK(!socket.is_open());
}

MICRO_TEST(a_handshake_that_is_already_there_opens_the_connection_with_a_zero_wait)
{
    MemoryStream stream;
    stream.feed(handshake_response());

    WebSocket socket;
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 0)), std::string{"ok"});
    MICRO_CHECK(socket.is_open());

    // A zero wait refuses to wait, not to try: the request went out first.
    MICRO_CHECK(contains(stream.written(), "Sec-WebSocket-Key: " + std::string{kSampleKey}));
}

MICRO_TEST(a_handshake_with_nothing_to_read_answers_timeout_after_one_look)
{
    MemoryStream stream;
    WebSocket socket;

    const auto started = std::chrono::steady_clock::now();
    MICRO_CHECK_EQ(name_of(socket.connect(stream, sample_request(), 0)), std::string{"timeout"});
    const long long spent = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();

    MICRO_CHECK(spent < 100);
    MICRO_CHECK(!socket.is_open());
    MICRO_CHECK(!socket.error().empty());
    MICRO_CHECK(contains(stream.written(), "GET /?v=10&encoding=json HTTP/1.1\r\n"));
}
