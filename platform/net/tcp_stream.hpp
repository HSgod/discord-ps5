/*
 * Accord - TCP as a core::Stream, for the host tests and the console payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX sockets, the same calls on both: the payload SDK stubs getaddrinfo,
 * poll and the socket options (checked with a probe before this file was
 * written), so there is one implementation rather than a host fake plus the
 * real thing. TLS of T3.1 wraps this rather than replacing it.
 *
 * The class owns a file descriptor, so it is movable and not copyable, and it
 * closes itself when it goes out of scope.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/stream.hpp"

namespace accord::net
{
class TcpStream final : public core::Stream
{
  public:
    TcpStream() = default;
    ~TcpStream() override;
    TcpStream(TcpStream &&other) noexcept;
    TcpStream &operator=(TcpStream &&other) noexcept;
    TcpStream(const TcpStream &) = delete;
    TcpStream &operator=(const TcpStream &) = delete;

    // Resolves and connects, trying each address the resolver hands back and
    // waiting no longer than timeout_ms in total. Nagle is switched off, because
    // a gateway frame is small and there is nothing to batch it with. False
    // means nothing was connected and error() says why.
    bool connect(std::string_view host, std::uint16_t port, int timeout_ms);

    // read: one recv, up to `size`, waiting the caller's timeout. write: the
    // whole buffer or a failure, because a half-written frame is worse than a
    // closed connection.
    core::StreamResult read(std::uint8_t *data, std::size_t size, int timeout_ms) noexcept override;
    core::StreamResult write(const std::uint8_t *data, std::size_t size,
                             int timeout_ms) noexcept override;

    void close() noexcept;
    bool is_open() const noexcept;

    // The text of the last failure: the resolver's answer, the socket error, or
    // the name of the call that timed out.
    const std::string &error() const noexcept
    {
        return error_;
    }

  private:
    int fd_ = -1;
    std::string error_;
};
} // namespace accord::net
