#!/usr/bin/env bash
# Accord - OpenSSL 3.5.2 built natively, for the TLS client's host tests (T3.1).
#
# The daemon gets 3.5.2 cross-built by tools/dave/build-openssl.sh. The host tests
# get the same version from a build of their own: the tree under build/dave/src is
# already configured for the console toolchain (BSD-x86_64) and cannot be reused
# for a macOS one, and testing the shipped version beats testing whatever OpenSSL
# the machine happens to have.
#
#   tools/build-openssl-host.sh    # no-op once the prefix is in place
#
# It runs on the host, not in the container: this is the native half of the test
# suite, the same half `make test` builds. The tarball is the pin
# tools/dave/fetch-sources.sh uses, checked against the sha256 OpenSSL publishes
# next to it, and it is reused from there when that build already downloaded it.
# Options match the console build (no shared libraries, no apps, no engine) so
# that the tests exercise the same feature set the daemon ships with.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

version=3.5.2
# The value in openssl-3.5.2.tar.gz.sha256 as OpenSSL publishes it.
sha256=c53a47e5e441c930c3928cf7bf6fb00e5d129b630e0aa873b08258656e7345ec
url=https://github.com/openssl/openssl/releases/download/openssl-$version/openssl-$version.tar.gz

work="$root/build/openssl-host"
src="$work/src"
prefix="$work/prefix"
tarball="$work/openssl-$version.tar.gz"

if [ -f "$prefix/lib/libssl.a" ] && [ -f "$prefix/lib/libcrypto.a" ]; then
    exit 0
fi

mkdir -p -- "$work"

if [ ! -f "$tarball" ]; then
    console_tarball="$root/build/dave/src/openssl-$version.tar.gz"
    if [ -f "$console_tarball" ]; then
        printf '==> [openssl-host] reusing %s\n' "$console_tarball" >&2
        cp -a -- "$console_tarball" "$tarball"
    else
        printf '==> [openssl-host] fetching %s\n' "$url" >&2
        curl -fsSL -o "$tarball" "$url"
    fi
fi

printf '==> [openssl-host] verifying the sha256 of openssl-%s.tar.gz\n' "$version" >&2
printf '%s  %s\n' "$sha256" "$tarball" | shasum -a 256 -c - >&2

rm -rf -- "$src"
mkdir -p -- "$src"
tar -xzf "$tarball" -C "$src" --strip-components=1

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '4')

cd "$src"
# The target is not named: on the host, OpenSSL's own Configure picks it (on an
# Apple silicon machine, darwin64-arm64-cc). Assembly stays on here -- it is the
# console build that has to go without it.
printf '==> [openssl-host] configuring %s\n' "$version" >&2
./Configure --prefix="$prefix" --openssldir="$prefix/ssl" \
    no-shared no-tests no-apps no-docs no-dso no-async no-legacy no-engine
printf '==> [openssl-host] building libcrypto and libssl (%s jobs)\n' "$jobs" >&2
make -j"$jobs" build_libs
make install_dev

[[ -f "$prefix/lib/libssl.a" && -f "$prefix/lib/libcrypto.a" ]] || {
    printf 'openssl-host: the build finished without libssl.a/libcrypto.a in %s\n' "$prefix" >&2
    exit 2
}

printf '==> [openssl-host] %s -> %s/lib, %s/include (OpenSSL %s)\n' \
    "$version" "$prefix" "$prefix" "$(sed -n 's/^# *define *OPENSSL_VERSION_STR *"\(.*\)"/\1/p' \
        "$prefix/include/openssl/opensslv.h" | head -1)"
