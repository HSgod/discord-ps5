/*
 * Accord - Logging seam between pure logic and the platform layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Logic in core/ never writes to a console by itself; it hands lines to a sink
 * the platform installs. That keeps core/ testable on the host and lets the
 * host tests capture what the app would have printed.
 */

#pragma once

#include <string_view>

namespace accord::core
{
enum class LogLevel
{
    info,
    warn,
    error,
};

std::string_view to_string(LogLevel level) noexcept;

using LogSink = void (*)(LogLevel level, std::string_view message) noexcept;

// Passing nullptr restores the silent default.
void set_log_sink(LogSink sink) noexcept;
LogSink log_sink() noexcept;

// No-op when no sink is installed.
void log(LogLevel level, std::string_view message) noexcept;
} // namespace accord::core
