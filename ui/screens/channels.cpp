/*
 * Accord - Channel list, static data.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/screens.hpp"

#include <array>
#include <string_view>

namespace accord::ui
{
namespace
{
struct Row
{
    std::string_view glyph; // "#" text, "*" voice
    std::string_view name;
    bool voice;
    bool unread;
};

constexpr std::array<Row, 9> kRows{{
    {"", "TEXT CHANNELS", false, false},
    {"#", "general", false, false},
    {"#", "announcements", false, true},
    {"#", "homebrew", false, true},
    {"#", "off-topic", false, false},
    {"", "VOICE CHANNELS", false, false},
    {"*", "General", true, false},
    {"*", "PS5 Payload Dev", true, false},
    {"*", "Go Live", true, false},
}};

constexpr std::size_t kSelected = 2;
} // namespace

void draw_channels(hui::gfx::DrawList &list, const ScreenContext &context)
{
    using hui::gfx::Align;
    using hui::gfx::Color;
    using hui::gfx::Rect;

    hui::ui::Painter paint(list, context.fonts, context.theme, context.glass_texture);
    const Color text = context.theme.text;
    const Color muted = context.theme.text_muted;

    // ---- sidebar ----
    const Rect sidebar{0, 0, 460, 1080};
    list.rounded_rect(sidebar, 0.0f, context.theme.surface.with_alpha(0.72f));

    list.rounded_rect({24, 40, 56, 56}, 18.0f, screens_detail::name_color("Homebrew PS5"));
    paint.label("H", 52.0f, 80.0f, 26, Color::rgb(0xffffff), Align::center);
    paint.body("Homebrew PS5", 100.0f, 78.0f, 28, text);
    paint.label("2418 ONLINE", 100.0f, 108.0f, 18, muted);

    float y = 168.0f;
    for (std::size_t index = 0; index < kRows.size(); ++index)
    {
        const Row &row = kRows[index];
        if (row.name == "TEXT CHANNELS" || row.name == "VOICE CHANNELS")
        {
            paint.label(row.name, 28.0f, y + 20.0f, 18, muted);
            y += 52.0f;
            continue;
        }

        const Rect bar{16, y, 428, 62};
        const bool chosen = index == kSelected;
        hui::ui::Look look;
        look.focus = chosen ? 1.0f : 0.0f;
        if (chosen)
        {
            paint.surface(bar, context.theme.radius, context.theme.surface_high, Color{},
                          /*raise=*/1.0f);
        }
        paint.label(row.glyph, 40.0f, y + 40.0f, 26, muted);
        paint.body(row.name, 76.0f, y + 40.0f, 26, chosen ? text : context.theme.text_muted);
        if (row.unread)
            list.circle(416.0f, y + 31.0f, 6.0f, context.theme.accent);
        y += 74.0f;
    }

    // ---- the selected channel's preview ----
    paint.heading("general", 540.0f, 142.0f, 44, text);
    paint.label("TEXT CHANNEL", 540.0f, 174.0f, 18, muted);

    const std::array<std::string_view, 3> preview{{
        "milosz: build went green, the ffpkg mounts on 13.60",
        "kata: nice. which runtime did it pull?",
        "milosz: the crate one, 4b3fbb7",
    }};
    float py = 260.0f;
    for (const std::string_view line : preview)
    {
        paint.body(line, 540.0f, py, 26, context.theme.text_muted);
        py += 44.0f;
    }

    paint.label("X  OPEN   TRIANGLE  VIEW", 540.0f, 1016.0f, 20, muted);
}

} // namespace accord::ui
