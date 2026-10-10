/*
 * Accord - The system's random bytes, behind one function.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Two things need them: the 16-byte nonce in Sec-WebSocket-Key and the fresh
 * masking key RFC 6455 5.3 requires per client frame. Both are read from
 * /dev/urandom, which the console kernel offers as well as the host, so there
 * is no platform seam here. A failure is reported rather than papered over:
 * a handshake nonce that is not random is not a nonce.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace accord::core
{
// Fills the whole buffer, or returns false and leaves its content undefined.
bool fill_random(std::uint8_t *buffer, std::size_t size) noexcept;
} // namespace accord::core
