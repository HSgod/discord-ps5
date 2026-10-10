/*
 * Accord - Tests for the SHA-1 the handshake needs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/sha1.hpp"

using namespace accord::core;

namespace
{
std::string to_hex(const std::array<std::uint8_t, 20> &digest)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(40);
    for (const std::uint8_t byte : digest)
    {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
    }
    return out;
}

std::string digest_of(std::string_view text)
{
    return to_hex(sha1(text));
}
} // namespace

MICRO_TEST(the_published_vectors_come_out_right)
{
    // FIPS 180-1's own two examples and the classic sentence.
    MICRO_CHECK_EQ(digest_of(""), std::string{"da39a3ee5e6b4b0d3255bfef95601890afd80709"});
    MICRO_CHECK_EQ(digest_of("abc"), std::string{"a9993e364706816aba3e25717850c26c9cd0d89d"});
    MICRO_CHECK_EQ(
        digest_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        std::string{"84983e441c3bd26ebaae4aa1f95129e5e54670f1"});
    MICRO_CHECK_EQ(digest_of("The quick brown fox jumps over the lazy dog"),
                   std::string{"2fd4e1c67a2d28fced849ee1bb76e7391b93eb12"});
}

MICRO_TEST(a_million_bytes_do_not_lose_the_length_count)
{
    // The vector from the standard: one million 'a', where the length field is
    // the only thing that grew past 32 bits of counting.
    std::string block(1000, 'a');
    Sha1 digest;
    for (int i = 0; i < 1000; ++i)
        digest.update(block);
    std::array<std::uint8_t, 20> sum{};
    digest.finish(sum.data());
    MICRO_CHECK_EQ(to_hex(sum), std::string{"34aa973cd4c4daa4f61eeb2bdbad27316534016f"});
}

MICRO_TEST(the_padding_holds_at_every_block_boundary)
{
    // Sizes around 55, 56, 63, 64, 120 and 128, where the padding length itself
    // decides whether another block is needed. The digests come from the host's
    // own sha1, an implementation that shares nothing with this one.
    struct Case
    {
        std::size_t size;
        const char *digest;
    };
    const Case cases[] = {
        {0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {1, "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8"},
        {54, "b05d71c64979cb95fa74a33cdb31a40d258ae02e"},
        {55, "c1c8bbdc22796e28c0e15163d20899b65621d65a"},
        {56, "c2db330f6083854c99d4b5bfb6e8f29f201be699"},
        {57, "f08f24908d682555111be7ff6f004e78283d989a"},
        {63, "03f09f5b158a7a8cdad920bddc29b81c18a551f5"},
        {64, "0098ba824b5c16427bd7a1122a5a442a25ec644d"},
        {65, "11655326c708d70319be2610e8a57d9a5b959d3b"},
        {100, "7f9000257a4918d7072655ea468540cdcbd42e0c"},
        {119, "ee971065aaa017e0632a8ca6c77bb3bf8b1dfc56"},
        {120, "f34c1488385346a55709ba056ddd08280dd4c6d6"},
        {127, "89d95fa32ed44a7c610b7ee38517ddf57e0bb975"},
        {128, "ad5b3fdbcb526778c2839d2f151ea753995e26a0"},
        {1000, "291e9a6c66994949b57ba5e650361e98fc36b1ba"},
    };

    for (const Case &test : cases)
    {
        const std::string text(test.size, 'a');
        MICRO_CHECK_EQ(digest_of(text), std::string{test.digest});
    }
}

MICRO_TEST(updating_in_pieces_gives_the_same_digest)
{
    const std::string text =
        "the same bytes, cut differently, have to give the same digest every time, "
        "whatever the size of the pieces they arrive in";

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{3}, std::size_t{7},
                                    std::size_t{63}, std::size_t{64}, std::size_t{65},
                                    std::size_t{128}})
    {
        Sha1 digest;
        for (std::size_t at = 0; at < text.size(); at += chunk)
        {
            const std::size_t take = std::min(chunk, text.size() - at);
            digest.update(std::string_view{text}.substr(at, take));
        }
        std::array<std::uint8_t, 20> sum{};
        digest.finish(sum.data());
        MICRO_CHECK_EQ(to_hex(sum), digest_of(text));
    }
}
