/*
 * Accord - WebSocket client (RFC 6455) over an abstract byte stream.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The whole protocol lives here, and nothing in this file knows what a socket
 * is: the stream comes in as core::Stream, so the host tests drive the client
 * over plain TCP today and the TLS client of T3.1 drops underneath it later
 * without touching this code.
 *
 * The shape of the API follows the rest of core/: no exceptions, and errors are
 * values. A timeout is the status of its own rather than a failure -- a client
 * waiting on a gateway for minutes is doing exactly that -- and partial bytes
 * of a frame that has not arrived stay inside the object, so a caller that
 * times out and comes back later never loses the middle of a message.
 *
 * What it does itself, because every client of RFC 6455 must: answers pings,
 * masks its own frames, reassembles fragmented messages, accepts control frames
 * between fragments, verifies the accept key of the handshake, and fails the
 * connection with the right close code when the peer breaks the protocol.
 *
 * What it deliberately leaves out: permessage-deflate, the extension RFC 7692
 * negotiates inside the handshake, and `zstd-stream`. A server that raised RSV1
 * -- the bit that extension uses -- would be compressing a frame this client did
 * not agree to, and every RSV bit stays a protocol error.
 *
 * Discord's own transport compression is not an extension and is read here:
 * a request with zlib_stream set asks for `compress=zlib-stream`, and from then
 * on the binary frames are pieces of one zlib stream rather than messages
 * (T3.3b, core/zlib_stream.hpp). The events it carries come out of receive()
 * as text, which is what they are: one JSON payload per flush.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/stream.hpp"
#include "core/zlib_stream.hpp"

namespace accord::core
{
// What opens a connection: where it goes, the nonce it starts with, and whether
// the events come back compressed.
struct WebSocketRequest
{
    std::string host;   // the Host header; the port stays out of it
    std::string target; // the request target, e.g. "/?v=10&encoding=json"
    std::array<std::uint8_t, 16> key{}; // fill_random() supplies this in the app
    // True when the target says `compress=zlib-stream`: the two belong together,
    // because this is what tells the reader below that a binary frame is a piece
    // of the gateway's compressed stream and not a message of its own.
    bool zlib_stream = false;
};

// RFC 6455 4.2.1: the key is the base64 of the 16-byte nonce.
std::string sec_websocket_key(const std::array<std::uint8_t, 16> &key);

// RFC 6455 4.2.2: base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")),
// with `key` as the text of the header, not the raw nonce.
std::string sec_websocket_accept(std::string_view key);

// The request exactly as it goes on the wire, blank line included.
std::string render_handshake(const WebSocketRequest &request);

enum class WsStatus
{
    ok,
    timeout,         // nothing arrived inside the wait the caller asked for
    closed,          // the peer closed, or the transport ended
    protocol_error,  // the peer broke RFC 6455; a close frame has gone out
    transport_error, // the stream failed
    too_large,       // a message over kMaxMessageBytes; close 1009 has gone out
};

enum class MessageKind
{
    none,
    text,  // a text frame, or an event decompressed out of the zlib stream
    binary,
    pong,  // the peer answered a ping; pings themselves are answered inside receive()
    close, // the peer closed; the reply has gone out, closing the stream is the caller's
};

struct Message
{
    MessageKind kind = MessageKind::none;
    // The frame on the wire the payload came out of. Same as kind for a message
    // the peer sent whole; `binary` for an event that came out of the zlib
    // stream, which is what the gateway's compressed transport puts its events
    // in and what a caller may want to see for itself.
    MessageKind frame_kind = MessageKind::none;
    std::string payload;           // the body for text, binary and pong
    std::uint16_t close_code = 0;  // close: the peer's code, 0 when it sent none
    std::string close_reason;      // close: the text the peer attached, if any
};

class WebSocket
{
  public:
    // One message. The gateway's payloads are far smaller; this is the point
    // where a broken or hostile peer is cut off rather than allowed to ask for
    // memory. A compressed connection has two of these limits: this one for the
    // frame on the wire, and ZlibStream::kMaxInflatedBytes for what it inflates
    // to after the message is decompressed.
    static constexpr std::size_t kMaxMessageBytes = 1u << 20;
    static constexpr std::size_t kMaxControlBytes = 125;

    WebSocket() = default;
    WebSocket(const WebSocket &) = delete;
    WebSocket &operator=(const WebSocket &) = delete;
    ~WebSocket() = default;

    // The stream must already be connected and must outlive this object. On any
    // status other than ok the connection is not usable and error() says why.
    // timeout_ms of 0 still sends the request and has one look at the answer,
    // which is what a caller that cannot wait wants.
    WsStatus connect(Stream &stream, const WebSocketRequest &request, int timeout_ms);

    // One event. Timeout leaves the connection open and any half-read frame
    // inside the object; close means the peer said goodbye and is_open() is
    // false afterwards. A timeout_ms of 0 is a poll: it reads what has already
    // arrived without waiting, so a frame that is there is never missed.
    //
    // On a compressed connection one event can take several frames to arrive and
    // a frame can carry a piece of one event, so a call returns the next
    // decompressed message and nothing else; a message already inflated is
    // handed over before the stream is read at all.
    WsStatus receive(int timeout_ms, Message &out);

    WsStatus send_text(std::string_view text, int timeout_ms);
    WsStatus send_binary(const std::uint8_t *data, std::size_t size, int timeout_ms);
    WsStatus send_ping(std::string_view payload, int timeout_ms);

    // Sends the closing frame (code 0 means "no code", which goes out as 1000).
    // The peer's reply is not waited for here: the next receive() reports it.
    // Calling it again after it has been sent is a no-op, because a second close
    // frame would itself be a protocol error.
    WsStatus send_close(std::uint16_t code, std::string_view reason, int timeout_ms);

    bool is_open() const noexcept
    {
        return stream_ != nullptr && open_;
    }
    bool close_sent() const noexcept
    {
        return close_sent_;
    }

    // Human text for the last failure: the peer's close reason, whichever
    // protocol rule was broken, or a note that the stream failed (a concrete
    // stream keeps the detail of that, behind its own error()).
    const std::string &error() const noexcept
    {
        return error_;
    }

  private:
    // Waits until `count` bytes sit in inbound_, or the deadline passes. An
    // expired deadline still brings one look at the stream, so nothing that has
    // already arrived is left unread.
    WsStatus need(std::size_t count, std::int64_t deadline_ms);
    // Reads one frame and leaves it in `out` when it is an app-level event.
    // Pings are answered here and do not come back to the caller.
    WsStatus next_event(std::int64_t deadline_ms, Message &out);

    WsStatus send_frame(std::uint8_t opcode, const std::uint8_t *payload, std::size_t size,
                        int timeout_ms);
    // Both record error(); fail() also marks the connection dead, reject() leaves
    // it alone, for the mistakes that are the caller's rather than the peer's.
    WsStatus fail(WsStatus status, std::string text);
    WsStatus reject(WsStatus status, std::string text);

    Stream *stream_ = nullptr;
    ZlibStream inflater_;
    std::string inbound_; // bytes read and not yet consumed
    std::string message_; // the message being reassembled, if any
    std::string error_;
    std::uint8_t fragment_opcode_ = 0; // 0 while no fragmented message is open
    bool open_ = false;
    bool failed_ = false;
    bool close_sent_ = false;
    bool close_received_ = false;
    // Set from the request by connect(): from then on a binary frame is a piece
    // of the compressed stream instead of a message.
    bool zlib_stream_ = false;
};
} // namespace accord::core
