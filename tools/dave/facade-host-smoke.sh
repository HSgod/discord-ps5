#!/usr/bin/env bash
# T6.0-2: compile the daemon's DAVE facade against the host build of the
# libraries and run it, so a broken facade shows up here rather than on a
# console. Runs inside the builder container, after build-host.sh.
#
# The facade source is the one the daemon uses, unchanged: same file, same flags
# apart from the compiler.

set -euo pipefail

export PATH=/usr/lib/llvm-18/bin:$PATH

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
prefix="$root/build/dave/prefix-host"
out="$root/build/dave/host/facade-smoke"

clang++-18 -std=c++20 -O1 -Wall -Wextra -I"$root" -I"$prefix/include" -I"$prefix/include/mlspp" \
    -o "$out" \
    "$root/platform/ps5/dave/dave_facade.cpp" \
    "$root/tools/dave/facade_host_smoke.cpp" \
    -Wl,--start-group \
    "$prefix/lib/libdave.a" \
    "$prefix/lib/libmlspp.a" "$prefix/lib/libhpke.a" \
    "$prefix/lib/libtls_syntax.a" "$prefix/lib/libbytes.a" \
    -Wl,--end-group \
    -lcrypto -lpthread

printf '%s\n' "==> [facade-host-smoke] $("$out")"
