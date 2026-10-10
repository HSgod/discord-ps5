#!/usr/bin/env bash
# Builds the background daemon payload into dist/accordd.elf.
#
# Run inside the builder container (see the payload target in the Makefile).
# The payload SDK's C++ driver brings its own sysroot, crt, libc++ and the
# libkernel/SceNet stubs, so the one library asked for on top of that is
# SceAudioOut. There is no separate libm: the math calls in the tone generator
# resolve inside the SDK's libc.
#
# DAVE (T6.0-2) is linked in as well: libdave, mlspp and OpenSSL 3 are static
# archives built for this toolchain by tools/dave/. Those live in build/, which
# is gitignored, so a fresh checkout builds them first with `make dave-deps`.
# DAVE_PREFIX points somewhere else than build/dave/prefix-ps5 if needed.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

sdk="${PS5_PAYLOAD_SDK:?PS5_PAYLOAD_SDK is unset: run this inside the builder container}"
cc="$sdk/bin/prospero-clang++"
cc_c="$sdk/bin/prospero-clang"
dave_prefix="${DAVE_PREFIX:-$root/build/dave/prefix-ps5}"

out="dist/accordd.elf"
mkdir -p dist

# core/json.cpp reads the pinned parser, and that parser is C: the SDK's C
# driver compiles it on its own and the object joins the same link as ours.
yyjson_dir="$root/third_party/yyjson"
if [ ! -f "$yyjson_dir/yyjson.c" ]; then
    printf '%s\n' "==> [payload] missing $yyjson_dir/yyjson.c" >&2
    printf '%s\n' "    fetch it first: bash tools/fetch-yyjson.sh" >&2
    exit 1
fi
yyjson_obj="$root/build/yyjson-ps5.o"
mkdir -p "$(dirname "$yyjson_obj")"
"$cc_c" -O2 -Wall -Wextra -c "$yyjson_dir/yyjson.c" -o "$yyjson_obj"

# core/zlib_stream.cpp reads Discord's compressed gateway stream with the pinned
# zlib, and zlib is C too, so the same C driver compiles it. The inflate side
# only: the payload reads a compressed stream and never writes one, which is the
# subset the Makefile links as well (the host tests add the compressor, but only
# to build their own fixtures).
zlib_dir="$root/third_party/zlib"
if [ ! -f "$zlib_dir/zlib.h" ]; then
    printf '%s\n' "==> [payload] missing $zlib_dir/zlib.h" >&2
    printf '%s\n' "    fetch it first: bash tools/fetch-zlib.sh" >&2
    exit 1
fi
zlib_obj_dir="$root/build/zlib-ps5"
mkdir -p "$zlib_obj_dir"
zlib_objects=()
for name in adler32 crc32 inffast inflate inftrees zutil; do
    object="$zlib_obj_dir/$name.o"
    "$cc_c" -O2 -Wall -Wextra -c "$zlib_dir/$name.c" -o "$object"
    zlib_objects+=("$object")
done

# A missing archive would show up only as a wall of undefined symbols at link
# time, so each one is named up front with the command that builds it.
dave_libs=(
    "$dave_prefix/lib/libdave.a"
    "$dave_prefix/lib/libmlspp.a"
    "$dave_prefix/lib/libhpke.a"
    "$dave_prefix/lib/libtls_syntax.a"
    "$dave_prefix/lib/libbytes.a"
    "$dave_prefix/lib/libcrypto.a"
)
for lib in "${dave_libs[@]}"; do
    if [ ! -f "$lib" ]; then
        printf '%s\n' "==> [payload] missing $lib" >&2
        printf '%s\n' "    build the DAVE chain first: see the header of this script" >&2
        exit 1
    fi
done

sources=()
while IFS= read -r source; do
    sources+=("$source")
done < <(find daemon/src core platform/ps5/dave -name '*.cpp' | sort)

# The archives go into a group because they reference each other both ways
# (mlspp pulls in hpke and tls_syntax, tls_syntax pulls in bytes).
# The second include is mlspp's own convention: its headers are grouped under
# dave_prefix/include/mlspp so that the <namespace.h> and <tls/...> inside them
# resolve. The unwind self-test (T6.0-3) includes one of them.
"$cc" -std=c++20 -O2 -Wall -Wextra -I. -I"$dave_prefix/include" -I"$dave_prefix/include/mlspp" \
    -o "$out" "${sources[@]}" "$yyjson_obj" "${zlib_objects[@]}" \
    -Wl,--start-group "${dave_libs[@]}" -Wl,--end-group \
    -lSceAudioOut

printf '%s\n' "==> [payload] $out ($(wc -c <"$out") bytes)"
