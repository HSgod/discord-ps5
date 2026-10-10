/*
 * Accord - SHA-1, and only as much of it as the handshake needs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RFC 6455 4.2.2 defines the server's answer as base64(sha1(key + GUID)), and
 * that is the only place this project uses SHA-1. It is not a security hash
 * any more and must not be used as one; it is here because the handshake says
 * so, which is also why it is written out rather than pulled in as a
 * dependency: a whole library for one call, on three build paths, is worse than
 * sixty lines with a test vector from the RFC.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace accord::core
{
class Sha1
{
  public:
    Sha1() noexcept;

    void update(const std::uint8_t *data, std::size_t size) noexcept;
    void update(std::string_view text) noexcept;

    // Writes the 20-byte digest and leaves the object usable only for update()
    // calls that would start a new digest; there is no reset() because nothing
    // here needs one.
    void finish(std::uint8_t out[20]) noexcept;

  private:
    void compress(const std::uint8_t block[64]) noexcept;

    std::uint32_t state_[5] = {};
    std::uint64_t length_ = 0;
    std::uint8_t pending_[64] = {};
    std::size_t pending_size_ = 0;
};

std::array<std::uint8_t, 20> sha1(std::string_view text) noexcept;
} // namespace accord::core
