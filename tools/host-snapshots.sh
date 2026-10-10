#!/usr/bin/env bash
# Accord - render every screen on the host (Mesa surfaceless EGL) to PNG.
#
# Builds the platform-neutral part of the app - the vendored kit, our screens
# and core/ (the login screen shows the product name from core/app_info.hpp) -
# for the PC and runs platform/host/snapshot_main.cpp. The console
# platform layer (ui/kit/platform/) is left out; the kit's host shim stands in
# for it. Runs inside the builder container, which carries Mesa:
#
#   scripts/dev.sh bash tools/host-snapshots.sh [theme id]
#
# Output: build/snapshots/*.png (one per screen).

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cxx="${HOST_CXX:-clang++}"
cc="${HOST_CC:-clang}"
build="$root/build/host-snapshots"
output="$root/build/snapshots"
theme="${1:-acrylic}"

rm -rf -- "$build"
mkdir -p -- "$build/obj" "$output"

# core/json.cpp parses with the pinned parser under third_party/yyjson, which is
# fetched by tools/fetch-yyjson.sh and compiled here as the C file it is.
yyjson="$root/third_party/yyjson/yyjson.c"
[[ -f $yyjson ]] || {
    printf 'missing %s: run bash tools/fetch-yyjson.sh\n' "$yyjson" >&2
    exit 2
}

# ui/kit/host/platform_host.cpp (the kit's sys::log shim) comes in through the
# find below, like the rest of the kit.
sources=(
    "$root/platform/host/snapshot_main.cpp"
    "$root/ui/screens.cpp"
    "$yyjson"
)
while IFS= read -r -d '' file; do
    sources+=("$file")
done < <(find "$root/core" "$root/ui/screens" "$root/ui/kit" -type f \( -name '*.cpp' -o -name '*.c' \) \
    ! -path "$root/ui/kit/platform/*" ! -path "$root/core/log.cpp" -print0 | sort -z)

objects=()
for source in "${sources[@]}"; do
    relative="${source#"$root/"}"
    object="$build/obj/${relative//\//_}.o"
    if [[ $source == *.c ]]; then
        "$cc" -std=c11 -O2 -w -I"$root" -c "$source" -o "$object"
    elif [[ $source == "$root/ui/kit/"* || $source == "$root/platform/host/"* ]]; then
        # The vendored kit and the host shim are third-party: keep our warnings,
        # not theirs.
        "$cxx" -std=c++20 -O2 -w -DGL_GLEXT_PROTOTYPES=1 -I"$root" -c "$source" -o "$object"
    else
        "$cxx" -std=c++20 -O2 -Wall -Wextra -Wpedantic -DGL_GLEXT_PROTOTYPES=1 -I"$root" \
            -c "$source" -o "$object"
    fi
    objects+=("$object")
done

"$cxx" "${objects[@]}" -lEGL -lGL -lm -o "$build/hui_snapshots"

EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    "$build/hui_snapshots" "$root/third_party/ps5-homebrew-ui/assets" "$output" "$theme"

printf '%s\n' "==> [host-snapshots] $(find "$output" -name '*.png' | wc -l | tr -d ' ') PNGs in build/snapshots"
