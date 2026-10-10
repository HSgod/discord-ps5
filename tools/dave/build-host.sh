#!/usr/bin/env bash
# T6.0-2: build the same DAVE stack natively and run libdave's own tests.
# Runs inside the builder container; needs libssl-dev, libgtest-dev and
# libgmock-dev, which the builder image carries.
#
# "The same flags" means the same CMake configuration the target build uses for
# the libraries themselves: mlspp with MLS_CXX_NAMESPACE=mlspp and
# DISABLE_GREASE=ON, libdave with PERSISTENT_KEYS=OFF, RTTI and exceptions left
# at the compiler defaults. Two things differ on purpose: the compiler is the
# container's native clang-18 instead of prospero-clang, and TESTING is ON here
# because running the suite is the point of this build. The host OpenSSL is
# Ubuntu's 3.0.13 rather than the 3.5.2 built for the target.

set -euo pipefail

export PATH=/usr/lib/llvm-18/bin:$PATH

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
src="$root/build/dave/src"
host="$root/build/dave/host"
prefix="$root/build/dave/prefix-host"
jobs="$(nproc)"

mkdir -p "$host"

common=(-G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF
    -DCMAKE_C_COMPILER=clang-18
    -DCMAKE_CXX_COMPILER=clang++-18
    -DCMAKE_PREFIX_PATH="$prefix"
    -DCMAKE_INSTALL_PREFIX="$prefix")

echo "=== nlohmann/json (host) ==="
cmake "${common[@]}" -S "$src/nlohmann-json" -B "$host/nlohmann" -DJSON_BuildTests=OFF
cmake --build "$host/nlohmann" -j "$jobs"
cmake --install "$host/nlohmann"

echo "=== mlspp (host, te same flagi co na PS5) ==="
cmake "${common[@]}" -S "$src/mlspp" -B "$host/mlspp" \
    -DMLS_CXX_NAMESPACE=mlspp -DDISABLE_GREASE=ON -DTESTING=OFF
cmake --build "$host/mlspp" -j "$jobs"
cmake --install "$host/mlspp"

echo "=== libdave (host, TESTING=ON) ==="
cmake "${common[@]}" -S "$src/libdave/cpp" -B "$host/libdave" \
    -DTESTING=ON -DPERSISTENT_KEYS=OFF
cmake --build "$host/libdave" -j "$jobs"

# Installed as well, so the facade smoke test can include <dave/dave.h> and link
# exactly the way the payload build does, from a prefix rather than a build tree.
cmake --install "$host/libdave"

# Upstream's CMakeLists never calls enable_testing(), so ctest finds no tests
# even though test/CMakeLists.txt registers them. The binaries are run directly.
echo "=== libdave_test ==="
"$host/libdave/test/libdave_test"
echo "=== capi_test ==="
"$host/libdave/test/capi/capi_test"
