/*
 * Discord PS5 - Host substitutes for the platform layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Built into the host test binary only; never part of the PS5 build. The
 * staging step copies core/, platform/ps5/ and ui/ into the boilerplate, so
 * this directory is excluded by construction.
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace discord_ps5::host
{
// Lines handed to the log seam since the last clear, without the level prefix.
std::vector<std::string> &captured_log() noexcept;

void clear_captured_log() noexcept;

// Installs a sink that appends every line to captured_log().
void install_capturing_log_sink() noexcept;

// captured_log() joined with '\n', for readable assertions.
std::string captured_text();
} // namespace discord_ps5::host
