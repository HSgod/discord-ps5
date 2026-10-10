/*
 * Accord - Application identity shared by every platform layer.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/app_info.hpp"

namespace accord::core
{
namespace
{
constexpr std::string_view name = "Accord";
constexpr std::string_view version = "0.1.0-skeleton";
} // namespace

const AppInfo &app_info() noexcept
{
    static const AppInfo info{name, version};
    return info;
}
} // namespace accord::core
