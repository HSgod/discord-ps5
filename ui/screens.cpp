/*
 * Discord PS5 - The screen registry.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/screens.hpp"

namespace discord_ps5::ui
{

std::span<const Screen> screens() noexcept
{
    static constexpr Screen kScreens[] = {
        {"01-login", "Log in (QR code)", draw_login},
        {"02-servers", "Server list", draw_servers},
        {"03-channels", "Channel list", draw_channels},
        {"04-chat", "Chat view", draw_chat},
        {"05-voice", "Voice panel", draw_voice},
        {"06-stream", "Stream player", draw_stream},
    };
    return kScreens;
}

} // namespace discord_ps5::ui
