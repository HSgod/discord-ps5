/*
 * Accord - Host substitutes for the platform layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform/host/host_stub.hpp"

#include "core/log.hpp"

namespace accord::host
{
namespace
{
void capture(core::LogLevel level, std::string_view message) noexcept
{
    try
    {
        captured_log().emplace_back(message);
        (void)level;
    }
    catch (...)
    {
        // A test sink must never take the process down; drop the line instead.
    }
}
} // namespace

std::vector<std::string> &captured_log() noexcept
{
    static std::vector<std::string> lines;
    return lines;
}

void clear_captured_log() noexcept
{
    captured_log().clear();
}

void install_capturing_log_sink() noexcept
{
    core::set_log_sink(&capture);
}

std::string captured_text()
{
    std::string joined;
    for (const std::string &line : captured_log())
    {
        if (!joined.empty())
            joined.push_back('\n');
        joined += line;
    }
    return joined;
}
} // namespace accord::host
