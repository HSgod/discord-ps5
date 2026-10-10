/*
 * Accord - one HTTP/1.1 request and one answer, over an abstract byte stream.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * T3.4's tool carried its own reader because one caller is not a library. The
 * daemon is the second caller -- it talks REST for login and for everything after
 * it -- so the reader moves here, next to the WebSocket client and above the same
 * seam: the stream comes in as core::Stream, so the host tests drive it over a
 * scripted stream and the TLS client of T3.1 drops underneath it with nothing in
 * this file changing. Nothing here knows what a socket, a console or OpenSSL is.
 *
 * The shape follows the rest of core/: no exceptions, no RTTI, and errors are
 * values. A timeout is a status of its own rather than a failure, because a
 * caller waiting on a server is doing exactly that.
 *
 * What it does: GET and POST with headers and a body, and the three ways an
 * HTTP/1.1 answer says how long it is -- Content-Length, `chunked`, and the body
 * that ends when the connection does. The two limits below are why it can be
 * handed a socket at all: a peer that claims a gigabyte of headers, or of body,
 * is cut off instead of believed.
 *
 * What it deliberately does not do:
 *   - follow redirects. A 3xx comes back as the answer it is, status and Location
 *     and all, and the decision belongs to the caller. Quietly walking to another
 *     host is how a client ends up sending its token somewhere it never meant to.
 *   - retry. A 429 comes back as a 429, with the `Retry-After` the server sent
 *     (retry_after()), and the caller decides. A client that sleeps by itself is
 *     a client whose caller cannot choose.
 *   - keep-alive. Every request asks for `Connection: close`, and a third of the
 *     answers end only when the peer hangs up, so the connection is spent by one
 *     call: nothing is kept between them, and the next request wants a new stream.
 *   - transfer codings other than `chunked`, and HTTP/2.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/stream.hpp"

namespace accord::core
{
struct Header
{
    std::string name;
    std::string value;
};

// One request, as the caller describes it. The factories fill in the ordinary
// shape; `headers` is where a caller puts what is its own business (a
// User-Agent, an Authorization, an Accept).
struct HttpRequest
{
    std::string method = "GET";
    std::string host;      // the Host header, without a port
    std::string target = "/";
    std::string body;
    std::vector<Header> headers; // sent as given, after the ones rendered below

    // A GET carries no body and, on the wire, no Content-Length.
    static HttpRequest get(std::string host, std::string target);

    // A POST always says how long its body is, an empty one included. An empty
    // `content_type` adds no Content-Type header.
    static HttpRequest post(std::string host, std::string target, std::string body,
                            std::string content_type);
};

// The request exactly as it goes on the wire: request line, Host, `Connection:
// close`, the caller's headers, the body's headers when there is a body, the
// blank line, the body.
std::string render_request(const HttpRequest &request);

struct HttpResponse
{
    int status = 0;
    std::string reason;  // the reason phrase as received; it is text, not a rule
    std::vector<Header> headers;
    std::string body;

    // The first value of `name`, or nothing. Header names are case-insensitive,
    // which is why callers ask here instead of walking the vector themselves.
    std::string_view header(std::string_view name) const noexcept;

    // What a 429 said about trying again, verbatim: a number of seconds, or an
    // HTTP date. Nothing is parsed and nothing is waited for here.
    std::string_view retry_after() const noexcept;
};

enum class HttpStatus
{
    ok,              // a whole answer sits in `out`
    timeout,         // nothing arrived inside the wait the caller asked for
    closed,          // the peer ended the stream before the answer was complete
    bad_request,     // the request this client was handed is not one it can send
    bad_response,    // the bytes are not an HTTP/1.1 answer this client can read
    too_large,       // a header block or a body passed its limit
    transport_error, // the stream failed, or swallowed a short write
};

class HttpClient
{
  public:
    // How far the head may run, the blank line included. A peer that sends more
    // headers than this is not answering the question that was asked.
    static constexpr std::size_t kMaxHeaderBytes = 16u << 10;
    // And how large a body may be before it is refused. Discord's REST answers
    // are kilobytes; this is where a peer that claims a gigabyte is cut off
    // instead of believed, and it is checked before the first body byte is read.
    static constexpr std::size_t kMaxBodyBytes = 16u << 20;

    HttpClient() = default;
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;
    ~HttpClient() = default;

    // Sends one request and reads one answer. The stream must already be
    // connected and must outlive the call; `timeout_ms` is the budget for the
    // whole exchange and is spent by the write as well, so a slow connect is not
    // paid for twice. On any status other than ok the connection is not usable
    // and error() says why; on ok it has been spent anyway (see the header).
    HttpStatus send(Stream &stream, const HttpRequest &request, int timeout_ms, HttpResponse &out);

    // Human text for the last failure: which rule of the answer was broken, or a
    // note that the stream failed (a concrete stream keeps that detail itself,
    // behind its own error()).
    const std::string &error() const noexcept
    {
        return error_;
    }

  private:
    HttpStatus fail(HttpStatus status, std::string text);

    std::string error_;
};
} // namespace accord::core
