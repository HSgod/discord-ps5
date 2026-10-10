/*
 * Accord - Tests for the shared application identity.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include "core/app_info.hpp"

MICRO_TEST(app_info_reports_product_name)
{
    MICRO_CHECK_EQ(accord::core::app_info().name, std::string_view{"Accord"});
}

MICRO_TEST(app_info_version_is_filled_in)
{
    MICRO_CHECK(!accord::core::app_info().version.empty());
}
