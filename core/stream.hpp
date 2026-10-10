/*
 * Accord - The byte stream the protocol code talks to, with no socket in it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * T3.3 runs before T3.1, so the WebSocket client is written against this seam
 * rather than against a socket: the host tests drive it over plain TCP, and the
 * TLS client later drops in underneath it without the WebSocket code changing.
 *
 * The interface is deliberately small and value-returning: no exceptions, no
 * errno, and a timeout is an ordinary answer rather than a failure, because a
 * client waiting on a gateway for minutes is doing exactly that.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace accord::core
{
enum class StreamStatus
{
    ok,      // at least one byte moved
    timeout, // nothing arrived inside the wait the caller asked for
    closed,  // the peer ended the stream; nothing more will ever arrive
    failed,  // the transport broke
};

struct StreamResult
{
    StreamStatus status = StreamStatus::ok;
    std::size_t transferred = 0;
};

class Stream
{
  public:
    Stream() = default;
    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;
    virtual ~Stream() = default;

    // Hands back whatever one read gave, never more than `size`. Status::ok
    // means at least one byte; a caller that needs exactly N bytes loops.
    virtual StreamResult read(std::uint8_t *data, std::size_t size, int timeout_ms) noexcept = 0;

    // Takes the whole buffer before returning ok; a short write is a failure of
    // this call, not something the caller has to stitch together.
    virtual StreamResult write(const std::uint8_t *data, std::size_t size,
                               int timeout_ms) noexcept = 0;
};
} // namespace accord::core
