/*
 * Discord PS5 - Tests for the shared application identity.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include "core/app_info.hpp"

MICRO_TEST(app_info_reports_product_name)
{
    MICRO_CHECK_EQ(discord_ps5::core::app_info().name, std::string_view{"Discord PS5"});
}

MICRO_TEST(app_info_version_is_filled_in)
{
    MICRO_CHECK(!discord_ps5::core::app_info().version.empty());
}
