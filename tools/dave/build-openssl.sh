#!/usr/bin/env bash
# T6.0-2: cross-build OpenSSL 3 (static libcrypto) for the payload toolchain.
# Runs inside the builder container, after fetch-sources.sh.
#
# Target BSD-x86_64 is the target PacBrew uses, and no-asm keeps the
# perl-generated x86_64 assembly out of the build: the SDK's assembler is not
# the one the OpenSSL build expects. The cost is speed at runtime, which matters
# little for a voice client and can be revisited later.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK is unset: run this inside the builder container}"
src="$root/build/dave/src/openssl-3.5.2"
prefix="$root/build/dave/prefix-ps5"

cd "$src"

export CC="$sdk/bin/prospero-clang"
export CXX="$sdk/bin/prospero-clang++"
export AR="$sdk/bin/llvm-ar"
export RANLIB="$sdk/bin/llvm-ranlib"
export NM="$sdk/bin/llvm-nm"

if [ ! -f Makefile ]; then
    ./Configure BSD-x86_64 \
        no-shared no-tests no-apps no-docs no-dso no-async no-asm no-legacy no-engine \
        --prefix="$prefix" --openssldir="$prefix/ssl"
fi

make -j"$(nproc)" build_libs
make install_dev

echo "--- zainstalowane ---"
ls -l "$prefix/lib/" | head
