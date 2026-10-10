/*
 * Accord - Home screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Placeholder: it only proves the drawing path end to end.
 */

#pragma once

#include "core/app_info.hpp"
#include "platform/ps5/demo_renderer.hpp"

namespace accord::ui
{
// The leading :: matters: inside accord::ui the name ps5 would otherwise
// bind to accord::ps5 rather than the SDK-side namespace.
void draw_home_screen(::ps5::demo::Canvas &canvas, const core::AppInfo &info) noexcept;
} // namespace accord::ui
