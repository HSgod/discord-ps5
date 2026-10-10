/*
 * Accord - Tests for the base64 the handshake needs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tests/micro_test.hpp"

#include <cstdint>
#include <string>

#include "core/base64.hpp"

using namespace accord::core;

MICRO_TEST(the_rfc_4648_vectors_come_out_right)
{
    // The examples in the standard, which is what every other implementation is
    // checked against.
    MICRO_CHECK_EQ(base64_encode(""), std::string{""});
    MICRO_CHECK_EQ(base64_encode("f"), std::string{"Zg=="});
    MICRO_CHECK_EQ(base64_encode("fo"), std::string{"Zm8="});
    MICRO_CHECK_EQ(base64_encode("foo"), std::string{"Zm9v"});
    MICRO_CHECK_EQ(base64_encode("foob"), std::string{"Zm9vYg=="});
    MICRO_CHECK_EQ(base64_encode("fooba"), std::string{"Zm9vYmE="});
    MICRO_CHECK_EQ(base64_encode("foobar"), std::string{"Zm9vYmFy"});
}

MICRO_TEST(bytes_that_are_not_text_survive_the_encoding)
{
    // The handshake key is 16 random bytes, not a string. These cases reach the
    // last two characters of the standard alphabet -- '+' and '/' -- and both
    // kinds of padding; the values come from the host's own base64.
    const std::uint8_t digest[20] = {0xf7, 0xbf, 0xfb, 0xff, 0x3f, 0xff, 0xef, 0x7e, 0x01, 0x00,
                                     0x00, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe};
    MICRO_CHECK_EQ(base64_encode(digest, sizeof(digest)), std::string{"97/7/z//734BAAA///////////4="});

    const std::uint8_t counting[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    MICRO_CHECK_EQ(base64_encode(counting, sizeof(counting)), std::string{"AAECAwQFBgcICQoLDA0ODw=="});

    const std::uint8_t plus[3] = {0xfb, 0xef, 0xbe};
    MICRO_CHECK_EQ(base64_encode(plus, sizeof(plus)), std::string{"++++"});

    const std::uint8_t one_byte[1] = {0x3e};
    MICRO_CHECK_EQ(base64_encode(one_byte, sizeof(one_byte)), std::string{"Pg=="});
}

MICRO_TEST(the_length_of_the_output_follows_the_padding_rules)
{
    for (std::size_t size = 0; size <= 40; ++size)
    {
        const std::string text(size, 'x');
        const std::string encoded = base64_encode(text);
        MICRO_CHECK_EQ(encoded.size(), ((size + 2) / 3) * 4);
        if (size % 3 != 0)
            MICRO_CHECK(encoded.back() == '=');
    }
}
