/*
 * Accord - Base64, encoding only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The handshake needs it in one direction: the 16 random bytes of the key go
 * out as Sec-WebSocket-Key, and the server's answer is compared against the
 * same encoding of the digest. Nothing in the protocol requires reading base64
 * back, so there is no decoder here to leave untested.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace accord::core
{
// Std, with padding; the empty input gives the empty string.
std::string base64_encode(const std::uint8_t *data, std::size_t size);
std::string base64_encode(std::string_view text);
} // namespace accord::core
