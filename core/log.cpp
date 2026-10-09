/*
 * Discord PS5 - Logging seam between pure logic and the platform layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/log.hpp"

namespace discord_ps5::core
{
namespace
{
LogSink active_sink = nullptr;
} // namespace

std::string_view to_string(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::info:
        return "info";
    case LogLevel::warn:
        return "warn";
    case LogLevel::error:
        return "error";
    }
    return "unknown";
}

void set_log_sink(LogSink sink) noexcept
{
    active_sink = sink;
}

LogSink log_sink() noexcept
{
    return active_sink;
}

void log(LogLevel level, std::string_view message) noexcept
{
    if (active_sink != nullptr)
        active_sink(level, message);
}
} // namespace discord_ps5::core
