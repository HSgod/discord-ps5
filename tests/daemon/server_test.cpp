/*
 * Accord - Tests for the budget one request is read under.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * SO_RCVTIMEO bounds a single recv, and the daemon serves clients from the same
 * thread that feeds audio, so a client dribbling a byte just before every
 * timeout used to hold that thread for minutes. These tests pin the budget that
 * bounds the whole request instead, and the shape of the wait the read loop
 * asks for between bytes.
 */

#include "tests/micro_test.hpp"

#include <cstdint>

#include "daemon/src/server.hpp"

using namespace accord::daemon;

namespace
{
constexpr std::int64_t kBudget = RequestBudget::kBudgetUs;
}

MICRO_TEST(a_fresh_budget_never_waits_for_the_whole_budget_at_once)
{
    const RequestBudget budget{1000};
    MICRO_CHECK_EQ(budget.remaining_us(1000), kBudget);
    MICRO_CHECK(!budget.expired(1000));
    // The step, not the 2 s: a client that went quiet is noticed in half a second.
    MICRO_CHECK_EQ(budget.next_timeout_ms(1000), RequestBudget::kMaxStepMs);
}

MICRO_TEST(the_wait_shrinks_with_what_is_left_of_the_budget)
{
    const RequestBudget budget{0};
    MICRO_CHECK_EQ(budget.next_timeout_ms(1'500'000), RequestBudget::kMaxStepMs);
    MICRO_CHECK_EQ(budget.next_timeout_ms(1'750'000), 250);
    MICRO_CHECK_EQ(budget.remaining_us(1'900'000), 100'000);
}

MICRO_TEST(the_budget_ends_and_stays_ended)
{
    const RequestBudget budget{0};
    MICRO_CHECK(budget.expired(kBudget));
    MICRO_CHECK_EQ(budget.next_timeout_ms(kBudget), 0);
    MICRO_CHECK_EQ(budget.remaining_us(kBudget), 0);

    // A minute later it is still zero rather than negative.
    MICRO_CHECK_EQ(budget.remaining_us(60'000'000), 0);
    MICRO_CHECK_EQ(budget.next_timeout_ms(60'000'000), 0);
    MICRO_CHECK(budget.expired(60'000'000));
}

MICRO_TEST(the_last_sliver_of_the_budget_still_gets_a_wait)
{
    const RequestBudget budget{0};
    MICRO_CHECK(!budget.expired(kBudget - 1));
    MICRO_CHECK_EQ(budget.remaining_us(kBudget - 1), 1);
    // Rounded up: a byte arriving inside the last microsecond still gets read.
    MICRO_CHECK_EQ(budget.next_timeout_ms(kBudget - 1), 1);
}

MICRO_TEST(a_dribbling_client_cannot_hold_the_read_loop)
{
    // The client sends its next byte exactly when the wait runs out, so every
    // recv waits its full step — the worst case for the loop in read_head().
    const RequestBudget budget{0};
    std::int64_t now = 0;
    int waits = 0;
    while (budget.next_timeout_ms(now) > 0)
    {
        now += budget.next_timeout_ms(now) * 1000;
        ++waits;
    }

    MICRO_CHECK_EQ(now, kBudget); // two seconds, not minutes
    MICRO_CHECK_EQ(waits, 4);     // four half-second steps
    MICRO_CHECK(budget.expired(now));
}
