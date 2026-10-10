/*
 * Accord - HTTP/1.1 client over an abstract byte stream.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/http.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace accord::core
{
namespace
{
constexpr std::size_t kReadChunk = 4096;

std::int64_t now_ms() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// How long a stream call may wait. Zero is not "do not read": it is one look at
// what has already arrived, so a caller that cannot wait still never misses the
// bytes that are there.
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

bool equals_ignoring_case(std::string_view a, std::string_view b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower_ascii(a[i]) != lower_ascii(b[i]))
            return false;
    return true;
}

std::string_view trim(std::string_view text) noexcept
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

int hex_value(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Reads the stream into a buffer this class owns, so a head or a chunk size that
// arrives a byte at a time is assembled here rather than in every caller.
class Reader
{
  public:
    Reader(Stream &stream, std::int64_t deadline_ms) noexcept
        : stream_{stream}, deadline_{deadline_ms}
    {
    }

    std::int64_t deadline() const noexcept
    {
        return deadline_;
    }

    // Bytes read and not yet consumed.
    std::string_view buffered() const noexcept
    {
        return {buffer_.data() + used_, buffer_.size() - used_};
    }

    void consume(std::size_t count) noexcept
    {
        used_ += count;
        if (used_ >= buffer_.size())
        {
            buffer_.clear();
            used_ = 0;
        }
    }

    // One read. ok means bytes arrived, and timeout means none did -- the caller
    // decides whether its deadline has passed.
    HttpStatus fill()
    {
        std::uint8_t bytes[kReadChunk];
        const StreamResult got = stream_.read(bytes, sizeof(bytes), remaining_ms(deadline_));
        switch (got.status)
        {
        case StreamStatus::ok:
            buffer_.append(reinterpret_cast<const char *>(bytes), got.transferred);
            return HttpStatus::ok;
        case StreamStatus::timeout:
            return HttpStatus::timeout;
        case StreamStatus::closed:
            return HttpStatus::closed;
        case StreamStatus::failed:
            return HttpStatus::transport_error;
        }
        return HttpStatus::transport_error;
    }

    // Reads until `count` bytes are buffered. A timeout is only final once the
    // deadline has really passed; before that it is a stream that had nothing
    // yet, which is what a caller that has seconds to spend should keep waiting
    // through.
    HttpStatus need(std::size_t count)
    {
        while (buffered().size() < count)
        {
            const bool expired = now_ms() >= deadline_;
            const HttpStatus status = fill();
            if (status == HttpStatus::ok)
                continue;
            if (status == HttpStatus::timeout)
            {
                if (expired)
                    return HttpStatus::timeout;
                continue;
            }
            return status; // closed, or the transport broke
        }
        return HttpStatus::ok;
    }

    // Reads until `needle` shows up, and leaves its offset in `at`. The needle
    // has to appear inside the first `limit` bytes: a buffer that reaches the
    // limit without it is too_large, whichever read brought it in.
    HttpStatus find(std::string_view needle, std::size_t limit, std::size_t &at)
    {
        while (true)
        {
            const std::string_view have = buffered();
            const std::size_t found = have.find(needle);
            if (found != std::string_view::npos)
            {
                if (found + needle.size() > limit)
                    return HttpStatus::too_large;
                at = found;
                return HttpStatus::ok;
            }
            if (have.size() >= limit)
                return HttpStatus::too_large;

            const bool expired = now_ms() >= deadline_;
            const HttpStatus status = fill();
            if (status == HttpStatus::ok)
                continue;
            if (status == HttpStatus::timeout)
            {
                if (expired)
                    return HttpStatus::timeout;
                continue;
            }
            return status;
        }
    }

  private:
    Stream &stream_;
    std::int64_t deadline_;
    std::string buffer_;
    std::size_t used_ = 0; // consumed prefix of buffer_
};

// RFC 9112 6.3: 1xx, 204 and 304 carry no body whatever their headers say. A
// client that read the Content-Length they may still show would wait for bytes
// the server is never going to send.
bool has_no_body(int status) noexcept
{
    return (status >= 100 && status < 200) || status == 204 || status == 304;
}

HttpStatus parse_status_line(std::string_view line, HttpResponse &out, std::string &error)
{
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos || line.compare(0, 5, "HTTP/") != 0)
    {
        error = "the answer does not start with a status line";
        return HttpStatus::bad_response;
    }
    if (line.compare(0, 7, "HTTP/1.") != 0)
    {
        error = "the answer is neither HTTP/1.0 nor HTTP/1.1";
        return HttpStatus::bad_response;
    }

    std::size_t at = space;
    while (at < line.size() && line[at] == ' ')
        ++at;

    int status = 0;
    std::size_t digits = 0;
    while (at < line.size() && line[at] >= '0' && line[at] <= '9' && digits < 3)
    {
        status = status * 10 + (line[at] - '0');
        ++at;
        ++digits;
    }
    if (digits != 3)
    {
        error = "the status line has no three-digit status";
        return HttpStatus::bad_response;
    }
    if (at < line.size() && line[at] != ' ')
    {
        error = "the status line is not a status followed by a reason phrase";
        return HttpStatus::bad_response;
    }

    out.status = status;
    while (at < line.size() && line[at] == ' ')
        ++at;
    out.reason.assign(line.substr(at));
    return HttpStatus::ok;
}

HttpStatus parse_header_lines(std::string_view block, HttpResponse &out, std::string &error)
{
    std::size_t at = 0;
    while (at < block.size())
    {
        const std::size_t end = block.find("\r\n", at);
        const std::string_view line =
            block.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);

        if (line.empty())
        {
            error = "the header block has a blank line inside it";
            return HttpStatus::bad_response;
        }
        // RFC 9112 5.2: a line that starts with whitespace continues the one
        // before it, and no server may send one any more. Reading it as a header
        // of its own is how a response is misread.
        if (line.front() == ' ' || line.front() == '\t')
        {
            error = "a header line continues the one above it, which no server may send";
            return HttpStatus::bad_response;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0)
        {
            error = "a header line has no name";
            return HttpStatus::bad_response;
        }
        // And no whitespace between the name and the colon, for the same reason.
        const std::string_view name = line.substr(0, colon);
        if (trim(name).size() != name.size())
        {
            error = "a header name has whitespace before its colon";
            return HttpStatus::bad_response;
        }

        out.headers.push_back(Header{std::string{name}, std::string{trim(line.substr(colon + 1))}});
        if (end == std::string_view::npos)
            break;
        at = end + 2;
    }
    return HttpStatus::ok;
}

HttpStatus parse_head(std::string_view head, HttpResponse &out, std::string &error)
{
    const std::size_t line_end = head.find("\r\n");
    const std::string_view status_line = line_end == std::string_view::npos ? head : head.substr(0, line_end);
    const HttpStatus status = parse_status_line(status_line, out, error);
    if (status != HttpStatus::ok || line_end == std::string_view::npos)
        return status;
    return parse_header_lines(head.substr(line_end + 2), out, error);
}

// How many times a header appears. The ones that decide how long the body is
// must appear exactly once: a second one with a different value is how one
// message is read as two.
std::size_t count_header(const std::vector<Header> &headers, std::string_view name) noexcept
{
    std::size_t count = 0;
    for (const Header &header : headers)
        if (equals_ignoring_case(header.name, name))
            ++count;
    return count;
}

// A Content-Length is a run of digits and nothing else: no sign, no space, no
// hex. `over_the_limit` is kept apart from `not_a_number` because the two are
// different news for the caller -- one is a peer that is too greedy, the other a
// peer that is not speaking HTTP -- and the accumulation stops at the first
// digit past the limit, so no value can overflow on the way.
enum class Length
{
    ok,
    not_a_number,
    over_the_limit,
};

Length parse_length(std::string_view text, std::size_t limit, std::size_t &out) noexcept
{
    if (text.empty())
        return Length::not_a_number;
    std::size_t value = 0;
    for (char c : text)
    {
        if (c < '0' || c > '9')
            return Length::not_a_number;
        value = value * 10 + static_cast<std::size_t>(c - '0');
        if (value > limit)
            return Length::over_the_limit;
    }
    out = value;
    return Length::ok;
}

HttpStatus read_exactly(Reader &reader, std::size_t count, std::string &out)
{
    while (count > 0)
    {
        const HttpStatus status = reader.need(1);
        if (status != HttpStatus::ok)
            return status;
        const std::string_view have = reader.buffered();
        const std::size_t take = std::min(count, have.size());
        out.append(have.data(), take);
        reader.consume(take);
        count -= take;
    }
    return HttpStatus::ok;
}

// Until the peer closes: the third way an HTTP/1.1 answer says where its body
// ends, and the one an answer with neither of the other two uses.
HttpStatus read_until_close(Reader &reader, std::string &out)
{
    while (true)
    {
        const std::string_view have = reader.buffered();
        if (!have.empty())
        {
            if (out.size() + have.size() > HttpClient::kMaxBodyBytes)
                return HttpStatus::too_large;
            out.append(have.data(), have.size());
            reader.consume(have.size());
            continue;
        }

        const bool expired = now_ms() >= reader.deadline();
        const HttpStatus status = reader.fill();
        if (status == HttpStatus::ok)
            continue;
        if (status == HttpStatus::timeout)
        {
            if (expired)
                return HttpStatus::timeout;
            continue;
        }
        if (status == HttpStatus::closed)
            return HttpStatus::ok; // that is what ends this body
        return status;
    }
}

HttpStatus read_chunked(Reader &reader, std::string &out)
{
    while (true)
    {
        // The chunk-size line, which a peer may split across reads like anything
        // else. Its extensions, after a ';', are read and dropped.
        std::size_t line_end = 0;
        HttpStatus status = reader.find("\r\n", HttpClient::kMaxHeaderBytes, line_end);
        if (status != HttpStatus::ok)
            return status;

        const std::string_view line = reader.buffered().substr(0, line_end);
        std::size_t digits = line.find(';');
        if (digits == std::string_view::npos)
            digits = line.size();
        if (digits == 0)
            return HttpStatus::bad_response; // a chunk with no size in front of it

        std::size_t size = 0;
        for (std::size_t i = 0; i < digits; ++i)
        {
            const int value = hex_value(line[i]);
            if (value < 0)
                return HttpStatus::bad_response; // not a chunk size
            size = size * 16 + static_cast<std::size_t>(value);
            if (size > HttpClient::kMaxBodyBytes)
                return HttpStatus::too_large;
        }
        reader.consume(line_end + 2);

        if (size == 0)
        {
            // The last chunk, then whatever trailers the peer attached and the
            // blank line that ends them -- usually just the blank line.
            status = reader.need(2);
            if (status != HttpStatus::ok)
                return status;
            if (reader.buffered().substr(0, 2) == "\r\n")
            {
                reader.consume(2);
                return HttpStatus::ok;
            }
            std::size_t trailers_end = 0;
            status = reader.find("\r\n\r\n", HttpClient::kMaxHeaderBytes, trailers_end);
            if (status != HttpStatus::ok)
                return status;
            reader.consume(trailers_end + 4);
            return HttpStatus::ok;
        }

        if (out.size() + size > HttpClient::kMaxBodyBytes)
            return HttpStatus::too_large;
        status = read_exactly(reader, size, out);
        if (status != HttpStatus::ok)
            return status;

        // And the CRLF that closes the chunk.
        status = reader.need(2);
        if (status != HttpStatus::ok)
            return status;
        if (reader.buffered().substr(0, 2) != "\r\n")
            return HttpStatus::bad_response; // a chunk that is not followed by its CRLF
        reader.consume(2);
    }
}
} // namespace

std::string render_request(const HttpRequest &request)
{
    const std::string method = request.method.empty() ? std::string{"GET"} : request.method;
    const std::string target = request.target.empty() ? std::string{"/"} : request.target;

    std::string out;
    out += method;
    out += ' ';
    out += target;
    out += " HTTP/1.1\r\nHost: ";
    out += request.host;
    out += "\r\nConnection: close\r\n";
    for (const Header &header : request.headers)
    {
        out += header.name;
        out += ": ";
        out += header.value;
        out += "\r\n";
    }
    // A GET says nothing about a body; a POST says how long its body is even
    // when that is zero, because a POST is the one that is expected to have one.
    if (!request.body.empty() || method == "POST")
    {
        out += "Content-Length: ";
        out += std::to_string(request.body.size());
        out += "\r\n";
    }
    out += "\r\n";
    out += request.body;
    return out;
}

HttpRequest HttpRequest::get(std::string host, std::string target)
{
    HttpRequest request;
    request.method = "GET";
    request.host = std::move(host);
    request.target = std::move(target);
    return request;
}

HttpRequest HttpRequest::post(std::string host, std::string target, std::string body,
                              std::string content_type)
{
    HttpRequest request;
    request.method = "POST";
    request.host = std::move(host);
    request.target = std::move(target);
    request.body = std::move(body);
    if (!content_type.empty())
        request.headers.push_back(Header{"Content-Type", std::move(content_type)});
    return request;
}

std::string_view HttpResponse::header(std::string_view name) const noexcept
{
    for (const Header &header : headers)
        if (equals_ignoring_case(header.name, name))
            return header.value;
    return {};
}

std::string_view HttpResponse::retry_after() const noexcept
{
    return header("retry-after");
}

HttpStatus HttpClient::fail(HttpStatus status, std::string text)
{
    error_ = std::move(text);
    return status;
}

HttpStatus HttpClient::send(Stream &stream, const HttpRequest &request, int timeout_ms,
                            HttpResponse &out)
{
    error_.clear();
    out = HttpResponse{};

    if (request.method.empty() || request.host.empty())
        return fail(HttpStatus::bad_request, "the request has no method or no host");

    const std::int64_t deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    const std::string bytes = render_request(request);
    const StreamResult wrote = stream.write(reinterpret_cast<const std::uint8_t *>(bytes.data()),
                                            bytes.size(), remaining_ms(deadline));
    if (wrote.status == StreamStatus::timeout)
        return fail(HttpStatus::timeout, "the request did not go out inside the wait");
    if (wrote.status != StreamStatus::ok || wrote.transferred != bytes.size())
        return fail(HttpStatus::transport_error, "the request did not go out whole");

    Reader reader{stream, deadline};

    std::size_t head_end = 0;
    HttpStatus status = reader.find("\r\n\r\n", kMaxHeaderBytes, head_end);
    if (status == HttpStatus::closed)
        return fail(status, "the peer closed before the headers were complete");
    if (status == HttpStatus::too_large)
        return fail(status, "the header block is larger than this client reads");
    if (status != HttpStatus::ok)
        return fail(status, "the answer did not arrive inside the wait");

    status = parse_head(reader.buffered().substr(0, head_end), out, error_);
    if (status != HttpStatus::ok)
        return status;
    reader.consume(head_end + 4);

    // A 3xx is an answer like any other and is handed over as one: no hop is
    // taken here, and a 429 is not retried. Only the body is read out.
    if (has_no_body(out.status))
        return HttpStatus::ok;

    const std::size_t transfer_encodings = count_header(out.headers, "transfer-encoding");
    const std::size_t content_lengths = count_header(out.headers, "content-length");
    const std::string_view encoding = out.header("transfer-encoding");
    const std::string_view length = out.header("content-length");

    if (transfer_encodings > 1 || content_lengths > 1)
        return fail(HttpStatus::bad_response, "the answer says twice how long its body is");

    // The whole value has to be `chunked`: anything in front of it is a coding
    // this client cannot undo, and reading its output as the body would hand the
    // caller compressed bytes as if they were the answer.
    const bool chunked = !encoding.empty() && equals_ignoring_case(trim(encoding), "chunked");
    if (!encoding.empty() && !chunked)
        return fail(HttpStatus::bad_response, "the answer uses a transfer coding this client does not read");
    if (chunked && !length.empty())
        return fail(HttpStatus::bad_response, "the answer has both Transfer-Encoding and Content-Length");

    if (chunked)
    {
        status = read_chunked(reader, out.body);
        if (status != HttpStatus::ok)
            return fail(status, "the chunked body broke off or is not chunked");
        return HttpStatus::ok;
    }

    if (!length.empty())
    {
        std::size_t wanted = 0;
        switch (parse_length(length, kMaxBodyBytes, wanted))
        {
        case Length::not_a_number:
            return fail(HttpStatus::bad_response, "the answer has a Content-Length that is not a length");
        case Length::over_the_limit:
            // Refused before the first byte of the body: a peer that claims a
            // gigabyte is cut off, not read.
            return fail(HttpStatus::too_large, "the body is larger than this client reads");
        case Length::ok:
            break;
        }
        status = read_exactly(reader, wanted, out.body);
        if (status != HttpStatus::ok)
            return fail(status, "the body broke off before its Content-Length");
        return HttpStatus::ok;
    }

    status = read_until_close(reader, out.body);
    if (status == HttpStatus::too_large)
        return fail(status, "the body is larger than this client reads");
    if (status != HttpStatus::ok)
        return fail(status, "the body, which ends with the connection, broke off");
    return HttpStatus::ok;
}
} // namespace accord::core
