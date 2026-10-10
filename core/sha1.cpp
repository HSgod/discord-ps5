/*
 * Accord - SHA-1, and only as much of it as the handshake needs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "core/sha1.hpp"

namespace accord::core
{
namespace
{
constexpr std::uint32_t kInitial[5] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u,
                                       0xc3d2e1f0u};

std::uint32_t rotate_left(std::uint32_t value, int bits) noexcept
{
    return (value << bits) | (value >> (32 - bits));
}
} // namespace

Sha1::Sha1() noexcept
{
    for (int i = 0; i < 5; ++i)
        state_[i] = kInitial[i];
}

void Sha1::update(const std::uint8_t *data, std::size_t size) noexcept
{
    length_ += size;

    if (pending_size_ > 0)
    {
        while (size > 0 && pending_size_ < sizeof(pending_))
        {
            pending_[pending_size_++] = *data++;
            --size;
        }
        if (pending_size_ == sizeof(pending_))
        {
            compress(pending_);
            pending_size_ = 0;
        }
    }

    while (size >= sizeof(pending_))
    {
        compress(data);
        data += sizeof(pending_);
        size -= sizeof(pending_);
    }

    while (size > 0)
    {
        pending_[pending_size_++] = *data++;
        --size;
    }
}

void Sha1::update(std::string_view text) noexcept
{
    update(reinterpret_cast<const std::uint8_t *>(text.data()), text.size());
}

void Sha1::compress(const std::uint8_t block[64]) noexcept
{
    std::uint32_t words[80] = {};
    for (int i = 0; i < 16; ++i)
        words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
    for (int i = 16; i < 80; ++i)
        words[i] = rotate_left(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];

    for (int i = 0; i < 80; ++i)
    {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5a827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }

        const std::uint32_t temp = rotate_left(a, 5) + f + e + k + words[i];
        e = d;
        d = c;
        c = rotate_left(b, 30);
        b = a;
        a = temp;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
}

void Sha1::finish(std::uint8_t out[20]) noexcept
{
    // Kept before padding, because update() below counts those bytes too.
    const std::uint64_t bits = length_ * 8;

    const std::uint8_t one = 0x80;
    update(&one, 1);
    const std::uint8_t zero = 0;
    while (pending_size_ != 56)
        update(&zero, 1);

    std::uint8_t tail[8] = {};
    for (int i = 0; i < 8; ++i)
        tail[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    update(tail, sizeof(tail));

    for (int i = 0; i < 5; ++i)
    {
        out[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
}

std::array<std::uint8_t, 20> sha1(std::string_view text) noexcept
{
    Sha1 digest;
    digest.update(text);
    std::array<std::uint8_t, 20> out{};
    digest.finish(out.data());
    return out;
}
} // namespace accord::core
