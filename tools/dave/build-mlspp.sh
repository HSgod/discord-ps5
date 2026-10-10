#!/usr/bin/env bash
# T6.0-2: cross-build nlohmann/json and mlspp for the payload toolchain.
# Runs inside the builder container, after fetch-sources.sh and build-openssl.sh.
#
# MLS_CXX_NAMESPACE=mlspp and DISABLE_GREASE=ON are the flags the libraries are
# pinned with (T6.0-1); TESTING=OFF drops mlspp's own suite, which cannot run on
# a target anyway. nlohmann/json is header only and only has to be installed for
# its CMake config.

set -euo pipefail

sdk="${PS5_PAYLOAD_SDK:?}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
src="$root/build/dave/src"
prefix="$root/build/dave/prefix-ps5"
toolchain="$root/tools/dave/toolchain-prospero.cmake"

common=(-G Ninja
    -DCMAKE_TOOLCHAIN_FILE="$toolchain"
    -DPS5_SDK="$sdk"
    -DPS5_DEPS_PREFIX="$prefix"
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_PREFIX_PATH="$prefix"
    -DCMAKE_INSTALL_PREFIX="$prefix")

echo "=== nlohmann/json (nagłówkowa, wydanie v3.11.3) ==="
cmake "${common[@]}" -S "$src/nlohmann-json" -B "$root/build/dave/build-nlohmann" -DJSON_BuildTests=OFF
cmake --build "$root/build/dave/build-nlohmann" -j "$(nproc)"
cmake --install "$root/build/dave/build-nlohmann"

echo "=== mlspp (namespace mlspp, bez GREASE, bez testów) ==="
cmake "${common[@]}" -S "$src/mlspp" -B "$root/build/dave/build-mlspp-ps5" \
    -DMLS_CXX_NAMESPACE=mlspp -DDISABLE_GREASE=ON -DTESTING=OFF
cmake --build "$root/build/dave/build-mlspp-ps5" -j "$(nproc)"
cmake --install "$root/build/dave/build-mlspp-ps5"

echo "--- archiwa w prefixie ---"
ls -l "$prefix/lib/"*.a
