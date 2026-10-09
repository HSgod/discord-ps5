/*
 * Discord PS5 - PS5 side of the logging seam.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace discord_ps5::ps5
{
// Routes core/ log lines to stdout, which the console collects in its log.
void install_log_sink() noexcept;
} // namespace discord_ps5::ps5
