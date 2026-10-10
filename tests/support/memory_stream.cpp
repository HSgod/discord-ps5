/*
 * Accord - A core::Stream a test can script byte by byte.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/support/memory_stream.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace accord::test
{
namespace
{
// The client asks for the time it has left, which can be seconds; a fake socket
// does not have to burn that, because the client checks its own clock anyway and
// will come back until the deadline is really gone.
constexpr int kMaxWaitMs = 25;

void wait_ms(int milliseconds) noexcept
{
    const int capped = std::min(milliseconds, kMaxWaitMs);
    if (capped > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds{capped});
}
} // namespace

void MemoryStream::feed(std::string_view bytes)
{
    Step step;
    step.kind = Step::Kind::data;
    step.bytes.assign(bytes);
    script_.push_back(std::move(step));
}

void MemoryStream::feed_timeout()
{
    Step step;
    step.kind = Step::Kind::timeout;
    script_.push_back(std::move(step));
}

void MemoryStream::feed_closed()
{
    Step step;
    step.kind = Step::Kind::closed;
    script_.push_back(std::move(step));
}

accord::core::StreamResult MemoryStream::read(std::uint8_t *data, std::size_t size,
                                              int timeout_ms) noexcept
{
    if (!script_.empty())
    {
        Step &step = script_.front();
        if (step.kind == Step::Kind::data)
        {
            std::size_t take = std::min(size, step.bytes.size());
            if (read_chunk_ > 0)
                take = std::min(take, read_chunk_);
            std::memcpy(data, step.bytes.data(), take);
            step.bytes.erase(0, take);
            if (step.bytes.empty())
                script_.pop_front();
            return {accord::core::StreamStatus::ok, take};
        }
        if (step.kind == Step::Kind::closed)
        {
            script_.pop_front();
            return {accord::core::StreamStatus::closed, 0};
        }
        script_.pop_front(); // a timeout still waits, like a socket that has nothing
    }

    wait_ms(timeout_ms);
    return {accord::core::StreamStatus::timeout, 0};
}

accord::core::StreamResult MemoryStream::write(const std::uint8_t *data, std::size_t size,
                                               int timeout_ms) noexcept
{
    (void)timeout_ms;
    if (fail_writes_)
        return {accord::core::StreamStatus::failed, 0};

    written_.append(reinterpret_cast<const char *>(data), size);
    return {accord::core::StreamStatus::ok, size};
}
} // namespace accord::test
