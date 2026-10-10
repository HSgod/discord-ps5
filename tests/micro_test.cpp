/*
 * Accord - Minimal self-registering test harness.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include <cstdio>
#include <exception>

namespace micro_test
{
std::vector<TestCase> &registry() noexcept
{
    static std::vector<TestCase> cases;
    return cases;
}

Registrar::Registrar(std::string name, TestFn fn)
{
    registry().push_back(TestCase{std::move(name), fn});
}

void record_failure(const char *file, int line, const std::string &detail)
{
    throw Failure{std::string{file} + ":" + std::to_string(line) + ": " + detail};
}

int run_all()
{
    std::size_t failures = 0;
    for (const TestCase &test : registry())
    {
        try
        {
            test.fn();
            std::printf("  [ ok ] %s\n", test.name.c_str());
        }
        catch (const Failure &failure)
        {
            ++failures;
            std::printf("  [FAIL] %s\n         %s\n", test.name.c_str(), failure.message().c_str());
        }
        catch (const std::exception &error)
        {
            ++failures;
            std::printf("  [FAIL] %s\n         unexpected exception: %s\n", test.name.c_str(),
                        error.what());
        }
    }

    std::printf("%zu test(s), %zu failure(s)\n", registry().size(), failures);
    return failures == 0 ? 0 : 1;
}
} // namespace micro_test
