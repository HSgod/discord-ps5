#!/usr/bin/env bash
# Discord PS5 - stage this project's PS5 sources into the boilerplate harness.
#
# The boilerplate builds from a single source directory, while our code lives in
# core/, platform/ps5/ and ui/. Those three are copied into its gitignored
# .local/ tree, mirroring the layout so that root-relative includes such as
# "core/log.hpp" keep working.
#
# platform/host/ is deliberately NOT staged: it is a host substitute and must
# never reach the console.
#
# ui/kit/ and the screens that draw on it are left out of the console build for
# now. The kit renders through ps5-opengl, and the boilerplate harness ships no
# GL headers, no link group and no SceAgc import stubs, so pulling the kit in
# means wiring all three and moving the entry point off demo_renderer. That
# change belongs with the console run it needs, not with the host preview.
# Until then the console app keeps drawing its own screen, and
# tools/host-snapshots.sh builds the kit on the host alone.

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
staging="$root/third_party/ps5-native-app-boilerplate/.local/discord-ps5"

rm -rf -- "$staging"
mkdir -p -- "$staging/src" "$staging/sce_sys"

cp -a -- "$root/core" "$staging/src/core"
mkdir -p -- "$staging/src/platform"
cp -a -- "$root/platform/ps5" "$staging/src/platform/ps5"
cp -a -- "$root/ui" "$staging/src/ui"
rm -rf -- "$staging/src/ui/kit" "$staging/src/ui/screens" \
    "$staging/src/ui/screens.cpp" "$staging/src/ui/screens.hpp"

cp -a -- "$root/sce_sys/param.json" "$staging/sce_sys/param.json"

printf '%s\n' '==> [stage] core/ platform/ps5/ ui/ + sce_sys/param.json -> boilerplate/.local/discord-ps5'
