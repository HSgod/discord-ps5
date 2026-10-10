/*
 * Accord - JSON reading for Discord payloads.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Gateway events and REST answers are the only JSON this program reads, and
 * both arrive from the network, so the reader is built around two rules:
 *
 *   1. a malformed document is a value, not a signal. parse() returns a
 *      Document whose ok() is false and whose error() says what was wrong and
 *      where. Nothing in here throws, and nothing calls abort(): a payload from
 *      Discord must not be able to take the daemon down.
 *   2. every number stays text until it is asked for as an integer. Discord's
 *      snowflakes are 64-bit ids; a double holds 53 bits exactly, so a reader
 *      that routes them through one silently corrupts the last digits of a
 *      channel or message id. number() hands back the literal, u64()/i64()
 *      convert it exactly and report failure instead of rounding, and no
 *      accessor returns a double at all.
 *
 * The value type is a borrowed view into the document that owns it: a Value is
 * valid only while its Document lives, and copying one is free.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace accord::core::json
{

// A value that is not there -- an absent member, an index past the end, a
// document that failed to parse -- is Kind::none. Chained lookups such as
// root().member("d").member("author").text() are safe: they walk to none and
// answer with an empty value rather than a trap.
class Value
{
  public:
    Value() = default;

    enum class Kind : std::uint8_t
    {
        none,
        null_value,
        boolean,
        number,
        string,
        array,
        object,
    };

    Kind kind() const noexcept;
    bool exists() const noexcept
    {
        return kind_ != Kind::none;
    }
    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_number() const noexcept;
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    // An object's member by key; none when this is not an object or the key is
    // absent.
    Value member(std::string_view key) const noexcept;
    // An array's element; none past the end or when this is not an array.
    Value element(std::size_t index) const noexcept;
    // Array elements, or object members. Zero for every other kind.
    std::size_t size() const noexcept;

    // A string, unescaped by the parser, or empty for any other kind. This is
    // what "content", "username" and the id fields (which Discord sends as
    // strings) are read with.
    std::string_view text() const noexcept;

    // The number exactly as the document wrote it, e.g. "1471229662733062144".
    // Empty when the value is not a number.
    std::string_view number() const noexcept;

    // Integer conversions. Each returns false and leaves *out alone when the
    // value is not a number, is fractional, or does not fit; nothing is
    // clamped or rounded on the way. Fractional numbers are only readable as
    // text, through number().
    bool u64(std::uint64_t *out) const noexcept;
    bool i64(std::int64_t *out) const noexcept;
    bool boolean(bool *out) const noexcept;

  private:
    friend class Document;
    // A yyjson value, or nullptr; kept untyped so this header does not pull the
    // parser's header into every file that reads a payload.
    Value(Kind kind, const void *raw) noexcept : kind_{kind}, raw_{raw}
    {
    }
    // Classifies a parser value and wraps it.
    static Value from_raw(const void *raw) noexcept;

    Kind kind_ = Kind::none;
    const void *raw_ = nullptr;
};

// One parsed document. Move-only: it owns the parser's arena.
class Document
{
  public:
    Document() noexcept = default;
    ~Document();
    Document(Document &&other) noexcept;
    Document &operator=(Document &&other) noexcept;
    Document(const Document &) = delete;
    Document &operator=(const Document &) = delete;

    // Parses UTF-8 text. A malformed document, a nesting depth past the
    // parser's limit or a failed allocation all come back as !ok() with the
    // reason in error(). No exception is thrown for a bad document.
    static Document parse(std::string_view text);

    bool ok() const noexcept
    {
        return document_ != nullptr;
    }
    // Empty when ok(); otherwise the parser's message plus the byte it stopped
    // at.
    const std::string &error() const noexcept
    {
        return error_;
    }
    // Byte offset in the input the error was found at; 0 when ok().
    std::size_t error_offset() const noexcept
    {
        return error_offset_;
    }
    // The document's root value, which is none when !ok().
    Value root() const noexcept;

  private:
    void *document_ = nullptr;
    std::string error_;
    std::size_t error_offset_ = 0;
};

} // namespace accord::core::json
