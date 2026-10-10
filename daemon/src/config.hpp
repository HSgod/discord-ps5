/*
 * Accord - Defaults for the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The default port is deliberately outside the range that is already busy on
 * the console: 2121, 3232, 6970, 8084, 8888, 10101, 12800, 34177.
 */

#pragma once

#include <cstdint>

namespace accord::daemon
{
inline constexpr std::uint16_t kDefaultPort = 8280;
inline constexpr const char *kLogPath = "/data/accord/daemon.log";
} // namespace accord::daemon
