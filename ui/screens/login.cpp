/*
 * Accord - Login screen (QR code), static data.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The code is drawn from a fixed pattern so the layout can be reviewed; the
 * Remote Auth handshake will replace it later.
 */

#include "ui/screens.hpp"

#include <array>
#include <cstdint>

namespace accord::ui
{
namespace
{
// 29x29 modules, the smallest real QR version. A module is one cell; the three
// finder squares are drawn first and the data area is a fixed hash, so the
// picture is stable between runs and between the host and the console.
constexpr int kModules = 29;

bool is_finder(int x, int y)
{
    const auto in_square = [](int ox, int oy, int px, int py) {
        if (px < ox || px >= ox + 7 || py < oy || py >= oy + 7)
            return false;
        const int lx = px - ox;
        const int ly = py - oy;
        const bool ring = lx == 0 || lx == 6 || ly == 0 || ly == 6;
        const bool core = lx >= 2 && lx <= 4 && ly >= 2 && ly <= 4;
        return ring || core;
    };
    return in_square(0, 0, x, y) || in_square(kModules - 7, 0, x, y) || in_square(0, kModules - 7, x, y);
}

bool is_dark(int x, int y)
{
    if (is_finder(x, y))
        return true;
    // Quiet zone around the finder squares stays empty.
    const bool quiet = (x < 8 && y < 8) || (x >= kModules - 8 && y < 8) || (x < 8 && y >= kModules - 8);
    if (quiet)
        return false;
    std::uint32_t hash = static_cast<std::uint32_t>(x) * 73856093u ^ static_cast<std::uint32_t>(y) * 19349663u;
    hash ^= hash >> 13;
    return (hash % 5u) < 2u;
}
} // namespace

void draw_login(hui::gfx::DrawList &list, const ScreenContext &context)
{
    using hui::gfx::Align;
    using hui::gfx::Color;
    using hui::gfx::Rect;

    hui::ui::Painter paint(list, context.fonts, context.theme, context.glass_texture);
    const Color text = context.theme.text;
    const Color muted = context.theme.text_muted;

    // The card the code sits in.
    const Rect card{610, 150, 700, 780};
    paint.panel(card);

    paint.heading("Log in", 660, 250, 52, text);
    paint.body("Scan this code with the Discord app on your phone.", 660, 300, 24, muted);

    // The code itself: a white tile with the modules on top.
    constexpr float kCell = 13.0f;
    const float qr = kModules * kCell;
    const Rect tile{960.0f - qr * 0.5f - 24.0f, 356.0f, qr + 48.0f, qr + 48.0f};
    list.shadow(tile, 18.0f, 26.0f, Color::rgb(0x000000, 0.45f));
    list.rounded_rect(tile, 18.0f, Color::rgb(0xffffff));

    const float origin_x = 960.0f - qr * 0.5f;
    const float origin_y = 380.0f;
    for (int y = 0; y < kModules; ++y)
    {
        for (int x = 0; x < kModules; ++x)
        {
            if (!is_dark(x, y))
                continue;
            list.rounded_rect({origin_x + x * kCell, origin_y + y * kCell, kCell, kCell}, 2.0f,
                              Color::rgb(0x11132b));
        }
    }

    // A ring turning under the code: "waiting for the phone". The host runner
    // freezes time, so the phase is fixed but the shape is the console's.
    const float phase = context.time * 2.2f;
    list.arc(672.0f, 836.0f, 16.0f, 4.0f, phase, 1.2f, context.theme.primary);

    paint.body("Waiting for confirmation", 704.0f, 844.0f, 22, muted);
    paint.label("ABCD-1234", 1264.0f, 844.0f, 22, text, Align::right);

    paint.label("Y  CANCEL", 660.0f, 888.0f, 20, muted);
    paint.label("PAD OK", 1260.0f, 888.0f, 20, muted, Align::right);
}

} // namespace accord::ui
