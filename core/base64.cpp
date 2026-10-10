/*
 * Accord - Base64, encoding only.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/base64.hpp"

namespace accord::core
{
namespace
{
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

std::string base64_encode(const std::uint8_t *data, std::size_t size)
{
    std::string out;
    out.reserve(((size + 2) / 3) * 4);

    std::size_t at = 0;
    while (at + 3 <= size)
    {
        const std::uint32_t group = (static_cast<std::uint32_t>(data[at]) << 16) |
                                    (static_cast<std::uint32_t>(data[at + 1]) << 8) |
                                    static_cast<std::uint32_t>(data[at + 2]);
        out.push_back(kAlphabet[(group >> 18) & 0x3f]);
        out.push_back(kAlphabet[(group >> 12) & 0x3f]);
        out.push_back(kAlphabet[(group >> 6) & 0x3f]);
        out.push_back(kAlphabet[group & 0x3f]);
        at += 3;
    }

    const std::size_t left = size - at;
    if (left == 1)
    {
        const std::uint32_t group = static_cast<std::uint32_t>(data[at]) << 16;
        out.push_back(kAlphabet[(group >> 18) & 0x3f]);
        out.push_back(kAlphabet[(group >> 12) & 0x3f]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (left == 2)
    {
        const std::uint32_t group = (static_cast<std::uint32_t>(data[at]) << 16) |
                                    (static_cast<std::uint32_t>(data[at + 1]) << 8);
        out.push_back(kAlphabet[(group >> 18) & 0x3f]);
        out.push_back(kAlphabet[(group >> 12) & 0x3f]);
        out.push_back(kAlphabet[(group >> 6) & 0x3f]);
        out.push_back('=');
    }

    return out;
}

std::string base64_encode(std::string_view text)
{
    return base64_encode(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}
} // namespace accord::core
