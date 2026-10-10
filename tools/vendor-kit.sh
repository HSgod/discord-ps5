#!/usr/bin/env bash
# Accord - vendor the ps5-homebrew-ui drawing kit into ui/kit/.
#
# Adopts the kit per third_party/ps5-homebrew-ui/docs/ADOPTING.md ("Take the
# kit"): the gfx/, ui/, ui/components/, core/ and audio/ directories, plus the
# console platform layer and stb. The kit's demo content (app/, concepts/,
# demo/) is deliberately NOT taken - it is the kit's own app, not ours - and
# nothing in the adopted subset depends on it (checked with grep over the
# includes before this script was written).
#
# The kit is a submodule, so the copy is checked in. Upstream references to its
# own headers are root-anchored ("gfx/renderer.hpp", "ui/fonts.hpp",
# "core/tween.hpp", "audio/...", "platform/...", "third_party/stb/..."), which
# would collide with our own core/ and ui/ on the include path. They are
# therefore rewritten to a "ui/kit/..." prefix, so a single include root stays
# enough and no name in the kit can shadow one of ours. Same-directory includes
# ("fonts.hpp") are relative and are left alone.
#
# usage: tools/vendor-kit.sh
# Idempotent: it replaces ui/kit/ wholesale.

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
kit="$root/third_party/ps5-homebrew-ui"
dest="$root/ui/kit"

if [[ ! -d "$kit/src/gfx" ]]; then
    printf '%s\n' "vendor-kit.sh: $kit is not populated (run: git submodule update --init)" >&2
    exit 1
fi

revision="$(git -C "$kit" rev-parse HEAD)"

rm -rf -- "$dest"
mkdir -p -- "$dest/third_party/stb" "$dest/platform" "$dest/host"

for directory in gfx ui core audio; do
    cp -a -- "$kit/src/$directory" "$dest/$directory"
done
cp -a -- "$kit/src/platform/ps5" "$dest/platform/ps5"
cp -a -- "$kit/src/third_party/stb/." "$dest/third_party/stb/"
cp -a -- "$kit/third_party/stb/stb_image_write.h" "$dest/third_party/stb/"

# The kit's host (Linux/WSL) system-services shim: sys::log, monotonic_us,
# sleep_us, park, quit. We build our own host snapshot runner on top of it.
cp -a -- "$kit/host/platform_host.cpp" "$dest/host/platform_host.cpp"

# Re-root the kit's own includes. This must be ONE substitution: six chained
# s/// rules would rewrite the "ui/" the earlier rule just inserted and produce
# "ui/kit/ui/kit/gfx/...". A single alternation advances past each replacement,
# so the inserted prefix is never re-matched. perl -pi keeps it portable across
# BSD and GNU sed; these six are the only root-anchored prefixes the subset uses.
find "$dest" -type f \( -name '*.cpp' -o -name '*.c' -o -name '*.hpp' -o -name '*.h' -o -name '*.inc' \) -print0 |
    xargs -0 perl -pi -e 's{#include "(gfx|ui|core|audio|platform|third_party)/}{#include "ui/kit/$1/}g;'

printf '%s\n' "vendor-kit.sh: ui/kit/ <- ps5-homebrew-ui@$revision"
