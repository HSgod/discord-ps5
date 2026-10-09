/*
 * Discord PS5 - PS5 side of the logging seam.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform/ps5/log_sink.hpp"

#include "core/log.hpp"

#include <cstdio>
#include <string_view>

namespace discord_ps5::ps5
{
namespace
{
// Written with fwrite rather than printf so no allocation and no formatting
// machinery runs on the console, where the sink is called from anywhere.
void write_line(core::LogLevel level, std::string_view message) noexcept
{
    const std::string_view name = core::to_string(level);

    std::fputs("[discord-ps5] ", stdout);
    std::fwrite(name.data(), 1, name.size(), stdout);
    std::fputs(": ", stdout);
    std::fwrite(message.data(), 1, message.size(), stdout);
    std::fputc('\n', stdout);
}
} // namespace

void install_log_sink() noexcept
{
    core::set_log_sink(&write_line);
}
} // namespace discord_ps5::ps5
