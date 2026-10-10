/*
 * Discord PS5 - The little HTTP surface of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "daemon/src/http.hpp"

#include <cstdio>

namespace discord_ps5::daemon
{
namespace
{
void append_json_string(std::string &out, std::string_view text)
{
    out.push_back('"');
    for (const char character : text)
    {
        switch (character)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20)
            {
                char escape[8];
                std::snprintf(escape, sizeof(escape), "\\u%04x",
                              static_cast<unsigned char>(character));
                out += escape;
            }
            else
            {
                out.push_back(character);
            }
            break;
        }
    }
    out.push_back('"');
}
} // namespace

RequestLine parse_request_line(std::string_view line)
{
    RequestLine request;

    const std::size_t after_method = line.find(' ');
    if (after_method == std::string_view::npos)
        return request;
    const std::size_t after_target = line.find(' ', after_method + 1);
    if (after_target == std::string_view::npos)
        return request;

    const std::string_view method = line.substr(0, after_method);
    if (method == "GET")
        request.method = Method::get;
    else if (method == "POST")
        request.method = Method::post;
    else
        return request;

    const std::string_view target =
        line.substr(after_method + 1, after_target - after_method - 1);

    const std::size_t question = target.find('?');
    if (question == std::string_view::npos)
    {
        request.path.assign(target);
    }
    else
    {
        request.path.assign(target.substr(0, question));
        request.query.assign(target.substr(question + 1));
    }
    return request;
}

std::string render_status_json(const Status &status)
{
    std::string out;
    out.reserve(192);

    out += "{\"uptime_seconds\":";
    out += std::to_string(status.uptime_seconds);
    out += ",\"audio_frames\":";
    out += std::to_string(status.audio_frames);
    out += ",\"audio_open\":";
    out += status.audio_open ? "true" : "false";
    out += ",\"tone_playing\":";
    out += status.tone_playing ? "true" : "false";
    out += ",\"port\":";
    out += std::to_string(status.port);
    out += ",\"audio_port\":";
    append_json_string(out, status.audio_port);
    out += ",\"last_error\":";
    append_json_string(out, status.last_error);
    out += "}\n";
    return out;
}

std::string render_response(const Response &response)
{
    std::string out;
    out.reserve(response.body.size() + 160);

    out += "HTTP/1.1 ";
    out += std::to_string(response.code);
    out += ' ';
    out += response.reason;
    out += "\r\nContent-Type: ";
    out += response.content_type;
    out += "\r\nContent-Length: ";
    out += std::to_string(response.body.size());
    out += "\r\nConnection: close\r\n\r\n";
    out += response.body;
    return out;
}
} // namespace discord_ps5::daemon
