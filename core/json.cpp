/*
 * Accord - JSON reading for Discord payloads.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * yyjson does the parsing (MIT, fetched and pinned by tools/fetch-yyjson.sh).
 * Two of its properties are the reason for the choice, and both are what
 * core/ needs: it reports failure as a return value -- no exceptions, no
 * setjmp/longjmp, no abort() anywhere in the source -- and it can hand every
 * number back as the text the document wrote, which is what keeps a 64-bit
 * snowflake out of a double on the way in.
 */

#include "core/json.hpp"

#include <charconv>
#include <utility>

#include "third_party/yyjson/yyjson.h"

namespace accord::core::json
{
namespace
{
// Every number is kept as its literal (YYJSON_READ_NUMBER_AS_RAW), so a number
// reaches this file as text and is only ever converted on request. Without the
// flag the parser would store numbers past 53 bits as a double itself.
constexpr yyjson_read_flag kReadFlags = YYJSON_READ_NUMBER_AS_RAW;

using Raw = yyjson_val;

// Value keeps its yyjson pointer untyped; this is the one place the type comes
// back, and it lives in this file.
const Raw *value_of(const void *raw)
{
    return static_cast<const Raw *>(raw);
}

// The integer of a number token, or false when it is fractional or does not
// fit: from_chars refuses rather than rounding, which is the whole point.
bool parse_u64(std::string_view literal, std::uint64_t *out) noexcept
{
    if (literal.empty() || literal.front() == '-')
        return false;

    std::uint64_t value = 0;
    const auto result = std::from_chars(literal.data(), literal.data() + literal.size(), value);
    if (result.ec != std::errc{} || result.ptr != literal.data() + literal.size())
        return false;

    *out = value;
    return true;
}

bool parse_i64(std::string_view literal, std::int64_t *out) noexcept
{
    if (literal.empty())
        return false;

    std::int64_t value = 0;
    const auto result = std::from_chars(literal.data(), literal.data() + literal.size(), value);
    if (result.ec != std::errc{} || result.ptr != literal.data() + literal.size())
        return false;

    *out = value;
    return true;
}
} // namespace

// A member function cannot be defined inside the anonymous namespace above, so
// this sits right after it, where value_of is still visible.
Value Value::from_raw(const void *raw) noexcept
{
    const Raw *value = value_of(raw);
    if (value == nullptr)
        return Value{};

    Kind kind = Kind::none;
    if (yyjson_is_null(value))
        kind = Kind::null_value;
    else if (yyjson_is_bool(value))
        kind = Kind::boolean;
    // With NUMBERS_AS_RAW set, a number is whatever the document wrote for it,
    // and the parser calls that a raw value rather than an int/uint/real.
    else if (yyjson_is_num(value) || yyjson_is_raw(value))
        kind = Kind::number;
    else if (yyjson_is_str(value))
        kind = Kind::string;
    else if (yyjson_is_arr(value))
        kind = Kind::array;
    else if (yyjson_is_obj(value))
        kind = Kind::object;

    return Value{kind, value};
}

Value::Kind Value::kind() const noexcept
{
    return kind_;
}

bool Value::is_null() const noexcept
{
    return kind_ == Kind::null_value;
}

bool Value::is_bool() const noexcept
{
    return kind_ == Kind::boolean;
}

bool Value::is_number() const noexcept
{
    return kind_ == Kind::number;
}

bool Value::is_string() const noexcept
{
    return kind_ == Kind::string;
}

bool Value::is_array() const noexcept
{
    return kind_ == Kind::array;
}

bool Value::is_object() const noexcept
{
    return kind_ == Kind::object;
}

Value Value::member(std::string_view key) const noexcept
{
    if (kind_ != Kind::object)
        return Value{};
    // yyjson wants a C string; a key from a payload is short and the caller's
    // view is not guaranteed terminated, so it is copied. Keys arrive one per
    // lookup, not per element of a loop over a large object.
    const std::string name{key};
    return from_raw(yyjson_obj_get(value_of(raw_), name.c_str()));
}

Value Value::element(std::size_t index) const noexcept
{
    if (kind_ != Kind::array)
        return Value{};
    return from_raw(yyjson_arr_get(value_of(raw_), index));
}

std::size_t Value::size() const noexcept
{
    if (kind_ == Kind::array)
        return yyjson_arr_size(value_of(raw_));
    if (kind_ == Kind::object)
        return yyjson_obj_size(value_of(raw_));
    return 0;
}

std::string_view Value::text() const noexcept
{
    if (kind_ != Kind::string)
        return {};
    const Raw *raw = value_of(raw_);
    return std::string_view{yyjson_get_str(raw), yyjson_get_len(raw)};
}

std::string_view Value::number() const noexcept
{
    if (kind_ != Kind::number)
        return {};
    const Raw *raw = value_of(raw_);
    return std::string_view{yyjson_get_raw(raw), yyjson_get_len(raw)};
}

bool Value::u64(std::uint64_t *out) const noexcept
{
    if (out == nullptr || kind_ != Kind::number)
        return false;
    return parse_u64(number(), out);
}

bool Value::i64(std::int64_t *out) const noexcept
{
    if (out == nullptr || kind_ != Kind::number)
        return false;
    return parse_i64(number(), out);
}

bool Value::boolean(bool *out) const noexcept
{
    if (out == nullptr || kind_ != Kind::boolean)
        return false;
    *out = yyjson_get_bool(value_of(raw_));
    return true;
}

Document::~Document()
{
    if (document_ != nullptr)
        yyjson_doc_free(static_cast<yyjson_doc *>(document_));
}

Document::Document(Document &&other) noexcept
    : document_{std::exchange(other.document_, nullptr)}, error_{std::move(other.error_)},
      error_offset_{std::exchange(other.error_offset_, 0)}
{
}

Document &Document::operator=(Document &&other) noexcept
{
    if (this != &other)
    {
        if (document_ != nullptr)
            yyjson_doc_free(static_cast<yyjson_doc *>(document_));
        document_ = std::exchange(other.document_, nullptr);
        error_ = std::move(other.error_);
        error_offset_ = std::exchange(other.error_offset_, 0);
    }
    return *this;
}

Document Document::parse(std::string_view text)
{
    Document document;

    yyjson_read_err error{};
    // The cast is only for the signature: without YYJSON_READ_INSITU the parser
    // copies the text it walks and never writes to it, so a caller's string_view
    // stays as it was.
    yyjson_doc *parsed = yyjson_read_opts(const_cast<char *>(text.data()), text.size(), kReadFlags,
                                          nullptr, &error);
    if (parsed == nullptr)
    {
        // The parser's message is a literal; the offset is where it stopped.
        document.error_ = error.msg != nullptr ? std::string{error.msg} : std::string{"parse failed"};
        document.error_ += " at byte " + std::to_string(error.pos);
        document.error_offset_ = error.pos;
        return document;
    }

    document.document_ = parsed;
    return document;
}

Value Document::root() const noexcept
{
    if (document_ == nullptr)
        return Value{};
    return Value::from_raw(yyjson_doc_get_root(static_cast<const yyjson_doc *>(document_)));
}

} // namespace accord::core::json
