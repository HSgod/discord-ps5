/*
 * Accord - Minimal self-registering test harness.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A micro-harness, not a framework. Keeping this in-tree
 * avoids a fetched dependency for a skeleton that only exercises pure logic:
 *
 *   MICRO_TEST(adding_works)
 *   {
 *       MICRO_CHECK_EQ(2 + 2, 4);
 *   }
 *
 * A failed check throws, the runner records it and continues with the next test.
 */

#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace micro_test
{
using TestFn = void (*)();

struct TestCase
{
    std::string name;
    TestFn fn;
};

class Failure
{
  public:
    explicit Failure(std::string message) : message_{std::move(message)}
    {
    }

    const std::string &message() const noexcept
    {
        return message_;
    }

  private:
    std::string message_;
};

std::vector<TestCase> &registry() noexcept;
void record_failure(const char *file, int line, const std::string &detail);
int run_all();

struct Registrar
{
    Registrar(std::string name, TestFn fn);
};

// Formatting helpers for check_equal(); add an overload when a new type shows up.
inline std::string to_text(std::string_view value)
{
    return std::string{value};
}

inline std::string to_text(const std::string &value)
{
    return value;
}

inline std::string to_text(const char *value)
{
    return value == nullptr ? std::string{"(null)"} : std::string{value};
}

template <typename T>
    requires std::is_arithmetic_v<T>
std::string to_text(T value)
{
    return std::to_string(value);
}

template <typename Actual, typename Expected>
void check_equal(const char *file, int line, const char *expression, const Actual &actual,
                 const Expected &expected)
{
    if (!(actual == expected))
    {
        record_failure(file, line, std::string{expression} + ": expected " + to_text(expected) +
                                      ", got " + to_text(actual));
    }
}
} // namespace micro_test

#define MICRO_TEST(test_name)                                                                    \
    static void test_name();                                                                     \
    static const ::micro_test::Registrar test_name##_registrar{#test_name, &test_name};          \
    static void test_name()

#define MICRO_CHECK(expression)                                                                  \
    do                                                                                           \
    {                                                                                            \
        if (!(expression))                                                                       \
            ::micro_test::record_failure(__FILE__, __LINE__, "expected: " #expression);           \
    } while (false)

#define MICRO_CHECK_EQ(actual, expected)                                                         \
    ::micro_test::check_equal(__FILE__, __LINE__, #actual, (actual), (expected))
