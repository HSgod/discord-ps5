/*
 * Accord - one manual run against the real Discord (T3.4).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Three steps, all of them our own code: GET https://discord.com/api/v10/gateway
 * over the TLS client of T3.1 with certificate verification and the pinned roots,
 * then a WebSocket connection to the address that answer named, then read HELLO
 * (`op 10`) and its `heartbeat_interval` with our JSON parser.
 *
 *   build/gateway-hello                 the gateway's text frames
 *   build/gateway-hello --zlib-stream   `compress=zlib-stream`, so the events
 *                                       arrive inside a binary zlib stream
 *
 * It is not a host test and not in `make test`: it needs the network and Discord
 * being up, and a suite that passes or fails with someone else's server is not a
 * suite. Run it by hand; the report of T3.4 records what it printed.
 *
 * Nothing here authenticates: no token, no IDENTIFY, no heartbeat. The gateway
 * answers HELLO to any connection, and the connection is closed cleanly with a
 * close frame as soon as HELLO has been read.
 *
 * The HTTP client below is this tool's own, and deliberately so: what T3.4 asks
 * for is a diagnostic run, not an HTTP stack. When the daemon needs the same GET
 * for the console criterion of T3.1, that is the moment to lift it into core/ --
 * one caller is not a library.
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "core/json.hpp"
#include "core/random.hpp"
#include "core/stream.hpp"
#include "core/websocket.hpp"
#include "platform/net/ca_bundle.hpp"
#include "platform/net/tcp_stream.hpp"
#include "platform/net/tls_stream.hpp"

namespace
{
using accord::core::Message;
using accord::core::MessageKind;
using accord::core::StreamResult;
using accord::core::StreamStatus;
using accord::core::WebSocket;
using accord::core::WebSocketRequest;
using accord::core::json::Document;
using accord::net::TcpStream;
using accord::net::TlsStream;

constexpr std::string_view kApiHost = "discord.com";
constexpr std::string_view kGatewayPath = "/api/v10/gateway";
constexpr std::string_view kQuery = "/?v=10&encoding=json";
constexpr int kBudgetMs = 30000; // the whole run, waits included
constexpr std::uint16_t kPort = 443;

std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::int64_t left_ms(std::int64_t deadline)
{
    const std::int64_t left = deadline - now_ms();
    return left > 0 ? left : 0;
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

std::string lower_ascii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text)
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    return out;
}

// One header, by name, from the block of them the answer carried.
std::string header_value(std::string_view head, std::string_view name)
{
    const std::string wanted = lower_ascii(name) + ":";
    std::size_t at = 0;
    while (at < head.size())
    {
        const std::size_t end = head.find("\r\n", at);
        const std::string_view line = head.substr(at, end == std::string_view::npos ? end : end - at);
        const std::string folded = lower_ascii(line);
        if (folded.compare(0, wanted.size(), wanted) == 0)
        {
            std::string_view value{line.data() + wanted.size(), line.size() - wanted.size()};
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                value.remove_prefix(1);
            return std::string{value};
        }
        if (end == std::string_view::npos)
            break;
        at = end + 2;
    }
    return {};
}

// A chunked body, decoded. Discord's edge uses it on some answers, and a tool
// that printed the chunk sizes as if they were the document would be reporting
// its own bug rather than the gateway's.
bool decode_chunked(std::string_view body, std::string &out)
{
    std::size_t at = 0;
    while (at < body.size())
    {
        const std::size_t line_end = body.find("\r\n", at);
        if (line_end == std::string_view::npos)
            return false;
        const std::string_view header = body.substr(at, line_end - at);
        std::size_t size = 0;
        {
            std::size_t digits = header.find(';');
            if (digits == std::string_view::npos)
                digits = header.size();
            for (std::size_t i = 0; i < digits; ++i)
            {
                const char c = header[i];
                const int value = c >= '0' && c <= '9'   ? c - '0'
                                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                         : -1;
                if (value < 0)
                    return false;
                size = size * 16 + static_cast<std::size_t>(value);
            }
        }
        at = line_end + 2;
        if (size == 0)
            return true;
        if (at + size > body.size())
            return false;
        out.append(body.data() + at, size);
        at += size + 2; // and the CRLF after the chunk
    }
    return true;
}

// The status line, the headers and the body of one HTTP answer.
struct Answer
{
    int status = 0;
    std::string reason;
    std::string body;
    bool decode_ok = false;
};

Answer split_answer(std::string_view raw)
{
    Answer answer;
    const std::size_t first_line = raw.find("\r\n");
    if (first_line == std::string_view::npos)
        return answer;
    const std::string_view status = raw.substr(0, first_line);
    const std::size_t first_space = status.find(' ');
    if (first_space == std::string_view::npos)
        return answer;
    for (std::size_t at = first_space + 1; at < status.size() && status[at] != ' '; ++at)
        answer.status = answer.status * 10 + (status[at] - '0');
    const std::size_t second_space = status.find(' ', first_space + 1);
    answer.reason = second_space == std::string_view::npos
                        ? std::string{status.substr(first_space + 1)}
                        : std::string{status.substr(second_space + 1)};

    const std::size_t head_end = raw.find("\r\n\r\n");
    if (head_end == std::string_view::npos)
        return answer;
    const std::string_view head = raw.substr(0, head_end);
    const std::string_view body = raw.substr(head_end + 4);

    const std::string encoding = lower_ascii(header_value(head, "transfer-encoding"));
    if (encoding.find("chunked") != std::string::npos)
        answer.decode_ok = decode_chunked(body, answer.body);
    else
    {
        answer.body = std::string{body};
        answer.decode_ok = true;
    }
    return answer;
}

// Reads until the peer closes, which `Connection: close` asks it to do.
bool read_whole(TlsStream &tls, std::int64_t deadline, std::string &out)
{
    while (true)
    {
        if (left_ms(deadline) == 0)
        {
            std::fprintf(stderr, "the answer did not end inside the wait\n");
            return false;
        }
        std::uint8_t buffer[4096];
        const StreamResult got = tls.read(buffer, sizeof(buffer), static_cast<int>(left_ms(deadline)));
        if (got.status == StreamStatus::ok)
        {
            out.append(reinterpret_cast<const char *>(buffer), got.transferred);
            continue;
        }
        if (got.status == StreamStatus::closed)
            return true;
        if (got.status == StreamStatus::timeout)
            continue; // the deadline above is what ends this
        std::fprintf(stderr, "the answer broke off: %s\n", tls.error().c_str());
        return false;
    }
}

// Step one: the gateway address, over HTTPS, verified against the pinned roots.
bool fetch_gateway_address(std::int64_t deadline, std::string &url)
{
    std::printf("dialing %.*s:%u\n", static_cast<int>(kApiHost.size()), kApiHost.data(), kPort);

    TcpStream transport;
    const auto began = now_ms();
    if (!transport.connect(kApiHost, kPort, static_cast<int>(left_ms(deadline))))
    {
        std::fprintf(stderr, "connect: %s\n", transport.error().c_str());
        return false;
    }
    std::printf("tcp connected in %lld ms\n", static_cast<long long>(now_ms() - began));

    TlsStream tls;
    const auto handshake_began = now_ms();
    if (!tls.handshake(transport, kApiHost, accord::net::bundled_ca_pem(),
                       static_cast<int>(left_ms(deadline))))
    {
        std::fprintf(stderr, "tls: %s\n", tls.error().c_str());
        return false;
    }
    std::printf("tls %s, peer %s, handshake in %lld ms\n", tls.tls_version().c_str(),
                tls.peer_subject().c_str(),
                static_cast<long long>(now_ms() - handshake_began));

    std::string request{"GET "};
    request += kGatewayPath;
    request += " HTTP/1.1\r\nHost: ";
    request += kApiHost;
    request += "\r\nUser-Agent: accord-gateway-hello (T3.4)\r\nAccept: application/json\r\n"
               "Connection: close\r\n\r\n";

    const StreamResult written = tls.write(reinterpret_cast<const std::uint8_t *>(request.data()),
                                          request.size(), static_cast<int>(left_ms(deadline)));
    if (written.status != StreamStatus::ok || written.transferred != request.size())
    {
        std::fprintf(stderr, "the request did not go out: %s\n", tls.error().c_str());
        return false;
    }

    std::string raw;
    if (!read_whole(tls, deadline, raw))
        return false;

    const Answer answer = split_answer(raw);
    std::printf("HTTP %d %s (%zu bytes)\n", answer.status, answer.reason.c_str(), raw.size());
    if (!answer.decode_ok)
    {
        std::fprintf(stderr, "the answer body did not decode\n");
        return false;
    }
    if (answer.status != 200)
    {
        std::fprintf(stderr, "the gateway address answered %d, not 200\n", answer.status);
        return false;
    }
    std::printf("body: %s\n", answer.body.c_str());

    const Document document = Document::parse(answer.body);
    if (!document.ok())
    {
        std::fprintf(stderr, "the answer is not JSON: %s\n", document.error().c_str());
        return false;
    }
    const std::string_view address = document.root().member("url").text();
    if (address.empty())
    {
        std::fprintf(stderr, "the answer has no \"url\"\n");
        return false;
    }
    url = std::string{address};
    std::printf("gateway address: %s\n", url.c_str());
    return true;
}

// wss://host/path -> host and path, or nothing when it is not a wss address.
bool split_wss(std::string_view url, std::string &host, std::string &path)
{
    if (url.compare(0, 6, "wss://") != 0)
    {
        std::fprintf(stderr, "the gateway address is not wss://: %.*s\n", static_cast<int>(url.size()),
                     url.data());
        return false;
    }
    std::string_view rest = url.substr(6);
    const std::size_t slash = rest.find('/');
    host = std::string{rest.substr(0, slash)};
    path = slash == std::string_view::npos ? std::string{} : std::string{rest.substr(slash)};
    if (host.empty())
    {
        std::fprintf(stderr, "the gateway address has no host\n");
        return false;
    }
    return true;
}

// Steps two and three.
bool read_hello(std::string_view gateway_url, bool zlib_stream, std::int64_t deadline)
{
    std::string host;
    std::string path;
    if (!split_wss(gateway_url, host, path))
        return false;

    std::string target = path.empty() ? std::string{kQuery} : path + std::string{kQuery}.substr(1);
    if (zlib_stream)
        target += "&compress=zlib-stream";
    std::printf("dialing %s:%u\n", host.c_str(), kPort);

    TcpStream transport;
    if (!transport.connect(host, kPort, static_cast<int>(left_ms(deadline))))
    {
        std::fprintf(stderr, "connect: %s\n", transport.error().c_str());
        return false;
    }

    TlsStream tls;
    if (!tls.handshake(transport, host, accord::net::bundled_ca_pem(),
                       static_cast<int>(left_ms(deadline))))
    {
        std::fprintf(stderr, "tls: %s\n", tls.error().c_str());
        return false;
    }
    std::printf("tls %s, peer %s\n", tls.tls_version().c_str(), tls.peer_subject().c_str());

    WebSocketRequest request;
    request.host = host;
    request.target = target;
    request.zlib_stream = zlib_stream;
    if (!accord::core::fill_random(request.key.data(), request.key.size()))
    {
        std::fprintf(stderr, "no random bytes for the handshake key\n");
        return false;
    }

    WebSocket socket;
    const auto sent = socket.connect(tls, request, static_cast<int>(left_ms(deadline)));
    if (sent != accord::core::WsStatus::ok)
    {
        std::fprintf(stderr, "the websocket handshake failed: %s\n", socket.error().c_str());
        return false;
    }
    std::printf("websocket open, target %s\n", target.c_str());

    // HELLO is the first event on a fresh connection. Anything else that arrives
    // first is printed rather than skipped, because it would be news.
    bool hello = false;
    std::uint64_t interval = 0;
    while (!hello && left_ms(deadline) > 0)
    {
        Message message;
        const auto status = socket.receive(static_cast<int>(left_ms(deadline)), message);
        if (status == accord::core::WsStatus::timeout)
            continue;
        if (status != accord::core::WsStatus::ok)
        {
            std::fprintf(stderr, "receive: %s\n", socket.error().c_str());
            return false;
        }
        if (message.kind == MessageKind::close)
        {
            std::fprintf(stderr, "the gateway closed before HELLO: code %u\n", message.close_code);
            return false;
        }
        if (message.kind != MessageKind::text)
        {
            std::printf("event (%.*s frame, %zu bytes): %.200s\n", 0, "", message.payload.size(),
                        message.payload.c_str());
            continue;
        }

        std::printf("event: %s\n", message.payload.c_str());
        std::printf("frame kind: %s", name_of(message.frame_kind).c_str());
        if (message.frame_kind == MessageKind::binary)
            std::printf(" (a piece of the zlib stream, decompressed here)");
        std::printf("\n");

        const Document document = Document::parse(message.payload);
        if (!document.ok())
        {
            std::fprintf(stderr, "the event is not JSON: %s\n", document.error().c_str());
            return false;
        }
        std::uint64_t op = 0;
        if (!document.root().member("op").u64(&op))
        {
            std::fprintf(stderr, "the event has no numeric \"op\"\n");
            return false;
        }
        if (op != 10)
        {
            std::printf("op %llu, not HELLO -- printing it and carrying on\n",
                        static_cast<unsigned long long>(op));
            continue;
        }
        if (!document.root().member("d").member("heartbeat_interval").u64(&interval))
        {
            std::fprintf(stderr, "HELLO has no \"heartbeat_interval\"\n");
            return false;
        }
        hello = true;
        std::printf("op 10 (HELLO), heartbeat_interval %llu ms\n",
                    static_cast<unsigned long long>(interval));
    }

    if (!hello)
    {
        std::fprintf(stderr, "no HELLO inside the wait\n");
        return false;
    }

    // A clean goodbye: the close frame, then the peer's. Nothing was ever sent
    // that a token would have been needed for, and nothing goes out after this.
    if (socket.send_close(1000, "", static_cast<int>(left_ms(deadline))) !=
        accord::core::WsStatus::ok)
    {
        std::fprintf(stderr, "the close frame did not go out: %s\n", socket.error().c_str());
        return false;
    }
    Message reply;
    const auto closed = socket.receive(static_cast<int>(left_ms(deadline)), reply);
    if (closed == accord::core::WsStatus::ok && reply.kind == MessageKind::close)
        std::printf("closed: code %u, reason \"%s\"\n", reply.close_code, reply.close_reason.c_str());
    else
        std::printf("closed: the peer did not answer the close frame (status %s)\n",
                    socket.error().c_str());
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    bool zlib_stream = false;
    for (int at = 1; at < argc; ++at)
    {
        const std::string_view argument{argv[at]};
        if (argument == "--zlib-stream")
            zlib_stream = true;
        else
        {
            std::fprintf(stderr, "usage: %s [--zlib-stream]\n", argv[0]);
            return 2;
        }
    }

    const std::int64_t deadline = now_ms() + kBudgetMs;

    std::printf("== the gateway address (T3.1 TLS, pinned roots) ==\n");
    std::string gateway_url;
    if (!fetch_gateway_address(deadline, gateway_url))
        return 1;

    std::printf("\n== HELLO (T3.3 websocket%s) ==\n",
                zlib_stream ? ", compress=zlib-stream" : ", no compression");
    if (!read_hello(gateway_url, zlib_stream, deadline))
        return 1;

    std::printf("\n== done in %lld ms ==\n", static_cast<long long>(now_ms() + kBudgetMs - deadline));
    return 0;
}
