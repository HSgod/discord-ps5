/*
 * Discord PS5 - Home screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/home_screen.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace discord_ps5::ui
{
namespace
{
// The CPU font shipped with the boilerplate has capitals only, so anything
// coming from core/ is folded before it is drawn.
std::array<char, 64> uppercase(std::string_view value) noexcept
{
    std::array<char, 64> folded{};

    const std::size_t count = value.size() < folded.size() - 1 ? value.size() : folded.size() - 1;
    for (std::size_t index = 0; index < count; ++index)
    {
        const char character = value[index];
        folded[index] = character >= 'a' && character <= 'z'
                            ? static_cast<char>(character - 'a' + 'A')
                            : character;
    }
    return folded;
}
} // namespace

void draw_home_screen(::ps5::demo::Canvas &canvas, const core::AppInfo &info) noexcept
{
    using ::ps5::demo::Color;

    canvas.clear(Color::background);
    canvas.rectangle(120, 375, 1680, 8, Color::panel);

    canvas.text(120, 110, uppercase(info.name).data(), 12, Color::white);
    canvas.text(120, 265, uppercase(info.version).data(), 5, Color::cyan);
    canvas.text(120, 960, "T2.1 SKELETON - SCREENS ARRIVE IN T2.2", 4, Color::yellow);
}
} // namespace discord_ps5::ui
