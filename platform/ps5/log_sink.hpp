/*
 * Accord - PS5 side of the logging seam.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace accord::ps5
{
// Routes core/ log lines to stdout, which the console collects in its log.
void install_log_sink() noexcept;
} // namespace accord::ps5
