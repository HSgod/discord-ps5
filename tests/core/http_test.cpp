/*
 * Accord - Tests for the HTTP/1.1 client of core/http.hpp.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The awkward answers are written down here rather than provoked over a socket:
 * a chunk size split between two reads, a body that stops in the middle, a peer
 * that sends more headers than the limit, a 429 that is not to be retried. The
 * socket path keeps its own, smaller set of tests, and the tool of T3.4 is what
 * checks the same client against the real Discord by hand.
 *
 * Every answer below is a literal, byte for byte, so what the client reads is
 * what a server would have put on the wire -- CRLFs and all.
 */

#include "tests/micro_test.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include "core/http.hpp"
#include "tests/support/memory_stream.hpp"

using accord::core::HttpClient;
using accord::core::HttpRequest;
using accord::core::HttpResponse;
using accord::core::HttpStatus;
using accord::test::MemoryStream;

namespace
{
// Long enough that a scripted answer is never cut short by the clock, and short
// enough that the one test which waits for a timeout does not hold the suite up.
constexpr int kWaitMs = 2000;
constexpr int kShortWaitMs = 40;

std::string name_of(HttpStatus status)
{
    switch (status)
    {
    case HttpStatus::ok:
        return "ok";
    case HttpStatus::timeout:
        return "timeout";
    case HttpStatus::closed:
        return "closed";
    case HttpStatus::bad_request:
        return "bad_request";
    case HttpStatus::bad_response:
        return "bad_response";
    case HttpStatus::too_large:
        return "too_large";
    case HttpStatus::transport_error:
        return "transport_error";
    }
    return "?";
}

HttpRequest gateway_request()
{
    return HttpRequest::get("discord.com", "/api/v10/gateway");
}

// A head and a body that both fit: the ordinary answer, and the one every other
// test here varies one detail of.
std::string answer_with(std::string_view headers)
{
    std::string out{"HTTP/1.1 200 OK\r\n"};
    out += headers;
    out += "\r\n";
    return out;
}
} // namespace

MICRO_TEST(a_body_with_a_content_length_is_read)
{
    MemoryStream stream;
    stream.feed(answer_with("Content-Length: 11\r\nContent-Type: application/json\r\n") +
                "hello world");

    HttpResponse answer;
    HttpClient http;
    const HttpStatus status = http.send(stream, gateway_request(), kWaitMs, answer);

    MICRO_CHECK_EQ(name_of(status), std::string{"ok"});
    MICRO_CHECK_EQ(answer.status, 200);
    MICRO_CHECK_EQ(answer.reason, std::string{"OK"});
    MICRO_CHECK_EQ(answer.body, std::string{"hello world"});

    // And the request that went out says who it is for, and that this
    // connection is spent by the one exchange.
    MICRO_CHECK_EQ(stream.written().find("GET /api/v10/gateway HTTP/1.1\r\n"), std::size_t{0});
    MICRO_CHECK(stream.written().find("Host: discord.com\r\n") != std::string::npos);
    MICRO_CHECK(stream.written().find("Connection: close\r\n") != std::string::npos);
}

MICRO_TEST(header_lookup_ignores_case)
{
    MemoryStream stream;
    stream.feed(answer_with("Content-Length: 2\r\nContent-TYPE: text/plain\r\n") + "hi");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.header("content-type"), std::string_view{"text/plain"});
    MICRO_CHECK_EQ(answer.header("CONTENT-TYPE"), std::string_view{"text/plain"});
    MICRO_CHECK_EQ(answer.header("content-length"), std::string_view{"2"});
    MICRO_CHECK_EQ(answer.header("transfer-encoding"), std::string_view{});
}

