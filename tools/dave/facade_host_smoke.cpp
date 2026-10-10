/*
 * Accord - T6.0-2: run the daemon's DAVE facade on the host.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The console run belongs to T2.3/T2.2b, so this is where a broken facade can
 * be seen: it calls exactly the entry point the daemon calls at startup and
 * fails the process when no key package comes back.
 */

#include "platform/ps5/dave/dave_facade.hpp"

#include <cstdio>

int main()
{
    const accord::ps5::dave::KeyPackageProbe probe =
        accord::ps5::dave::probe_key_package(0, "0");

    std::printf("status=%s bytes=%zu protocol_version=%u reported_failure=%s reason=\"%s\"\n",
                accord::ps5::dave::to_string(probe.status), probe.bytes,
                static_cast<unsigned>(probe.protocol_version),
                probe.library_reported_failure ? "yes" : "no", probe.reported_reason);

    return probe.status == accord::ps5::dave::Status::ok ? 0 : 1;
}
