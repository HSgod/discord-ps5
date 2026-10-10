/*
 * Discord PS5 - Stream player, static data.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The picture is a stand-in: the CPU decoder will blit real H.264/VP8 frames
 * where this placeholder is drawn.
 */

#include "ui/screens.hpp"

#include <array>
#include <string_view>

namespace discord_ps5::ui
{
namespace
{
using screens_detail::avatar;
using screens_detail::name_color;
} // namespace

void draw_stream(hui::gfx::DrawList &list, const ScreenContext &context)
{
    using hui::gfx::Align;
    using hui::gfx::Color;
    using hui::gfx::Rect;

    hui::ui::Painter paint(list, context.fonts, context.theme, context.glass_texture);
    const hui::ui::FontRef &semibold = context.fonts.semibold;
    const Color text = context.theme.text;
    const Color muted = context.theme.text_muted;

    // The video surface, letterboxed inside the frame.
    const Rect video{136, 96, 1360, 765};
    list.shadow(video, context.theme.radius_card, 30.0f, Color::rgb(0x000000, 0.5f));
    list.rounded_rect(video, context.theme.radius_card, Color::rgb(0x05060d));
    // Two bands standing in for a frame: the decode will replace them.
    list.rounded_rect(video.inset(8.0f), context.theme.radius_card - 6.0f,
                      Color::rgb(0x0b1026).with_alpha(0.9f));
    paint.heading("720p60", 816.0f, 492.0f, 40, Color::rgb(0xffffff).with_alpha(0.18f),
                  Align::center);

    // Live badge and viewer count.
    const Rect badge{168, 128, 118, 46};
    list.rounded_rect(badge, 10.0f, context.theme.danger);
    paint.label("LIVE", badge.cx(), 159.0f, 22, Color::rgb(0xffffff), Align::center);
    list.circle(330.0f, 151.0f, 8.0f, context.theme.danger);
    paint.body("1 284 watching", 348.0f, 160.0f, 24, Color::rgb(0xffffff));

    // Who is streaming.
    paint.body("orbit is streaming Homebrew PS5", 136.0f, 908.0f, 28, text);
    paint.label("GO LIVE  /  SCREEN SHARE", 136.0f, 940.0f, 18, muted);

    // Control bar.
    const std::array<std::string_view, 3> controls{{"WATCH", "EXPAND", "LEAVE"}};
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const Rect button{136.0f + static_cast<float>(index) * 300.0f, 976, 268, 72};
        hui::ui::Look look;
        look.focus = index == 0 ? 1.0f : 0.0f;
        paint.button(button, controls[index],
                     index == 0 ? hui::ui::ButtonKind::primary : hui::ui::ButtonKind::secondary,
                     look);
    }

    // Viewers, a short list on the right.
    paint.label("VIEWERS", 1560.0f, 152.0f, 20, muted);
    const std::array<std::string_view, 4> viewers{{"kata", "milosz", "dev-kit", "henny"}};
    float y = 200.0f;
    for (const std::string_view viewer : viewers)
    {
        avatar(list, semibold, 1596.0f, y + 26.0f, 24.0f, name_color(viewer), viewer);
        paint.body(viewer, 1640.0f, y + 36.0f, 26, text);
        y += 72.0f;
    }
    paint.body("+ 1 280 more", 1560.0f, y + 36.0f, 24, muted);
}

} // namespace discord_ps5::ui