MICRO_TEST(a_chunked_body_is_read)
{
    MemoryStream stream;
    stream.feed(answer_with("Transfer-Encoding: chunked\r\n"));
    stream.feed("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.body, std::string{"hello world"});
}

MICRO_TEST(a_chunk_size_split_across_reads_is_read)
{
    MemoryStream stream;
    stream.feed(answer_with("Transfer-Encoding: chunked\r\n"));
    // The size of the one chunk is `10` in hex, and it arrives in two pieces --
    // the `1` and the `0` in separate reads, which is what a size that lands on
    // a read boundary looks like.
    stream.feed("1");
    stream.feed("0\r\n");
    stream.feed("0123456789abcdef\r\n");
    stream.feed("0\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.body, std::string{"0123456789abcdef"});
}

MICRO_TEST(chunk_extensions_and_trailers_are_dropped)
{
    MemoryStream stream;
    stream.feed(answer_with("Transfer-Encoding: chunked\r\n"));
    stream.feed("5;name=value\r\nhello\r\n0\r\nX-Trailer: 1\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.body, std::string{"hello"});
}

MICRO_TEST(a_body_that_ends_with_the_connection_is_read)
{
    MemoryStream stream;
    stream.feed(answer_with("Content-Type: text/plain\r\n")); // no length of any kind
    stream.feed("until the peer ");
    stream.feed("closes");
    stream.feed_closed();

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.body, std::string{"until the peer closes"});
}

MICRO_TEST(bytes_after_the_body_are_not_part_of_it)
{
    MemoryStream stream;
    // One byte more than the length promised: a client that read to the end of
    // the buffer instead of to the Content-Length would report it.
    stream.feed(answer_with("Content-Length: 5\r\n") + "helloEXTRA");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.body, std::string{"hello"});
}

MICRO_TEST(headers_over_the_limit_are_refused)
{
    std::string endless{"HTTP/1.1 200 OK\r\nX-Padding: "};
    endless.append(HttpClient::kMaxHeaderBytes, 'a');

    MemoryStream stream;
    stream.feed(endless);
    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)),
                   std::string{"too_large"});

    // And the same head with a blank line behind it, arriving in one read: the
    // end is there, but it is past the limit, which is just as much too large.
    MemoryStream finished_stream;
    finished_stream.feed(endless + "\r\n\r\n");
    HttpResponse finished;
    HttpClient finished_http;
    MICRO_CHECK_EQ(name_of(finished_http.send(finished_stream, gateway_request(), kWaitMs, finished)),
                   std::string{"too_large"});
}

MICRO_TEST(a_body_over_the_limit_is_refused)
{
    MemoryStream stream;
    stream.feed(answer_with("Content-Length: 33554432\r\n")); // 32 MiB

    HttpResponse answer;
    HttpClient http;
    // Refused before the first byte of the body was read: the rest is not in the
    // script, so a client that tried to read it would time out instead.
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)),
                   std::string{"too_large"});
}

MICRO_TEST(a_chunked_body_over_the_limit_is_refused)
{
    MemoryStream stream;
    stream.feed(answer_with("Transfer-Encoding: chunked\r\n"));
    stream.feed("2000000\r\n"); // 32 MiB, in one chunk

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)),
                   std::string{"too_large"});
}

MICRO_TEST(a_body_that_breaks_off_is_reported_as_closed)
{
    MemoryStream stream;
    stream.feed(answer_with("Content-Length: 100\r\n") + "only ten b");
    stream.feed_closed();

    HttpResponse answer;
    HttpClient http;
    const HttpStatus status = http.send(stream, gateway_request(), kWaitMs, answer);
    MICRO_CHECK_EQ(name_of(status), std::string{"closed"});
    MICRO_CHECK(!http.error().empty());
    MICRO_CHECK_EQ(answer.body, std::string{"only ten b"}); // what did arrive is kept
}

MICRO_TEST(a_429_comes_back_with_its_retry_after)
{
    MemoryStream stream;
    stream.feed("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 2\r\nContent-Length: 5\r\n\r\nslow!");

    HttpResponse answer;
    HttpClient http;
    const HttpStatus status = http.send(stream, gateway_request(), kWaitMs, answer);

    MICRO_CHECK_EQ(name_of(status), std::string{"ok"});
    MICRO_CHECK_EQ(answer.status, 429);
    MICRO_CHECK_EQ(answer.reason, std::string{"Too Many Requests"});
    MICRO_CHECK_EQ(answer.retry_after(), std::string_view{"2"});
    MICRO_CHECK_EQ(answer.body, std::string{"slow!"});

    // And it is the caller's decision: nothing was retried, so the one request
    // is the only thing on the wire.
    MICRO_CHECK_EQ(stream.written().find("GET ", 1), std::string::npos);
}

