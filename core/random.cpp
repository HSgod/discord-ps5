/*
 * Accord - The system's random bytes, behind one function.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/random.hpp"

#include <cstdio>

namespace accord::core
{
bool fill_random(std::uint8_t *buffer, std::size_t size) noexcept
{
    std::FILE *source = std::fopen("/dev/urandom", "rb");
    if (source == nullptr)
        return false;

    std::size_t got = 0;
    while (got < size)
    {
        const std::size_t chunk = std::fread(buffer + got, 1, size - got, source);
        if (chunk == 0)
        {
            std::fclose(source);
            return false;
        }
        got += chunk;
    }

    std::fclose(source);
    return true;
}
} // namespace accord::core
