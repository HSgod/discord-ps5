#!/usr/bin/env bash
# Accord - pinned zlib download for the gateway's zlib-stream compression.
#
# Discord asks for `compress=zlib-stream` in the gateway URL and then sends every
# event as one zlib stream per connection, flushed per message (see
# research/discord-ps5/04-discord-api.md and THIRD_PARTY.md). core/zlib_stream.cpp
# reads that stream, so zlib has to be built for the host tests and for the
# daemon, and staged into the title.
#
#   tools/fetch-zlib.sh        # fetch when missing, then verify
#
# The pin is the release tarball and the sha256 zlib.net publishes next to it, so
# a swapped download cannot pass silently. The tarball is unpacked into build/
# (gitignored) and its sources are copied into third_party/zlib together with the
# licence, which is what every consumer builds from: the host tests and the
# daemon take the files they need by name, and tools/stage-ps5-sources.sh hands
# the title the inflate side of the same copies.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

version=1.3.2
# zlib.net lists this hash next to zlib-1.3.2.tar.gz; the download is checked
# against it before anything is unpacked.
sha256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
url=https://zlib.net/zlib-$version.tar.gz

scratch="$root/build/zlib-src"
vendor="$root/third_party/zlib"
tarball="$scratch/zlib-$version.tar.gz"
unpacked="$scratch/src" # the tarball's own top directory is stripped

mkdir -p -- "$scratch"

if [ ! -f "$unpacked/zlib.h" ]; then
    if [ ! -f "$tarball" ]; then
        printf '==> [zlib] fetching %s\n' "$url" >&2
        curl -fsSL -o "$tarball" "$url"
    fi
    printf '==> [zlib] verifying the sha256 of zlib-%s.tar.gz\n' "$version" >&2
    printf '%s  %s\n' "$sha256" "$tarball" | shasum -a 256 -c - >&2
    mkdir -p -- "$unpacked"
    tar -xzf "$tarball" -C "$unpacked" --strip-components=1
fi

[[ -f $unpacked/zlib.h && -f $unpacked/LICENSE ]] || {
    printf 'missing %s/zlib.h or LICENSE: remove %s and fetch again\n' "$unpacked" "$scratch" >&2
    exit 2
}

mkdir -p -- "$vendor"

# The sources and the headers, plus the licence: the directory is meant to be
# readable as the release it came from, and the headers carry the version and the
# copyright. Which of them a build compiles is that build's business -- the
# artifacts take the inflate side, and the compressor joins only the host tests,
# which build their own fixtures and then inflate them with the same library.
for source in "$unpacked"/*.c "$unpacked"/*.h "$unpacked"/LICENSE; do
    name=$(basename -- "$source")
    cmp -s "$source" "$vendor/$name" || cp -- "$source" "$vendor/$name"
done

printf '%s\n' "$vendor"
