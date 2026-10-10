/*
 * Discord PS5 - Chat view, static data.
 * SPDX-License-Identifier: GPL-3.0-or-later
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

struct Message
{
    std::string_view author;
    std::string_view time;
    std::string_view text;
    bool mention;
};

constexpr std::array<Message, 6> kMessages{{
    {"milosz", "19:02", "build went green - make, make test and make ffpkg all exit 0", false},
    {"kata", "19:04", "nice. which runtime did it pull?", false},
    {"milosz", "19:04", "the crate one, pinned at 4b3fbb7", false},
    {"orbit", "19:07", "on 13.60 only the .ffpkg image starts, the folder build dies in processSpawn", true},
    {"kata", "19:09", "so the console build is finally closed?", false},
    {"milosz", "19:10", "yes - the demo renders and the pad drives it", false},
}};
} // namespace

void draw_chat(hui::gfx::DrawList &list, const ScreenContext &context)
{
    using hui::gfx::Align;
    using hui::gfx::Color;
    using hui::gfx::Rect;

    hui::ui::Painter paint(list, context.fonts, context.theme, context.glass_texture);
    const hui::ui::FontRef &semibold = context.fonts.semibold;
    const Color text = context.theme.text;
    const Color muted = context.theme.text_muted;

    // Top bar.
    list.rounded_rect({0, 0, 1920, 96}, 0.0f, context.theme.surface.with_alpha(0.72f));
    paint.label("#", 48.0f, 62.0f, 30, muted);
    paint.body("general", 84.0f, 62.0f, 30, text);
    paint.body("Homebrew PS5", 360.0f, 62.0f, 24, muted);
    paint.label("2418 ONLINE", 1872.0f, 60.0f, 20, muted, Align::right);

    // Messages.
    float y = 156.0f;
    for (const Message &message : kMessages)
    {
        avatar(list, semibold, 72.0f, y + 20.0f, 26.0f, name_color(message.author), message.author);
        paint.body(message.author, 120.0f, y + 26.0f, 26,
                   message.mention ? context.theme.accent : text);
        paint.label(message.time, 120.0f + semibold.measure(message.author, 26) + 16.0f, y + 24.0f,
                    18, muted);
        // A mention is called out by tinting the whole line; the @name itself
        // will be marked once messages are parsed rather than hard-coded.
        paint.body(message.text, 120.0f, y + 64.0f, 26,
                   message.mention ? context.theme.accent : context.theme.text_muted);
        y += 124.0f;
    }

    // Composer.
    const Rect composer{40, 940, 1840, 92};
    list.rounded_rect(composer, context.theme.radius_card, context.theme.surface_high);
    paint.body("Message #general", 72.0f, 997.0f, 26, muted);
    // The caret the console blinks; frozen bright for the snapshot.
    list.rounded_rect({72.0f + context.fonts.regular.measure("Message #general", 26) + 6.0f, 968,
                       3, 36}, 1.0f, text);
    list.circle(1832.0f, 986.0f, 22.0f, context.theme.primary);
    paint.label("X", 1832.0f, 995.0f, 22, context.theme.on_primary, Align::center);
}

} // namespace discord_ps5::ui
