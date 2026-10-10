/*
 * Accord - Application identity shared by every platform layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pure host logic: knows nothing about the PS5 SDK, so it compiles and is
 * testable with the host toolchain. The title ID is deliberately absent -- it
 * lives in sce_sys/param.json only, so the two cannot drift apart.
 */

#pragma once

#include <string_view>

namespace accord::core
{
struct AppInfo
{
    std::string_view name;
    std::string_view version;
};

// Stable for the lifetime of the process.
const AppInfo &app_info() noexcept;
} // namespace accord::core
