/*
 * Accord - Tests for the JSON reader.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The five Gateway events in tests/fixtures are the shapes T4.2 will have to
 * read (HELLO, READY, MESSAGE_CREATE, Heartbeat ACK, Invalid Session), written
 * out by hand from the documented payloads. What is checked here is what the
 * reader promises: a bad document comes back as a value, text arrives
 * unescaped, and a snowflake keeps every one of its digits.
 */

#include "tests/micro_test.hpp"

#include "core/json.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using accord::core::json::Document;
using accord::core::json::Value;

namespace
{

// The tests run from the repository root, so the fixtures are where the tree
// keeps them.
std::string read_fixture(std::string_view name)
{
    std::ifstream file{std::string{"tests/fixtures/"} + std::string{name},
                       std::ios::binary};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Document parse_fixture(std::string_view name)
{
    const std::string text = read_fixture(name);
    MICRO_CHECK(!text.empty());
    return Document::parse(text);
}

} // namespace

MICRO_TEST(fixtures_are_found_and_parse)
{
    for (const char *name : {"gateway-hello.json", "gateway-ready.json",
                             "gateway-message-create.json", "gateway-heartbeat-ack.json",
                             "gateway-invalid-session.json"})
    {
        const Document document = parse_fixture(name);
        MICRO_CHECK(document.ok());
        MICRO_CHECK(document.root().is_object());
    }
}

MICRO_TEST(hello_carries_the_heartbeat_interval)
{
    const Document document = parse_fixture("gateway-hello.json");
    MICRO_CHECK(document.ok());

    const Value root = document.root();
    std::uint64_t opcode = 0;
    MICRO_CHECK(root.member("op").u64(&opcode));
    MICRO_CHECK_EQ(opcode, 10u);

    std::uint64_t interval = 0;
    MICRO_CHECK(root.member("d").member("heartbeat_interval").u64(&interval));
    MICRO_CHECK_EQ(interval, 41250u);
}

MICRO_TEST(ready_gives_the_session_and_the_resume_url)
{
    const Document document = parse_fixture("gateway-ready.json");
    MICRO_CHECK(document.ok());

    const Value payload = document.root().member("d");
    MICRO_CHECK_EQ(payload.member("session_id").text(),
                   std::string_view{"8f3a1c90-1f5d-4a1f-9c2f-6b9d2f0c4e77"});
    MICRO_CHECK_EQ(payload.member("resume_gateway_url").text(),
                   std::string_view{"wss://gateway-us-east1-b.discord.gg"});
    MICRO_CHECK_EQ(payload.member("user").member("username").text(), std::string_view{"accord"});

    // Ids come as strings on the wire, and stay exact as strings.
    MICRO_CHECK_EQ(payload.member("user").member("id").text(),
                   std::string_view{"80351110224678912"});

    // guilds: two entries, the first one named null rather than missing.
    const Value guilds = payload.member("guilds");
    MICRO_CHECK(guilds.is_array());
    MICRO_CHECK_EQ(guilds.size(), 2u);
    MICRO_CHECK_EQ(guilds.element(0).member("id").text(), std::string_view{"41771983423143937"});

    bool unavailable = false;
    MICRO_CHECK(guilds.element(0).member("unavailable").boolean(&unavailable));
    MICRO_CHECK(unavailable);
    MICRO_CHECK(guilds.element(0).member("name").is_null());
    MICRO_CHECK(!guilds.element(1).member("name").exists());

    bool verified = false;
    MICRO_CHECK(payload.member("user").member("verified").boolean(&verified));
    MICRO_CHECK(verified);
    MICRO_CHECK(payload.member("user").member("bot").is_bool());
}

MICRO_TEST(message_create_text_is_unescaped)
{
    const Document document = parse_fixture("gateway-message-create.json");
    MICRO_CHECK(document.ok());

    const Value message = document.root().member("d");
    // The fixture is a message with sample text, and the parser has to decode
    // all four escapes in it: the quoted name, the em dash, the two-byte letter
    // and the surrogate pair at the end.
    MICRO_CHECK_EQ(message.member("content").text(),
                   std::string_view{"hej \"Accord\" \xe2\x80\x94 idziemy na g\xc5\x82os? \xf0\x9f\x8e\xb5"});
    MICRO_CHECK_EQ(message.member("author").member("username").text(), std::string_view{"milosz"});
    MICRO_CHECK(message.member("author").member("global_name").is_null());
    MICRO_CHECK_EQ(message.member("mentions").size(), 0u);

    bool tts = true;
    MICRO_CHECK(message.member("tts").boolean(&tts));
    MICRO_CHECK(!tts);
}

MICRO_TEST(heartbeat_ack_and_invalid_session_parse)
{
    const Document ack = parse_fixture("gateway-heartbeat-ack.json");
    MICRO_CHECK(ack.ok());
    std::uint64_t opcode = 0;
    MICRO_CHECK(ack.root().member("op").u64(&opcode));
    MICRO_CHECK_EQ(opcode, 11u);
    MICRO_CHECK(ack.root().member("d").is_null());

    const Document invalid = parse_fixture("gateway-invalid-session.json");
    MICRO_CHECK(invalid.ok());
    MICRO_CHECK(invalid.root().member("op").u64(&opcode));
    MICRO_CHECK_EQ(opcode, 9u);
    bool resumable = false;
    MICRO_CHECK(invalid.root().member("d").boolean(&resumable));
    MICRO_CHECK(resumable);
}

MICRO_TEST(a_snowflake_survives_the_read_exactly)
{
    // 2^53 is where a double stops counting by ones; this id is far past it and
    // is the reason numbers are read as text.
    const Document document = Document::parse(R"({"id": 1471229662733062144})");
    MICRO_CHECK(document.ok());

    const Value id = document.root().member("id");
    // The literal first, then the conversion: both must be exact.
    MICRO_CHECK_EQ(id.number(), std::string_view{"1471229662733062144"});

    std::uint64_t value = 0;
    MICRO_CHECK(id.u64(&value));
    MICRO_CHECK_EQ(value, 1471229662733062144ull);

    std::int64_t signed_value = 0;
    MICRO_CHECK(id.i64(&signed_value));
    MICRO_CHECK_EQ(signed_value, 1471229662733062144ll);
}

MICRO_TEST(numbers_that_do_not_fit_are_refused_not_rounded)
{
    const Document document =
        Document::parse(R"({"big": 123456789012345678901234567890, "fraction": 2.5, "negative": -42})");
    MICRO_CHECK(document.ok());

    // Past 64 bits: the literal is kept, the integer conversion refuses.
    const Value big = document.root().member("big");
    MICRO_CHECK_EQ(big.number(), std::string_view{"123456789012345678901234567890"});
    std::uint64_t value = 0;
    MICRO_CHECK(!big.u64(&value));

    // A fraction is not an integer either, and must not be truncated to one.
    MICRO_CHECK_EQ(document.root().member("fraction").number(), std::string_view{"2.5"});
    MICRO_CHECK(!document.root().member("fraction").u64(&value));

    std::int64_t signed_value = 0;
    MICRO_CHECK(document.root().member("negative").i64(&signed_value));
    MICRO_CHECK_EQ(signed_value, -42ll);
    MICRO_CHECK(!document.root().member("negative").u64(&value));

    // Asking the wrong type for a value fails instead of converting.
    MICRO_CHECK(!document.root().member("big").boolean(nullptr));
    MICRO_CHECK_EQ(document.root().member("big").text(), std::string_view{});
}

MICRO_TEST(a_malformed_document_is_a_value_not_a_crash)
{
    const Document malformed = parse_fixture("gateway-malformed.json");
    MICRO_CHECK(!malformed.ok());
    MICRO_CHECK(!malformed.error().empty());
    MICRO_CHECK(malformed.error_offset() > 0);
    // Everything a caller could ask a failed document still answers.
    MICRO_CHECK(!malformed.root().exists());
    MICRO_CHECK_EQ(malformed.root().member("op").size(), 0u);
    std::uint64_t opcode = 0;
    MICRO_CHECK(!malformed.root().member("op").u64(&opcode));

    for (const char *text : {"", "   ", "{", "[1,2", "{\"a\":}", "nan", "{\"a\" 1}",
                             "{\"a\": 0x10}", "tru", "{\"a\": 1,}"})
    {
        const Document document = Document::parse(text);
        MICRO_CHECK(!document.ok());
        MICRO_CHECK(!document.error().empty());
    }
}

MICRO_TEST(deep_nesting_and_its_truncation_are_both_survivable)
{
    // The reader is iterative and ships with no depth limit
    // (YYJSON_READER_DEPTH_LIMIT is 0), so a document like this is walked, not
    // recursed into: 4000 frames deep must not take the daemon's stack down.
    std::string text(4000, '[');
    text += std::string(4000, ']');

    const Document document = Document::parse(text);
    MICRO_CHECK(document.ok());
    MICRO_CHECK(document.root().is_array());
    MICRO_CHECK_EQ(document.root().size(), 1u);

    // Cut off one bracket and the same text is a clean error, not a crash.
    const Document truncated = Document::parse(text.substr(0, text.size() - 1));
    MICRO_CHECK(!truncated.ok());
    MICRO_CHECK(!truncated.error().empty());
}

MICRO_TEST(lookups_past_the_end_answer_with_none)
{
    const Document document = Document::parse(R"({"a": [1, 2], "b": {"c": true}})");
    MICRO_CHECK(document.ok());

    const Value root = document.root();
    MICRO_CHECK(!root.member("nope").exists());
    MICRO_CHECK(!root.member("nope").member("deeper").exists());
    MICRO_CHECK_EQ(root.member("nope").text(), std::string_view{});
    MICRO_CHECK(!root.member("a").element(2).exists());
    MICRO_CHECK(root.member("a").element(1).is_number());
    // An object is not an array and the other way round.
    MICRO_CHECK(!root.member("b").element(0).exists());
    MICRO_CHECK_EQ(root.member("b").size(), 1u);
    MICRO_CHECK_EQ(root.member("a").size(), 2u);
    // A scalar has no members and no elements.
    MICRO_CHECK(!root.member("a").element(0).member("x").exists());
    MICRO_CHECK_EQ(root.member("a").element(0).size(), 0u);
}
