#!/usr/bin/env bash
# Accord - pinned Mozilla CA bundle for the TLS client of T3.1.
#
# The gateway is reached over TLS and the certificate has to be checked against
# roots we ship, so those roots are Mozilla's root store as curl extracts it. The
# bundle is a build input, not a committed file: `third_party/cacert/` is
# gitignored, and what the repository holds is this script, the version and the
# hash.
#
#   tools/fetch-cacert.sh      # fetch when missing, then verify
#
# The pin is the dated copy (curl.se keeps one per extraction date, and
# cacert.pem itself is replaced in place, so it cannot be pinned) together with
# the sha256 curl publishes next to it. A swapped or truncated download cannot
# pass silently.
#
# Consumers: tools/embed-cacert.sh turns the file into the C++ source that
# carries it into build/host-tests and dist/accordd.elf. See THIRD_PARTY.md.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

version=2026-09-25
# curl.se/ca/cacert-2026-09-25.pem.sha256 carries exactly this value.
sha256=a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505
url=https://curl.se/ca/cacert-$version.pem

vendor="$root/third_party/cacert"
bundle="$vendor/cacert.pem"

if [ ! -f "$bundle" ]; then
    scratch="$root/build/cacert-src"
    download="$scratch/cacert-$version.pem"
    mkdir -p -- "$scratch"

    if [ ! -f "$download" ]; then
        printf '==> [cacert] fetching %s\n' "$url" >&2
        curl -fsSL -o "$download" "$url"
    fi
    printf '==> [cacert] verifying the sha256 of cacert-%s.pem\n' "$version" >&2
    printf '%s  %s\n' "$sha256" "$download" | shasum -a 256 -c - >&2

    mkdir -p -- "$vendor"
    cp -a -- "$download" "$bundle"
fi

# A bundle that lost certificates in transit still parses, so the count is
# checked as well as the hash: the sha256 says "this is the file curl published",
# and the count says "the file is a whole root store and not a stub".
count=$(grep -c '^-----BEGIN CERTIFICATE-----$' "$bundle" || true)
if [ "$count" -lt 100 ]; then
    printf 'cacert: %s holds %s certificates, which is not a root store: remove it and fetch again\n' \
        "$bundle" "$count" >&2
    exit 2
fi

printf '==> [cacert] %s: %s certificates (Mozilla, %s)\n' \
    "third_party/cacert/cacert.pem" "$count" "$version"
