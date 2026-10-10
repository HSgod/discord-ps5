/*
 * Accord - what the pad does outside a screen.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shell's navigation is the one piece of input handling that is worth a
 * test, and it cannot be tested while it lives in the console entry point,
 * where reading the pad needs the kit and the SDK. So the buttons the shell
 * cares about, the action each one means, and the state those actions move
 * are all here, free of both: the entry point translates a pad frame into a
 * NavButton (platform/ps5/main.cpp) and applies the result to a NavState,
 * and the host tests (tests/ui/nav_test.cpp) drive the same functions.
 *
 * What a screen does with a confirmed button -- opening a server, sending a
 * message -- is the screen's business, not the shell's, and is not here.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace accord::ui
{

/* The pad as the shell sees it: the kit's logical actions reduced to the ones
 * that mean something between screens. */
enum class NavButton : std::uint8_t
{
    none,
    up,
    down,
    left,
    right,
    cross,
    circle,
    triangle,
    square,
    options,
    touch,
};

/* What a button asks the shell to do. */
enum class NavAction : std::uint8_t
{
    none,
    next_screen,
    previous_screen,
    open_keyboard,
    close_keyboard,
    quit,
};

/* Which screen is up, and who owns the input: the shell, or the on-screen
 * keyboard. screens == 0 is a shell with nothing to show, and is ignored
 * rather than divided by. */
struct NavState
{
    std::size_t screens = 0;
    std::size_t screen = 0;
    bool typing = false;
    bool quitting = false;
};

/* The action a button means, given whether the keyboard has the input. While
 * it does, the keyboard itself turns buttons into characters, and only Circle
 * still means anything to the shell -- it puts the keyboard away. */
NavAction nav_action(NavButton button, bool typing) noexcept;

/* Moves the state by one action. Screen changes wrap around, so the shell
 * never runs off either end of the list. */
void nav_apply(NavState &state, NavAction action) noexcept;

} // namespace accord::ui
