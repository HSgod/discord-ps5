/*
 * Accord - WebSocket client (RFC 6455) over an abstract byte stream.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/websocket.hpp"

#include <chrono>

#include "core/base64.hpp"
#include "core/random.hpp"
#include "core/sha1.hpp"

namespace accord::core
{
namespace
{
constexpr std::string_view kAcceptGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr std::size_t kReadChunk = 4096;
constexpr std::size_t kMaxHeadBytes = 16 * 1024;
constexpr int kReplyTimeoutMs = 1000; // for the close frames this file sends itself

constexpr std::uint8_t kOpContinuation = 0x0;
constexpr std::uint8_t kOpText = 0x1;
constexpr std::uint8_t kOpBinary = 0x2;
constexpr std::uint8_t kOpClose = 0x8;
constexpr std::uint8_t kOpPing = 0x9;
constexpr std::uint8_t kOpPong = 0xa;

std::int64_t now_ms() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// How long a stream call may wait. Zero is not "do not read": it is one look at
// what has already arrived, which is what a caller polling with receive(0) asks
// for, and what keeps a frame that is already in the buffer from being missed.
int remaining_ms(std::int64_t deadline_ms) noexcept
{
    const std::int64_t left = deadline_ms - now_ms();
    if (left <= 0)
        return 0;
    return left > 86'400'000 ? 86'400'000 : static_cast<int>(left);
}

char lower_ascii(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

bool equals_ignoring_case(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower_ascii(a[i]) != lower_ascii(b[i]))
            return false;
    return true;
}

// Header values such as "keep-alive, Upgrade" are comma-separated token lists.
bool has_token(std::string_view value, std::string_view token) noexcept
{
    std::size_t at = 0;
    while (true)
    {
        const std::size_t comma = value.find(',', at);
        const std::size_t end = comma == std::string_view::npos ? value.size() : comma;
        if (equals_ignoring_case(trim(value.substr(at, end - at)), token))
            return true;
        if (comma == std::string_view::npos)
            return false;
        at = comma + 1;
    }
}

// Finds one header in a response head; names are case-insensitive.
bool header_value(std::string_view head, std::string_view name, std::string_view *out) noexcept
{
    std::size_t at = head.find("\r\n"); // skip the status line
    if (at == std::string_view::npos)
        return false;
    at += 2;

    while (at < head.size())
    {
        const std::size_t end = head.find("\r\n", at);
        if (end == std::string_view::npos || end == at)
            return false; // the blank line ends the headers
        const std::string_view line = head.substr(at, end - at);
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && equals_ignoring_case(line.substr(0, colon), name))
        {
            *out = trim(line.substr(colon + 1));
            return true;
        }
        at = end + 2;
    }
    return false;
}

// A text frame must hold UTF-8 (RFC 6455 5.6 and 8.1). Overlong forms and
// surrogates are rejected because the spec's test suites reject them too.
bool is_valid_utf8(std::string_view text) noexcept
{
    std::size_t at = 0;
    while (at < text.size())
    {
        const std::uint8_t byte = static_cast<std::uint8_t>(text[at]);
        if (byte < 0x80)
        {
            ++at;
            continue;
        }

        std::size_t extra = 0;
        std::uint32_t code = 0;
        if ((byte & 0xe0) == 0xc0)
        {
            extra = 1;
            code = byte & 0x1f;
        }
        else if ((byte & 0xf0) == 0xe0)
        {
            extra = 2;
            code = byte & 0x0f;
        }
        else if ((byte & 0xf8) == 0xf0)
        {
            extra = 3;
            code = byte & 0x07;
        }
        else
        {
            return false;
        }

        if (at + extra >= text.size())
            return false;
        for (std::size_t i = 1; i <= extra; ++i)
        {
            const std::uint8_t next = static_cast<std::uint8_t>(text[at + i]);
            if ((next & 0xc0) != 0x80)
                return false;
            code = (code << 6) | (next & 0x3f);
        }

        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
            (extra == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff))
            return false;

        at += extra + 1;
    }
    return true;
}

// RFC 6455 7.4.1: the codes a peer may put on the wire. 1004, 1005, 1006 and
// 1015 are reserved and never sent; 1012-1014 were registered later and are
// accepted, because a server that sends "service restart" is not misbehaving.
bool is_valid_close_code(std::uint16_t code) noexcept
{
    if (code < 1000)
        return false;
    if (code == 1004 || code == 1005 || code == 1006 || code == 1015)
        return false;
    if (code > 1015 && code < 3000)
        return false;
    if (code >= 5000)
        return false;
    return true;
}
} // namespace

std::string sec_websocket_key(const std::array<std::uint8_t, 16> &key)
{
    return base64_encode(key.data(), key.size());
}

std::string sec_websocket_accept(std::string_view key)
{
    Sha1 digest;
    digest.update(key);
    digest.update(kAcceptGuid);
    std::array<std::uint8_t, 20> sum{};
    digest.finish(sum.data());
    return base64_encode(sum.data(), sum.size());
}

std::string render_handshake(const WebSocketRequest &request)
{
    const std::string target = request.target.empty() ? std::string{"/"} : request.target;

    std::string out;
    out.reserve(160);
    out += "GET " + target + " HTTP/1.1\r\n";
    out += "Host: " + request.host + "\r\n";
    out += "Upgrade: websocket\r\n";
    out += "Connection: Upgrade\r\n";
    out += "Sec-WebSocket-Key: " + sec_websocket_key(request.key) + "\r\n";
    out += "Sec-WebSocket-Version: 13\r\n";
    out += "\r\n";
    return out;
}

WsStatus WebSocket::fail(WsStatus status, std::string text)
{
    // RFC 6455 7.1.7: failing the connection because the peer broke the protocol
    // means sending the close frame -- 1002 -- before dropping it. close_sent_
    // goes up first, so a failure inside send_frame() cannot come back here.
    const bool send_close = status == WsStatus::protocol_error && open_ && !close_sent_ && !failed_;
    error_ = text;
    open_ = false;
    failed_ = true;
    if (send_close)
    {
        close_sent_ = true;
        const std::uint8_t payload[2] = {static_cast<std::uint8_t>(1002 >> 8),
                                         static_cast<std::uint8_t>(1002 & 0xff)};
        send_frame(kOpClose, payload, sizeof(payload), kReplyTimeoutMs);
        error_ = std::move(text); // the reason stays the protocol error, not a write failure
        failed_ = true;
    }
    return status;
}

WsStatus WebSocket::reject(WsStatus status, std::string text)
{
    error_ = std::move(text);
    return status;
}

WsStatus WebSocket::connect(Stream &stream, const WebSocketRequest &request, int timeout_ms)
{
    stream_ = &stream;
    inbound_.clear();
    message_.clear();
    error_.clear();
    fragment_opcode_ = 0;
    open_ = false;
    failed_ = false;
    close_sent_ = false;
    close_received_ = false;

    const std::string handshake = render_handshake(request);
    const StreamResult written = stream.write(
        reinterpret_cast<const std::uint8_t *>(handshake.data()), handshake.size(), timeout_ms);
    if (written.status != StreamStatus::ok)
        return fail(WsStatus::transport_error, "the handshake request could not be sent");

    const std::int64_t deadline_ms = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    std::size_t head_end = std::string::npos;
    while (head_end == std::string::npos)
    {
        head_end = inbound_.find("\r\n\r\n");
        if (head_end != std::string::npos)
            break;
        if (inbound_.size() > kMaxHeadBytes)
            return fail(WsStatus::protocol_error, "the handshake response head is too long");

        const int wait = remaining_ms(deadline_ms);
        std::uint8_t chunk[kReadChunk];
        const StreamResult result = stream.read(chunk, sizeof(chunk), wait);
        if (result.status == StreamStatus::timeout)
        {
            // With time left this is an ordinary loop. With none it was the one
            // look the expired deadline is still owed, and the answer is now the
            // timeout the caller asked for.
            if (wait > 0)
                continue;
            return fail(WsStatus::timeout, "no handshake response inside the wait");
        }
        if (result.status == StreamStatus::closed)
            return fail(WsStatus::closed, "the server closed the connection during the handshake");
        if (result.status == StreamStatus::failed || result.transferred == 0)
            return fail(WsStatus::transport_error, "the stream failed during the handshake");
        inbound_.append(reinterpret_cast<const char *>(chunk), result.transferred);
    }

    const std::string_view head{inbound_.data(), head_end + 4};
    const std::size_t line_end = head.find("\r\n");
    const std::string_view status_line = head.substr(0, line_end);

    // "HTTP/1.1 101 Switching Protocols": the version digits do not matter, the
    // code does.
    if (!status_line.starts_with("HTTP/1.") || status_line.size() < 12)
        return fail(WsStatus::protocol_error, "the server did not answer with an HTTP status line");
    const std::string_view code = status_line.substr(9, 3);
    if (code != "101")
        return fail(WsStatus::protocol_error,
                    "the server answered " + std::string{code} + " instead of 101");

    std::string_view value;
    if (!header_value(head, "Upgrade", &value) || !has_token(value, "websocket"))
        return fail(WsStatus::protocol_error, "the response does not upgrade to websocket");
    if (!header_value(head, "Connection", &value) || !has_token(value, "upgrade"))
        return fail(WsStatus::protocol_error, "the response does not say Connection: Upgrade");
    if (!header_value(head, "Sec-WebSocket-Accept", &value))
        return fail(WsStatus::protocol_error, "the response has no Sec-WebSocket-Accept");
    if (std::string{value} != sec_websocket_accept(sec_websocket_key(request.key)))
        return fail(WsStatus::protocol_error, "the response accept key does not match the nonce");
    // Nothing was asked for, so anything offered is an answer to a question that
    // was never asked, and would change how frames are read.
    if (header_value(head, "Sec-WebSocket-Extensions", &value) && !value.empty())
        return fail(WsStatus::protocol_error, "the server chose an extension that was not offered");
    if (header_value(head, "Sec-WebSocket-Protocol", &value) && !value.empty())
        return fail(WsStatus::protocol_error, "the server chose a subprotocol that was not offered");

    // Whatever follows the head is already frame data; Discord often sends the
    // first one right behind the upgrade.
    inbound_.erase(0, head_end + 4);
    open_ = true;
    return WsStatus::ok;
}

WsStatus WebSocket::need(std::size_t count, std::int64_t deadline_ms)
{
    while (inbound_.size() < count)
    {
        // A zero wait is one look at the stream without waiting, so a frame that
        // is already there is found instead of being declared absent.
        const int wait = remaining_ms(deadline_ms);

        std::uint8_t chunk[kReadChunk];
        const StreamResult result = stream_->read(chunk, sizeof(chunk), wait);
        if (result.status == StreamStatus::timeout)
        {
            // Nothing there right now: with time left the loop waits again, with
            // none the caller gets the timeout it asked for. Either way the bytes
            // already in inbound_ stay, so the half of a frame is never lost.
            if (wait > 0)
                continue;
            return WsStatus::timeout;
        }
        if (result.status == StreamStatus::closed)
        {
            if (!inbound_.empty() || !message_.empty())
                return fail(WsStatus::closed, "the connection ended in the middle of a frame");
            return fail(WsStatus::closed, "the peer closed the connection");
        }
        if (result.status == StreamStatus::failed || result.transferred == 0)
            return fail(WsStatus::transport_error, "the stream failed");
        inbound_.append(reinterpret_cast<const char *>(chunk), result.transferred);
    }
    return WsStatus::ok;
}

WsStatus WebSocket::next_event(std::int64_t deadline_ms, Message &out)
{
    while (true)
    {
        WsStatus status = need(2, deadline_ms);
        if (status != WsStatus::ok)
            return status;

        const std::uint8_t first = static_cast<std::uint8_t>(inbound_[0]);
        const std::uint8_t second = static_cast<std::uint8_t>(inbound_[1]);
        const bool fin = (first & 0x80) != 0;
        const std::uint8_t opcode = static_cast<std::uint8_t>(first & 0x0f);

        if ((first & 0x70) != 0)
            return fail(WsStatus::protocol_error,
                        "a reserved bit is set and no extension was negotiated");
        // RFC 6455 5.1: the server must not mask, the client must not see it.
        if ((second & 0x80) != 0)
            return fail(WsStatus::protocol_error, "a frame from the server came masked");

        std::uint64_t length = second & 0x7f;
        std::size_t head_size = 2;
        if (length == 126)
        {
            status = need(4, deadline_ms);
            if (status != WsStatus::ok)
                return status;
            length = (static_cast<std::uint64_t>(static_cast<std::uint8_t>(inbound_[2])) << 8) |
                     static_cast<std::uint8_t>(inbound_[3]);
            if (length < 126)
                return fail(WsStatus::protocol_error,
                            "the frame length is not in its shortest form");
            head_size = 4;
        }
        else if (length == 127)
        {
            status = need(10, deadline_ms);
            if (status != WsStatus::ok)
                return status;
            length = 0;
            for (int i = 2; i < 10; ++i)
                length = (length << 8) | static_cast<std::uint8_t>(inbound_[i]);
            if (length >= (1ull << 63))
                return fail(WsStatus::protocol_error, "the frame length has its top bit set");
            if (length < 65536)
                return fail(WsStatus::protocol_error,
                            "the frame length is not in its shortest form");
            head_size = 10;
        }

        const bool control = (opcode & 0x8) != 0;
        if (opcode != kOpContinuation && opcode != kOpText && opcode != kOpBinary &&
            opcode != kOpClose && opcode != kOpPing && opcode != kOpPong)
            return fail(WsStatus::protocol_error, "the frame has an unknown opcode");

        if (control)
        {
            if (!fin)
                return fail(WsStatus::protocol_error, "a control frame must not be fragmented");
            if (length > kMaxControlBytes)
                return fail(WsStatus::protocol_error,
                            "a control frame may not carry more than 125 bytes");
        }
        else
        {
            if (opcode == kOpContinuation && fragment_opcode_ == 0)
                return fail(WsStatus::protocol_error,
                            "a continuation frame arrived with no message open");
            if (opcode != kOpContinuation && fragment_opcode_ != 0)
                return fail(WsStatus::protocol_error,
                            "a new message started before the fragmented one finished");
            if (length > kMaxMessageBytes || message_.size() > kMaxMessageBytes - length)
            {
                send_close(1009, "message too big", kReplyTimeoutMs);
                return fail(WsStatus::too_large, "the message is over the size limit");
            }
        }

        status = need(head_size + static_cast<std::size_t>(length), deadline_ms);
        if (status != WsStatus::ok)
            return status; // a timeout keeps the bytes of the half-read frame

        std::string payload{inbound_.data() + head_size, static_cast<std::size_t>(length)};
        inbound_.erase(0, head_size + static_cast<std::size_t>(length));

        if (opcode == kOpPing)
        {
            // A ping may arrive between the fragments of a message, and RFC 6455
            // asks for the pong as soon as convenient. It is not an event for
            // the caller, so the loop goes round again.
            const WsStatus answered = send_frame(kOpPong,
                                                 reinterpret_cast<const std::uint8_t *>(
                                                     payload.data()),
                                                 payload.size(), kReplyTimeoutMs);
            if (answered != WsStatus::ok)
                return answered;
            continue;
        }

        if (opcode == kOpPong)
        {
            out.kind = MessageKind::pong;
            out.payload = std::move(payload);
            return WsStatus::ok;
        }

        if (opcode == kOpClose)
        {
            std::uint16_t code = 0;
            if (payload.size() == 1)
                return fail(WsStatus::protocol_error, "a close frame carried a single byte");
            if (payload.size() >= 2)
            {
                code = static_cast<std::uint16_t>(
                    (static_cast<std::uint8_t>(payload[0]) << 8) |
                    static_cast<std::uint8_t>(payload[1]));
                if (!is_valid_close_code(code))
                    return fail(WsStatus::protocol_error,
                                "the close frame carried an invalid code");
                const std::string_view reason{payload.data() + 2, payload.size() - 2};
                if (!is_valid_utf8(reason))
                    return fail(WsStatus::protocol_error, "the close reason is not UTF-8");
            }

            close_received_ = true;
            open_ = false;
            out.kind = MessageKind::close;
            out.close_code = code;
            out.close_reason = payload.size() >= 2 ? payload.substr(2) : std::string{};

            // Answering the close is what makes the closing handshake; a failure
            // to send it does not change the fact that the peer has gone.
            if (!close_sent_)
                send_close(code == 0 ? 1000 : code, "", kReplyTimeoutMs);
            return WsStatus::ok;
        }

        if (opcode == kOpContinuation)
        {
            message_.append(payload);
            if (!fin)
                continue;
            const std::uint8_t started_as = fragment_opcode_;
            fragment_opcode_ = 0;
            if (started_as == kOpText && !is_valid_utf8(message_))
                return fail(WsStatus::protocol_error, "a text message is not UTF-8");
            out.kind = started_as == kOpText ? MessageKind::text : MessageKind::binary;
            out.payload = std::move(message_);
            message_.clear();
            return WsStatus::ok;
        }

        if (!fin)
        {
            fragment_opcode_ = opcode;
            message_ = std::move(payload);
            continue;
        }

        if (opcode == kOpText && !is_valid_utf8(payload))
            return fail(WsStatus::protocol_error, "a text message is not UTF-8");
        out.kind = opcode == kOpText ? MessageKind::text : MessageKind::binary;
        out.payload = std::move(payload);
        return WsStatus::ok;
    }
}

WsStatus WebSocket::receive(int timeout_ms, Message &out)
{
    out = Message{};
    if (stream_ == nullptr)
        return fail(WsStatus::closed, "not connected");
    if (failed_ || close_received_)
        return WsStatus::closed;

    const std::int64_t deadline_ms = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    return next_event(deadline_ms, out);
}

WsStatus WebSocket::send_frame(std::uint8_t opcode, const std::uint8_t *payload, std::size_t size,
                               int timeout_ms)
{
    std::uint8_t mask[4] = {};
    if (!fill_random(mask, sizeof(mask)))
        return fail(WsStatus::transport_error, "no random bytes for the frame mask");

    std::string frame;
    frame.reserve(size + 14);
    frame.push_back(static_cast<char>(0x80 | opcode)); // nothing here fragments on the way out
    if (size < 126)
    {
        frame.push_back(static_cast<char>(0x80 | size));
    }
    else if (size <= 0xffff)
    {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>((size >> 8) & 0xff));
        frame.push_back(static_cast<char>(size & 0xff));
    }
    else
    {
        frame.push_back(static_cast<char>(0x80 | 127));
        const std::uint64_t wide = size;
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<char>((wide >> shift) & 0xff));
    }
    frame.append(reinterpret_cast<const char *>(mask), sizeof(mask));

    const std::size_t head = frame.size();
    frame.resize(head + size);
    for (std::size_t i = 0; i < size; ++i)
        frame[head + i] = static_cast<char>(payload[i] ^ mask[i % 4]);

    const StreamResult result = stream_->write(
        reinterpret_cast<const std::uint8_t *>(frame.data()), frame.size(), timeout_ms);
    if (result.status != StreamStatus::ok)
        return fail(WsStatus::transport_error, "the frame could not be written");
    return WsStatus::ok;
}

WsStatus WebSocket::send_text(std::string_view text, int timeout_ms)
{
    if (!is_open())
        return fail(WsStatus::closed, "the connection is not open");
    // RFC 6455 8.1: a client must not put text on the wire that is not UTF-8.
    if (!is_valid_utf8(text))
        return reject(WsStatus::protocol_error, "refusing to send text that is not UTF-8");
    return send_frame(kOpText, reinterpret_cast<const std::uint8_t *>(text.data()), text.size(),
                      timeout_ms);
}

WsStatus WebSocket::send_binary(const std::uint8_t *data, std::size_t size, int timeout_ms)
{
    if (!is_open())
        return fail(WsStatus::closed, "the connection is not open");
    if (size > kMaxMessageBytes)
        return reject(WsStatus::too_large, "refusing to send a message over the size limit");
    return send_frame(kOpBinary, data, size, timeout_ms);
}

WsStatus WebSocket::send_ping(std::string_view payload, int timeout_ms)
{
    if (!is_open())
        return fail(WsStatus::closed, "the connection is not open");
    if (payload.size() > kMaxControlBytes)
        return reject(WsStatus::too_large, "a control frame may not carry more than 125 bytes");
    return send_frame(kOpPing, reinterpret_cast<const std::uint8_t *>(payload.data()),
                      payload.size(), timeout_ms);
}

WsStatus WebSocket::send_close(std::uint16_t code, std::string_view reason, int timeout_ms)
{
    if (stream_ == nullptr)
        return fail(WsStatus::closed, "not connected");
    if (close_sent_)
        return WsStatus::ok; // one close frame is all RFC 6455 allows

    std::string payload;
    if (code != 0)
    {
        if (!is_valid_close_code(code))
            return reject(WsStatus::protocol_error, "refusing to send an invalid close code");
        if (reason.size() > kMaxControlBytes - 2)
            return reject(WsStatus::too_large, "the close reason does not fit in a control frame");
        if (!is_valid_utf8(reason))
            return reject(WsStatus::protocol_error, "refusing to send a close reason that is not UTF-8");
        payload.push_back(static_cast<char>((code >> 8) & 0xff));
        payload.push_back(static_cast<char>(code & 0xff));
        payload.append(reason);
    }

    close_sent_ = true;
    open_ = false;
    return send_frame(kOpClose, reinterpret_cast<const std::uint8_t *>(payload.data()),
                      payload.size(), timeout_ms);
}
} // namespace accord::core