MICRO_TEST(a_redirect_is_an_answer_and_is_not_followed)
{
    MemoryStream stream;
    stream.feed("HTTP/1.1 301 Moved Permanently\r\nLocation: https://elsewhere.example/\r\n"
                "Content-Length: 0\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.status, 301);
    MICRO_CHECK_EQ(answer.header("location"), std::string_view{"https://elsewhere.example/"});
    MICRO_CHECK_EQ(answer.body, std::string{});

    // A hop would have shown up as a second request here, and as an answer the
    // caller never asked for.
    MICRO_CHECK_EQ(stream.written().find("GET ", 1), std::string::npos);
}

MICRO_TEST(a_204_has_no_body_whatever_its_headers_say)
{
    MemoryStream stream;
    // The headers claim five bytes that are never coming: reading them would
    // wait for the whole timeout, and 204 has no body by definition.
    stream.feed("HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)), std::string{"ok"});
    MICRO_CHECK_EQ(answer.status, 204);
    MICRO_CHECK_EQ(answer.body, std::string{});
}

MICRO_TEST(a_body_length_said_twice_is_refused)
{
    MemoryStream stream;
    stream.feed(answer_with("Content-Length: 5\r\nContent-Length: 6\r\n") + "hello");

    HttpResponse answer;
    HttpClient http;
    const HttpStatus status = http.send(stream, gateway_request(), kWaitMs, answer);
    MICRO_CHECK_EQ(name_of(status), std::string{"bad_response"});
    MICRO_CHECK(!http.error().empty());
}

MICRO_TEST(a_transfer_coding_this_client_cannot_undo_is_refused)
{
    MemoryStream stream;
    stream.feed(answer_with("Transfer-Encoding: gzip, chunked\r\n") + "5\r\nhello\r\n0\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    // Reading the chunks would hand back gzipped bytes as if they were the body.
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)),
                   std::string{"bad_response"});
}

MICRO_TEST(a_status_line_that_is_not_http_is_refused)
{
    MemoryStream stream;
    stream.feed("GATEWAY 200 OK\r\nContent-Length: 0\r\n\r\n");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)),
                   std::string{"bad_response"});
}

MICRO_TEST(an_answer_that_never_arrives_times_out)
{
    MemoryStream stream; // nothing scripted at all

    HttpResponse answer;
    HttpClient http;
    const HttpStatus status = http.send(stream, gateway_request(), kShortWaitMs, answer);
    MICRO_CHECK_EQ(name_of(status), std::string{"timeout"});
}

MICRO_TEST(a_request_with_no_host_is_refused)
{
    MemoryStream stream;

    HttpResponse answer;
    HttpClient http;
    const HttpRequest request = HttpRequest::get("", "/api/v10/gateway");
    MICRO_CHECK_EQ(name_of(http.send(stream, request, kWaitMs, answer)),
                   std::string{"bad_request"});
    MICRO_CHECK(stream.written().empty()); // and nothing went out
}

MICRO_TEST(a_write_that_fails_is_a_transport_error)
{
    MemoryStream stream;
    stream.set_fail_writes(true);
    stream.feed(answer_with("Content-Length: 2\r\n") + "hi");

    HttpResponse answer;
    HttpClient http;
    MICRO_CHECK_EQ(name_of(http.send(stream, gateway_request(), kWaitMs, answer)),
                   std::string{"transport_error"});
}

MICRO_TEST(render_request_writes_a_get_and_a_post)
{
    const HttpRequest get = HttpRequest::get("discord.com", "/api/v10/gateway");
    MICRO_CHECK_EQ(accord::core::render_request(get),
                   std::string{"GET /api/v10/gateway HTTP/1.1\r\n"
                               "Host: discord.com\r\n"
                               "Connection: close\r\n"
                               "\r\n"});

    const std::string body = "grant_type=authorization_code";
    const HttpRequest post = HttpRequest::post("discord.com", "/api/v10/oauth2/token", body,
                                               "application/x-www-form-urlencoded");
    MICRO_CHECK_EQ(accord::core::render_request(post),
                   std::string{"POST /api/v10/oauth2/token HTTP/1.1\r\n"
                               "Host: discord.com\r\n"
                               "Connection: close\r\n"
                               "Content-Type: application/x-www-form-urlencoded\r\n"
                               "Content-Length: " +
                               std::to_string(body.size()) + "\r\n\r\n" + body});

    // A POST with nothing to say still says how long nothing is; a GET does not
    // mention a body it never has.
    const HttpRequest empty_post = HttpRequest::post("discord.com", "/x", "", "");
    MICRO_CHECK(accord::core::render_request(empty_post).find("Content-Length: 0\r\n") !=
                std::string::npos);
    MICRO_CHECK(accord::core::render_request(empty_post).find("Content-Type") == std::string::npos);
}
