/*
 * Discord PS5 - Tests for the daemon's HTTP surface.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include <string>

#include "daemon/src/http.hpp"

using namespace discord_ps5::daemon;

MICRO_TEST(request_line_parses_the_method_and_the_path)
{
    const RequestLine get = parse_request_line("GET /status HTTP/1.1");
    MICRO_CHECK(get.method == Method::get);
    MICRO_CHECK_EQ(get.path, std::string{"/status"});
    MICRO_CHECK(get.query.empty());

    const RequestLine post = parse_request_line("POST /quit HTTP/1.1");
    MICRO_CHECK(post.method == Method::post);
    MICRO_CHECK_EQ(post.path, std::string{"/quit"});
}

MICRO_TEST(request_line_keeps_the_query_apart_from_the_path)
{
    const RequestLine request = parse_request_line("GET /status?verbose=1 HTTP/1.1");
    MICRO_CHECK(request.method == Method::get);
    MICRO_CHECK_EQ(request.path, std::string{"/status"});
    MICRO_CHECK_EQ(request.query, std::string{"verbose=1"});
}

MICRO_TEST(request_line_rejects_anything_else)
{
    MICRO_CHECK(parse_request_line("").method == Method::unknown);
    MICRO_CHECK(parse_request_line("GET").method == Method::unknown);
    MICRO_CHECK(parse_request_line("DELETE /status HTTP/1.1").method == Method::unknown);
}

MICRO_TEST(status_json_lists_every_counter)
{
    Status status;
    status.uptime_seconds = 12;
    status.audio_frames = 3456;
    status.audio_open = true;
    status.tone_playing = false;
    status.port = 8280;
    status.audio_port = "voice";
    status.last_error = "";

    MICRO_CHECK_EQ(render_status_json(status),
                   std::string{"{\"uptime_seconds\":12,\"audio_frames\":3456,\"audio_open\":true,"
                               "\"tone_playing\":false,\"port\":8280,\"audio_port\":\"voice\","
                               "\"last_error\":\"\"}\n"});
}

MICRO_TEST(status_json_escapes_the_error_text)
{
    Status status;
    status.last_error = "bad \"port\"\n";

    const std::string json = render_status_json(status);
    MICRO_CHECK(json.find("\"bad \\\"port\\\"\\n\"") != std::string::npos);
}

MICRO_TEST(response_carries_a_length_and_a_blank_line_before_the_body)
{
    Response response;
    response.body = "hello\n";

    const std::string text = render_response(response);
    MICRO_CHECK(text.starts_with("HTTP/1.1 200 OK\r\n"));
    MICRO_CHECK(text.find("Content-Length: 6\r\n") != std::string::npos);
    MICRO_CHECK(text.ends_with("\r\n\r\nhello\n"));
}

MICRO_TEST(response_can_carry_an_error_status)
{
    Response response;
    response.code = 404;
    response.reason = "Not Found";
    response.body = "not found\n";

    MICRO_CHECK(render_response(response).starts_with("HTTP/1.1 404 Not Found\r\n"));
}
