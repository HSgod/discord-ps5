#!/usr/bin/env bash
# T6.0-2: cross-build libdave for the payload toolchain.
# Runs inside the builder container, after build-mlspp.sh.
#
# PERSISTENT_KEYS=OFF is deliberate: the option picks the library's key-pair
# implementation, and the "off" branch compiles persisted_key_pair_null.cpp,
# which hands back a null key pair so every session falls back to a transient
# signature key. Nothing is stored on the console yet. Whether a real Discord
# call needs a stable identity across restarts is a question for Faza 6, not
# for this task; the code path to change is this flag plus a storage location.
#
# Exception and RTTI flags stay at the compiler's defaults in both builds, which
# is what T6.0-1 asked for: the facade needs to catch exceptions by type.

set -euo pipefail

sdk="${PS5_PAYLOAD_SDK:?}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
src="$root/build/dave/src"
prefix="$root/build/dave/prefix-ps5"
toolchain="$root/tools/dave/toolchain-prospero.cmake"

cmake -G Ninja -S "$src/libdave/cpp" -B "$root/build/dave/build-libdave-ps5" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DPS5_SDK="$sdk" \
    -DPS5_DEPS_PREFIX="$prefix" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DTESTING=OFF \
    -DPERSISTENT_KEYS=OFF \
    -DCMAKE_PREFIX_PATH="$prefix" \
    -DCMAKE_INSTALL_PREFIX="$prefix"

cmake --build "$root/build/dave/build-libdave-ps5" -j "$(nproc)"
cmake --install "$root/build/dave/build-libdave-ps5"

echo "--- libdave.a ---"
ls -l "$prefix/lib/libdave.a"
echo "--- headers ---"
ls "$prefix/include/dave/"
