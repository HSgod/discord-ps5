/*
 * Discord PS5 - Server list, static data.
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

struct Server
{
    std::string_view initial;
    std::string_view name;
    int members;
};

constexpr std::array<Server, 6> kServers{{
    {"H", "Homebrew PS5", 2418},
    {"O", "Orbit Store", 903},
    {"H", "HEN Central", 15604},
    {"F", "Final Fantasy XIV", 88231},
    {"D", "Discord Developers", 202415},
    {"P", "PacBrew", 611},
}};

constexpr std::size_t kSelected = 0;
} // namespace

void draw_servers(hui::gfx::DrawList &list, const ScreenContext &context)
{
    using hui::gfx::Align;
    using hui::gfx::Color;
    using hui::gfx::Rect;

    hui::ui::Painter paint(list, context.fonts, context.theme, context.glass_texture);
    const hui::ui::FontRef &semibold = context.fonts.semibold;
    const Color text = context.theme.text;
    const Color muted = context.theme.text_muted;

    // ---- the rail ----
    const Rect rail{0, 0, 104, 1080};
    list.rounded_rect(rail, 0.0f, context.theme.surface.with_alpha(0.72f));

    list.rounded_rect({24, 28, 56, 56}, 18.0f, context.theme.primary);
    paint.label("HOME", 52.0f, 68.0f, 13, context.theme.on_primary, Align::center);
    list.rounded_rect({40, 108, 24, 4}, 2.0f, context.theme.surface_high);

    for (std::size_t index = 0; index < kServers.size(); ++index)
    {
        const float cy = 176.0f + static_cast<float>(index) * 96.0f;
        const bool chosen = index == kSelected;
        // The selected server gets the pill marker on the left and a brightened
        // disc, exactly the cue Discord's own client uses.
        if (chosen)
            list.rounded_rect({4, cy - 26, 10, 52}, 5.0f, context.theme.text);
        const Color base = name_color(kServers[index].name);
        avatar(list, semibold, 52.0f, cy, chosen ? 34.0f : 30.0f,
               chosen ? base : base.with_alpha(0.75f), kServers[index].initial);
    }

    // ---- the list ----
    paint.heading("Servers", 168.0f, 142.0f, 46, text);
    paint.label("6 JOINED", 1752.0f, 138.0f, 20, muted, Align::right);

    float y = 200.0f;
    for (std::size_t index = 0; index < kServers.size(); ++index)
    {
        const Rect row{144, y, 1632, 96};
        const bool chosen = index == kSelected;
        hui::ui::Look look;
        look.focus = chosen ? 1.0f : 0.0f;
        if (chosen)
            paint.focus_ring(row, context.theme.radius_card, 1.0f);

        avatar(list, semibold, 200.0f, y + 48.0f, 30.0f, name_color(kServers[index].name),
               kServers[index].initial);
        paint.body(kServers[index].name, 252.0f, y + 56.0f, 30,
                   chosen ? text : context.theme.text_muted);
        paint.label(std::to_string(kServers[index].members) + " MEMBERS", 1740.0f, y + 54.0f, 20,
                    muted, Align::right);
        y += 116.0f;
    }

    paint.label("X  OPEN   O  MARK READ", 168.0f, 1016.0f, 20, muted);
}

} // namespace discord_ps5::ui
