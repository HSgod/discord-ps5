/*
 * Accord - Tests for the shell's pad navigation.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * These are the tests the console walkthrough cannot be: they press buttons
 * on the actions the entry point hands over, so a mapping mistake shows up
 * here rather than as a wrong screen during a two-minute run on the PS5.
 */

#include "tests/micro_test.hpp"

#include "ui/nav.hpp"

using accord::ui::NavAction;
using accord::ui::NavButton;
using accord::ui::NavState;

namespace
{

NavAction action_of(NavButton button)
{
    return accord::ui::nav_action(button, false);
}

NavAction typed_action_of(NavButton button)
{
    return accord::ui::nav_action(button, true);
}

NavState shell_with(std::size_t screens)
{
    NavState state;
    state.screens = screens;
    return state;
}

} // namespace

MICRO_TEST(arrows_move_between_screens)
{
    MICRO_CHECK(action_of(NavButton::right) == NavAction::next_screen);
    MICRO_CHECK(action_of(NavButton::down) == NavAction::next_screen);
    MICRO_CHECK(action_of(NavButton::left) == NavAction::previous_screen);
    MICRO_CHECK(action_of(NavButton::up) == NavAction::previous_screen);
}

MICRO_TEST(circle_goes_back_and_triangle_opens_the_keyboard)
{
    MICRO_CHECK(action_of(NavButton::circle) == NavAction::previous_screen);
    MICRO_CHECK(action_of(NavButton::triangle) == NavAction::open_keyboard);
    MICRO_CHECK(action_of(NavButton::options) == NavAction::quit);
    MICRO_CHECK(action_of(NavButton::square) == NavAction::none);
    MICRO_CHECK(action_of(NavButton::none) == NavAction::none);
}

MICRO_TEST(screen_changes_wrap_around_the_list)
{
    NavState state = shell_with(3);

    accord::ui::nav_apply(state, NavAction::next_screen);
    MICRO_CHECK_EQ(state.screen, 1u);
    accord::ui::nav_apply(state, NavAction::next_screen);
    MICRO_CHECK_EQ(state.screen, 2u);
    accord::ui::nav_apply(state, NavAction::next_screen);
    MICRO_CHECK_EQ(state.screen, 0u);

    accord::ui::nav_apply(state, NavAction::previous_screen);
    MICRO_CHECK_EQ(state.screen, 2u);
    accord::ui::nav_apply(state, NavAction::previous_screen);
    MICRO_CHECK_EQ(state.screen, 1u);
}

MICRO_TEST(a_shell_without_screens_ignores_movement)
{
    NavState state = shell_with(0);

    accord::ui::nav_apply(state, NavAction::next_screen);
    accord::ui::nav_apply(state, NavAction::previous_screen);

    MICRO_CHECK_EQ(state.screen, 0u);
}

MICRO_TEST(opening_and_closing_the_keyboard_follows_the_action)
{
    NavState state = shell_with(6);

    accord::ui::nav_apply(state, NavAction::open_keyboard);
    MICRO_CHECK(state.typing);
    accord::ui::nav_apply(state, NavAction::close_keyboard);
    MICRO_CHECK(!state.typing);
    MICRO_CHECK(!state.quitting);
}

/* Options cannot reach the shell while the keyboard is up (see the test
 * above), so quitting is only ever applied to a shell that is not typing. */
MICRO_TEST(quit_marks_the_shell_for_shutdown_and_leaves_the_screen_alone)
{
    NavState state = shell_with(6);

    accord::ui::nav_apply(state, NavAction::quit);

    MICRO_CHECK(state.quitting);
    MICRO_CHECK(!state.typing);
    MICRO_CHECK_EQ(state.screen, 0u);
}

MICRO_TEST(only_circle_reaches_the_shell_while_typing)
{
    MICRO_CHECK(typed_action_of(NavButton::circle) == NavAction::close_keyboard);
    MICRO_CHECK(typed_action_of(NavButton::right) == NavAction::none);
    MICRO_CHECK(typed_action_of(NavButton::triangle) == NavAction::none);
    MICRO_CHECK(typed_action_of(NavButton::options) == NavAction::none);
}

MICRO_TEST(typing_returns_the_input_to_the_screens)
{
    NavState state = shell_with(6);
    state.screen = 2;
    state.typing = true;

    accord::ui::nav_apply(state, accord::ui::nav_action(NavButton::circle, state.typing));
    MICRO_CHECK(!state.typing);
    MICRO_CHECK_EQ(state.screen, 2u);

    /* The next press walks screens again: closing the keyboard only takes the
     * input back, it does not also count as back. */
    accord::ui::nav_apply(state, accord::ui::nav_action(NavButton::right, state.typing));
    MICRO_CHECK_EQ(state.screen, 3u);
}
