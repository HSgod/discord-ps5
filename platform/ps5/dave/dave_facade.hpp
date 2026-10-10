/*
 * Accord - The one door to libdave: C API in, error codes out.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * libdave's C bindings have no try/catch of their own (cpp/src/bindings_capi.cpp
 * catches nothing), so a C++ exception raised inside the library can leave the
 * C API. This facade is therefore the only translation unit in the project that
 * includes dave.h and the only place allowed to catch: every entry point wraps
 * the library call in catch (...) and reports a status code. No exception may
 * cross this boundary; the rest of the daemon keeps the project's
 * no-exceptions style.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace accord::ps5::dave
{
// Longest reason kept from the library's MLS failure callback. The library
// frees its own strings as soon as the callback returns, so anything worth
// logging has to be copied here first.
inline constexpr std::size_t kReportedReasonBytes = 128;

enum class Status : std::uint8_t
{
    ok = 0,         // the library produced a key package
    no_session,     // daveSessionCreate returned null
    no_key_package, // the session handed back an empty package
    threw,          // an exception crossed the C API and was caught here
};

struct KeyPackageProbe
{
    Status status = Status::threw;
    std::size_t bytes = 0;              // length of the marshalled key package
    std::uint16_t protocol_version = 0; // version the session was initialised with

    // The library's MLS failure callback is not only for fatal errors: with
    // PERSISTENT_KEYS=OFF it also reports the missing persistent key while the
    // session still falls back on a transient one and the key package comes out
    // fine. So this is reported next to the outcome, never instead of it.
    bool library_reported_failure = false;
    char reported_reason[kReportedReasonBytes] = {};
};

// Creates a session, initialises it at the library's highest supported protocol
// version, takes the marshalled MLS key package and tears the session down.
// It never throws, never aborts and never lets an exception out.
KeyPackageProbe probe_key_package(std::uint64_t group_id, std::string_view self_user_id) noexcept;

// Stable short name for the log line, e.g. "ok" or "threw".
const char *to_string(Status status) noexcept;
} // namespace accord::ps5::dave
