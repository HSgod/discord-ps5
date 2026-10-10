/*
 * Discord PS5 - Voice panel, static data.
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

struct Participant
{
    std::string_view name;
    bool speaking;
    bool muted;
};

constexpr std::array<Participant, 4> kParticipants{{
    {"milosz", true, false},
    {"kata", false, false},
    {"orbit", false, true},
    {"dev-kit", false, false},
}};
} // namespace

void draw_voice(hui::gfx::DrawList &list, const ScreenContext &context)
{
    using hui::gfx::Align;
    using hui::gfx::Color;
    using hui::gfx::Rect;

    hui::ui::Painter paint(list, context.fonts, context.theme, context.glass_texture);
    const hui::ui::FontRef &semibold = context.fonts.semibold;
    const Color text = context.theme.text;
    const Color muted = context.theme.text_muted;

    // Two columns: the channel card on the left, who is in it on the right.
    const Rect card{96, 120, 760, 720};
    paint.panel(card);

    paint.label("VOICE CONNECTED", 144.0f, 190.0f, 20, context.theme.success);
    paint.heading("General", 144.0f, 264.0f, 52, text);
    paint.body("Homebrew PS5  /  Voice Channels", 144.0f, 306.0f, 24, muted);

    // A live level meter, the one piece of motion on this screen.
    const Rect meter{144, 360, 664, 18};
    paint.well(meter, 9.0f, context.theme.surface_high);
    const float level = 0.35f + 0.25f * (0.5f + 0.5f * context.time);
    paint.progress(meter, level > 1.0f ? 1.0f : level);

    // Participants, each a row with a speaking ring.
    float y = 430.0f;
    for (const Participant &participant : kParticipants)
    {
        if (participant.speaking && participant.muted == false)
            list.ring(200.0f, y + 34.0f, 38.0f, 4.0f, context.theme.success);
        avatar(list, semibold, 200.0f, y + 34.0f, 32.0f, name_color(participant.name),
               participant.name);
        paint.body(participant.name, 256.0f, y + 44.0f, 28,
                   participant.muted ? muted : text);
        paint.label(participant.muted ? "MUTED" : "CONNECTED", 800.0f, y + 42.0f, 18, muted,
                    Align::right);
        y += 84.0f;
    }

    // Controls.
    const Rect bar{96, 900, 760, 108};
    paint.panel(bar);
    const std::array<std::string_view, 3> controls{{"MIC", "SOUND", "LEAVE"}};
    for (std::size_t index = 0; index < controls.size(); ++index)
    {
        const Rect button{132.0f + static_cast<float>(index) * 236.0f, 922, 204, 64};
        hui::ui::Look look;
        look.focus = index == 0 ? 1.0f : 0.0f;
        paint.button(button, controls[index],
                     index == 2 ? hui::ui::ButtonKind::primary : hui::ui::ButtonKind::secondary,
                     look);
    }

    paint.label("TRIANGLE  LEAVE   SQUARE  AUDIO   X  MIC", 96.0f, 1046.0f, 20, muted);

    // Right column: the same participants as a speaking grid.
    paint.label("IN THIS CHANNEL", 960.0f, 190.0f, 20, muted);
    for (std::size_t index = 0; index < kParticipants.size(); ++index)
    {
        const float cx = 1104.0f + static_cast<float>(index % 2) * 440.0f;
        const float cy = 400.0f + static_cast<float>(index / 2) * 440.0f;
        const Rect tile{cx - 180.0f, cy - 180.0f, 360.0f, 360.0f};
        list.rounded_rect(tile, context.theme.radius_card, context.theme.surface.with_alpha(0.6f));
        if (kParticipants[index].speaking)
            paint.focus_ring(tile, context.theme.radius_card, 1.0f);
        avatar(list, semibold, cx, cy - 20.0f, 70.0f, name_color(kParticipants[index].name),
               kParticipants[index].name);
        paint.body(kParticipants[index].name, cx, cy + 116.0f, 28, text, Align::center);
        if (kParticipants[index].muted)
            paint.label("MUTED", cx, cy + 148.0f, 18, context.theme.danger, Align::center);
    }
}

} // namespace discord_ps5::ui
