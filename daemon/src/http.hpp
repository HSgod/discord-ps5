/*
 * Accord - The little HTTP surface of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Only two endpoints matter to the experiment: GET /status reports the audio
 * counters, POST /quit stops the daemon without touching the console. Request
 * parsing and rendering are pure, so the host tests cover them.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace accord::daemon
{
enum class Method
{
    get,
    post,
    unknown,
};

struct RequestLine
{
    Method method = Method::unknown;
    std::string path;  // the target without its query string
    std::string query; // everything after the first '?'
};

// Parses one request line such as "GET /status HTTP/1.1". Anything that is not
// a GET or a POST comes back as Method::unknown.
RequestLine parse_request_line(std::string_view line);

// What GET /status reports.
struct Status
{
    std::uint64_t uptime_seconds = 0;
    std::uint64_t audio_frames = 0;
    bool audio_open = false;
    bool tone_playing = false;
    std::uint16_t port = 0;
    std::string_view audio_port;
    std::string_view last_error;      // empty when nothing has failed
    std::string_view unwind_selftest; // "ok" or "failed", see T6.0-3
};

std::string render_status_json(const Status &status);

struct Response
{
    int code = 200;
    std::string_view reason = "OK";
    std::string_view content_type = "text/plain; charset=utf-8";
    std::string body;
};

std::string render_response(const Response &response);
} // namespace accord::daemon
