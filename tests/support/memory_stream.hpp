/*
 * Accord - A core::Stream a test can script byte by byte.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The WebSocket client's awkward cases -- a frame that stops halfway, a peer
 * that goes quiet in the middle of a header, a close code that is not legal --
 * are painful to provoke over a real socket and trivial to write down here. The
 * socket path keeps its own, smaller set of tests.
 *
 * The script is a queue: data, or a wait that runs out, or the peer ending the
 * stream, in the order they were fed.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "core/stream.hpp"

namespace accord::test
{
class MemoryStream final : public accord::core::Stream
{
  public:
    // Bytes the next reads hand out, in order.
    void feed(std::string_view bytes);
    // The next read behaves as if the wait ran out. Bytes already fed stay where
    // they are, so this can sit in front of them or between them.
    void feed_timeout();
    // The next read reports the peer gone, as a socket does at end of stream.
    void feed_closed();

    // Everything the client wrote, raw, exactly as it went on the wire.
    const std::string &written() const noexcept
    {
        return written_;
    }
    void clear_written()
    {
        written_.clear();
    }
    void set_fail_writes(bool on) noexcept
    {
        fail_writes_ = on;
    }
    // Hands out at most this many bytes per read, so a test can make every
    // frame arrive in pieces.
    void set_read_chunk(std::size_t chunk) noexcept
    {
        read_chunk_ = chunk == 0 ? 1 : chunk;
    }

    accord::core::StreamResult read(std::uint8_t *data, std::size_t size,
                                    int timeout_ms) noexcept override;
    accord::core::StreamResult write(const std::uint8_t *data, std::size_t size,
                                     int timeout_ms) noexcept override;

  private:
    struct Step
    {
        enum class Kind
        {
            data,
            timeout,
            closed,
        };
        Kind kind = Kind::data;
        std::string bytes;
    };

    std::deque<Step> script_;
    std::string written_;
    std::size_t read_chunk_ = 0; // 0 = as much as the caller asked for
    bool fail_writes_ = false;
};
} // namespace accord::test
