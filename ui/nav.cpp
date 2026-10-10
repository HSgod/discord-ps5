/*
 * Accord - what the pad does outside a screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ui/nav.hpp"

namespace accord::ui
{

NavAction nav_action(NavButton button, bool typing) noexcept
{
    if (typing)
        return button == NavButton::circle ? NavAction::close_keyboard : NavAction::none;

    switch (button)
    {
    case NavButton::left:
    case NavButton::up:
        return NavAction::previous_screen;
    case NavButton::right:
    case NavButton::down:
        /* Cross walks the list too, for now: no screen has anything of its
         * own to confirm until the server list arrives. */
    case NavButton::cross:
        return NavAction::next_screen;
    case NavButton::circle:
        return NavAction::previous_screen;
    case NavButton::triangle:
        return NavAction::open_keyboard;
    case NavButton::options:
        return NavAction::quit;
    case NavButton::square:
    case NavButton::touch:
    case NavButton::none:
        return NavAction::none;
    }
    return NavAction::none;
}

void nav_apply(NavState &state, NavAction action) noexcept
{
    switch (action)
    {
    case NavAction::next_screen:
        if (state.screens != 0)
            state.screen = (state.screen + 1) % state.screens;
        break;
    case NavAction::previous_screen:
        if (state.screens != 0)
            state.screen = (state.screen + state.screens - 1) % state.screens;
        break;
    case NavAction::open_keyboard:
        state.typing = true;
        break;
    case NavAction::close_keyboard:
        state.typing = false;
        break;
    case NavAction::quit:
        state.quitting = true;
        break;
    case NavAction::none:
        break;
    }
}

} // namespace accord::ui
