/*
 * Accord - The six static screens and the context they draw in.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every screen is a plain function over a draw list. No screen owns a GL
 * context, reads the clock or touches a controller: the host snapshot runner
 * and (later) the console loop both call the same functions, so a PNG made on
 * the PC is what the console draws.
 *
 * The drawing itself is ps5-homebrew-ui's kit, vendored into ui/kit/ (see
 * tools/vendor-kit.sh). Its headers are prefixed with "ui/kit/" so they cannot
 * collide with ours on the include path.
 */

#pragma once

#include "ui/kit/gfx/draw_list.hpp"
#include "ui/kit/ui/fonts.hpp"
#include "ui/kit/ui/theme.hpp"
#include "ui/kit/ui/widgets.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace accord::ui
{
// What a screen needs to paint one frame. The glass texture is the kit's
// blurred copy of the frame behind; a screen that draws no glass may get 0.
struct ScreenContext
{
    const hui::ui::Fonts &fonts;
    const hui::ui::Theme &theme;
    std::uint32_t glass_texture = 0;
    float time = 0.0f;
};

using ScreenDraw = void (*)(hui::gfx::DrawList &list, const ScreenContext &context);

struct Screen
{
    const char *id;    // file-name friendly; the host runner names the PNG after it
    const char *title; // one line for the snapshot log
    ScreenDraw draw;
};

// In the order the screens are reached in the app.
std::span<const Screen> screens() noexcept;

// The six screens. Data is hard-coded for now.
void draw_login(hui::gfx::DrawList &list, const ScreenContext &context);
void draw_servers(hui::gfx::DrawList &list, const ScreenContext &context);
void draw_channels(hui::gfx::DrawList &list, const ScreenContext &context);
void draw_chat(hui::gfx::DrawList &list, const ScreenContext &context);
void draw_voice(hui::gfx::DrawList &list, const ScreenContext &context);
void draw_stream(hui::gfx::DrawList &list, const ScreenContext &context);

namespace screens_detail
{
// A filled disc with a centred initial: a stand-in for an avatar image, which
// the kit has no loader for yet. Colour is caller-chosen so each name keeps
// the same one across screens.
inline void avatar(hui::gfx::DrawList &list, const hui::ui::FontRef &font, float cx, float cy,
                   float radius, hui::gfx::Color color, std::string_view initial)
{
    list.circle(cx, cy, radius, color);
    if (!initial.empty())
    {
        list.text(*font.font, font.texture, initial.substr(0, 1), cx, cy + radius * 0.34f,
                  radius * 0.98f, hui::gfx::Color::rgb(0xffffff), hui::gfx::Align::center);
    }
}

// A stable colour per name, so the same handle is the same hue on every screen.
// These six are Accord's own: muted, mid-luminance hues, chosen to carry a white
// initial and to stay clear of any other client's brand colours.
inline hui::gfx::Color name_color(std::string_view name)
{
    std::uint32_t hash = 2166136261u;
    for (const char character : name)
    {
        hash = (hash ^ static_cast<std::uint8_t>(character)) * 16777619u;
    }
    const std::uint32_t palette[] = {0x2f8f7a, 0x4a7fb5, 0x7b5ea7, 0xb0567a, 0xbf7b3f, 0x6f8f3f};
    return hui::gfx::Color::rgb(palette[hash % 6]);
}
} // namespace screens_detail
} // namespace accord::ui
