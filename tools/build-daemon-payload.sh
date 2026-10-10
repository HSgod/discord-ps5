#!/usr/bin/env bash
# Builds the background daemon payload into dist/accordd.elf.
#
# Run inside the builder container (see the payload target in the Makefile).
# The payload SDK's C++ driver brings its own sysroot, crt, libc++ and the
# libkernel/SceNet stubs, so the one library asked for on top of that is
# SceAudioOut. There is no separate libm: the math calls in the tone generator
# resolve inside the SDK's libc.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

sdk="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK is unset: run this inside the builder container}"
cc="$sdk/bin/prospero-clang++"

out="dist/accordd.elf"
mkdir -p dist

sources=()
while IFS= read -r source; do
    sources+=("$source")
done < <(find daemon/src core -name '*.cpp' | sort)

"$cc" -std=c++20 -O2 -Wall -Wextra -I. -o "$out" "${sources[@]}" -lSceAudioOut

printf '%s\n' "==> [payload] $out ($(wc -c <"$out") bytes)"
