/*
 * Accord - The one door to libdave: C API in, error codes out.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "platform/ps5/dave/dave_facade.hpp"

#include <dave/dave.h>

#include <string>

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
