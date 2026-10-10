/*
 * Accord - The one door to libdave: C API in, error codes out.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform/ps5/dave/dave_facade.hpp"

#include <dave/dave.h>

// T6.0-3: mlspp's headers, included the way libdave includes them (the prefix
// puts them under a directory of their own so that <namespace.h> inside them
// resolves). Only the self-test uses them; the probe below goes through dave.h.
#include <bytes/bytes.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace accord::ps5::dave
{
namespace
{
// What the MLS failure callback hands over. The library frees its own strings as
// soon as the callback returns, so the reason is copied here; the buffer is
// fixed-size because nothing in a callback should allocate or throw.
struct FailureSink
{
    bool fired = false;
    char reason[kReportedReasonBytes] = {};
};

void on_mls_failure(const char * /*source*/, const char *reason, void *user_data) noexcept
{
    FailureSink *sink = static_cast<FailureSink *>(user_data);
    if (sink == nullptr)
        return;

    sink->fired = true;
    if (reason == nullptr)
        return;

    std::size_t i = 0;
    while (reason[i] != '\0' && i + 1 < kReportedReasonBytes)
    {
        sink->reason[i] = reason[i];
        ++i;
    }
    sink->reason[i] = '\0';
}
} // namespace

const char *to_string(const UnwindSelfTest &test) noexcept
{
    return test.ok() ? "ok" : "failed";
}

UnwindSelfTest run_unwind_self_test() noexcept
{
    UnwindSelfTest test;

    // A plain runtime error, thrown and caught in this file. Nothing else is
    // involved, so a miss here means unwinding itself does not work.
    try
    {
        throw std::runtime_error{"unwind self test"};
    }
    catch (const std::runtime_error &)
    {
        test.runtime_error_caught = true;
    }
    catch (...)
    {
        // Something arrived, but not as the type that was thrown: unwinding ran
        // and the type did not survive it.
    }

    // Thrown by mlspp's compiled code (bytes.cpp, in libbytes.a) and caught by
    // its exact type here, so the unwind tables of another archive are exercised.
    // from_hex refuses an odd-length string.
    try
    {
        (void)::mlspp::bytes_ns::from_hex("abc");
    }
    catch (const std::invalid_argument &)
    {
        test.mlspp_error_caught = true;
    }
    catch (...)
    {
    }

    // The parse path the task names: a length prefix that promises more bytes
    // than there are makes tls_syntax throw ReadError, a std::invalid_argument.
    try
    {
        const std::vector<std::uint8_t> truncated{0x02};
        std::vector<std::uint8_t> parsed;
        ::mlspp::tls::unmarshal(truncated, parsed);
    }
    catch (const ::mlspp::tls::ReadError &)
    {
        test.tls_parse_error_caught = true;
    }
    catch (...)
    {
    }

    return test;
}

const char *to_string(Status status) noexcept
{
    switch (status)
    {
    case Status::ok:
        return "ok";
    case Status::no_session:
        return "no_session";
    case Status::no_key_package:
        return "no_key_package";
    case Status::threw:
        return "threw";
    }
    return "unknown";
}

KeyPackageProbe probe_key_package(std::uint64_t group_id, std::string_view self_user_id) noexcept
{
    KeyPackageProbe probe;
    FailureSink sink;
    const std::string self{self_user_id}; // the C API keeps this pointer

    try
    {
        DAVESessionHandle session =
            daveSessionCreate(nullptr, "accord-key-package-probe", &on_mls_failure, &sink);
        if (session == nullptr)
        {
            probe.status = Status::no_session;
            return probe;
        }

        probe.protocol_version = daveMaxSupportedProtocolVersion();

        try
        {
            daveSessionInit(session, probe.protocol_version, group_id, self.c_str());

            std::uint8_t *package = nullptr;
            std::size_t length = 0;
            daveSessionGetMarshalledKeyPackage(session, &package, &length);

            if (package != nullptr)
            {
                probe.bytes = length;
                daveFree(package);
            }
            probe.status = (probe.bytes > 0) ? Status::ok : Status::no_key_package;
        }
        catch (...)
        {
            // The session exists, so it still has to be destroyed below.
            probe.status = Status::threw;
        }

        daveSessionDestroy(session);
    }
    catch (...)
    {
        probe.status = Status::threw;
    }

    // Reported next to the outcome, never instead of it: the callback fires for
    // conditions the session recovers from on its own (a missing persistent key
    // with PERSISTENT_KEYS=OFF), and a key package can come out of that fine.
    probe.library_reported_failure = sink.fired;
    for (std::size_t i = 0; i < kReportedReasonBytes; ++i)
    {
        probe.reported_reason[i] = sink.reason[i];
        if (sink.reason[i] == '\0')
            break;
    }

    return probe;
}
} // namespace accord::ps5::dave
