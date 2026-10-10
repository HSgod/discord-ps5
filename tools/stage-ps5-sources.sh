#!/usr/bin/env bash
# Accord - stage this project's PS5 sources into the boilerplate harness.
#
# The boilerplate builds from a single source directory, while our code lives in
# core/, platform/ps5/ and ui/. Those three are copied into its gitignored
# .local/ tree, mirroring the layout so that root-relative includes such as
# "core/log.hpp" keep working.
#
# platform/host/ is deliberately NOT staged: it is a host substitute and must
# never reach the console. For the same reason ui/kit/host/ stays behind: it
# defines the kit's sys:: seam for the PC (monotonic_us, log, park, quit) and
# would collide with ui/kit/platform/ps5/system.cpp, which defines the same
# symbols for the console.
#
# platform/ps5/dave/ belongs to the payload build alone. It includes
# <dave/dave.h> from the cross-built prefix under build/, and it needs
# exceptions and RTTI, while the harness compiles the title with
# -fno-exceptions -fno-rtti. Staging it broke the title build with
# "dave/dave.h file not found". The payload reads its sources straight from
# this tree, so nothing here depends on that directory.

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
staging="$root/third_party/ps5-native-app-boilerplate/.local/accord"

rm -rf -- "$staging"
mkdir -p -- "$staging/src" "$staging/sce_sys" "$staging/assets"

cp -a -- "$root/core" "$staging/src/core"
mkdir -p -- "$staging/src/platform"
cp -a -- "$root/platform/ps5" "$staging/src/platform/ps5"
cp -a -- "$root/ui" "$staging/src/ui"
rm -rf -- "$staging/src/platform/ps5/dave" "$staging/src/ui/kit/host"

cp -a -- "$root/sce_sys/param.json" "$staging/sce_sys/param.json"

# core/json.cpp reads the pinned parser as "third_party/yyjson/yyjson.h", which
# is the path the host tests and the daemon resolve from the repository root.
# Staging the same pair under src/ keeps that spelling working here too, and the
# harness compiles the staged .c along with our own.
json_src="$root/third_party/yyjson"
[[ -f $json_src/yyjson.c && -f $json_src/yyjson.h ]] || {
    printf 'missing %s: run tools/fetch-yyjson.sh\n' "$json_src" >&2
    exit 2
}
mkdir -p -- "$staging/src/third_party/yyjson"
cp -a -- "$json_src/yyjson.c" "$json_src/yyjson.h" "$staging/src/third_party/yyjson/"

# The kit draws its text with SDF atlases it loads from the application's own
# assets, so the six .huifont files (with their licences) are staged from the
# vendored kit into assets/fonts, which the harness copies into the .ffpkg as
# /app0/assets. They are third-party binaries: staged, not committed here.
kit_assets="$root/third_party/ps5-homebrew-ui/assets/fonts"
[[ -d $kit_assets ]] || {
    printf 'missing %s: run git submodule update --init third_party/ps5-homebrew-ui\n' \
        "$kit_assets" >&2
    exit 2
}
cp -a -- "$kit_assets" "$staging/assets/fonts"

printf '%s\n' '==> [stage] core/ platform/ps5/ ui/ + sce_sys/param.json + assets/fonts + third_party/yyjson -> boilerplate/.local/accord'
