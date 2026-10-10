/*
 * Accord - Tests for the logging seam.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include "core/log.hpp"
#include "platform/host/host_stub.hpp"

namespace
{
// Installs the capturing sink for the duration of a test and restores the
// silent default afterwards, even when a check throws.
struct CapturingSink
{
    CapturingSink()
    {
        accord::host::clear_captured_log();
        accord::host::install_capturing_log_sink();
    }

    ~CapturingSink()
    {
        accord::core::set_log_sink(nullptr);
    }
};
} // namespace

MICRO_TEST(log_levels_have_names)
{
    using accord::core::LogLevel;
    MICRO_CHECK_EQ(accord::core::to_string(LogLevel::info), std::string_view{"info"});
    MICRO_CHECK_EQ(accord::core::to_string(LogLevel::warn), std::string_view{"warn"});
    MICRO_CHECK_EQ(accord::core::to_string(LogLevel::error), std::string_view{"error"});
}

MICRO_TEST(logging_without_a_sink_is_silent)
{
    accord::core::set_log_sink(nullptr);
    accord::core::log(accord::core::LogLevel::info, "dropped");
    MICRO_CHECK(accord::core::log_sink() == nullptr);
}

MICRO_TEST(installed_sink_captures_lines_in_order)
{
    CapturingSink sink;
    accord::core::log(accord::core::LogLevel::info, "first");
    accord::core::log(accord::core::LogLevel::error, "second");
    MICRO_CHECK_EQ(accord::host::captured_text(), std::string{"first\nsecond"});
}
