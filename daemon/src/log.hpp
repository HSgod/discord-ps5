/*
 * Discord PS5 - Log file of the background daemon payload.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The daemon has no console to print to while it runs, so every line goes to
 * a file the operator can pull afterwards. Lines are flushed as they are
 * written: a payload that is killed must still leave a readable log.
 */

#pragma once

#include <string_view>

#include "core/log.hpp"

namespace discord_ps5::daemon
{
// Creates the parent directory, then opens the file for appending. A false
// return leaves the daemon running, but without a log file.
bool open_log(std::string_view path) noexcept;

void close_log() noexcept;

// printf-style, one timestamped line. Also mirrored to stdout or stderr.
void logf(discord_ps5::core::LogLevel level, const char *format, ...) noexcept;
} // namespace discord_ps5::daemon
