/*
 * Accord - PS5 entry point.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The boilerplate supplies no main() of its own: it compiles exactly what
 * APP_SOURCE_DIR holds, so this file is the whole application entry.
 */

#include "core/app_info.hpp"
#include "core/log.hpp"
#include "platform/ps5/demo_renderer.hpp"
#include "platform/ps5/log_sink.hpp"
#include "ui/home_screen.hpp"

namespace
{
constexpr std::string_view ready_message = "Accord shell ready";

void draw_scene(ps5::demo::Canvas &canvas) noexcept
{
    accord::ui::draw_home_screen(canvas, accord::core::app_info());
}
} // namespace

int main()
{
    accord::ps5::install_log_sink();

    const accord::core::AppInfo &info = accord::core::app_info();
    accord::core::log(accord::core::LogLevel::info, info.name);

    // Never returns: it owns the video output and stays up until the shell
    // asks the application to close.
    ps5::demo::run(draw_scene, ready_message);
}
