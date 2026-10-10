/*
 * Accord - the CA bundle the TLS client trusts, as the build embedded it.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The roots are Mozilla's, as curl extracts and publishes them: pinned by
 * tools/fetch-cacert.sh (dated file plus the sha256 curl publishes beside it) and
 * turned into a C++ source by tools/embed-cacert.sh, which the host tests and the
 * daemon both compile. The generated file says which extraction it carries.
 *
 * It is embedded rather than read from disk because the daemon is a bare payload:
 * no package, no asset directory, and no promise that /data/accord/ exists. The
 * roots travel inside the binary that needs them.
 *
 * Verification is not optional anywhere in this project: this is where the roots
 * for it come from. See THIRD_PARTY.md for provenance and licence.
 */

#pragma once

#include <string_view>

namespace accord::net
{
// The pinned bundle, one or more PEM certificates, exactly as published. Never
// empty: the build fails rather than produce an artifact that cannot verify.
std::string_view bundled_ca_pem();
} // namespace accord::net
