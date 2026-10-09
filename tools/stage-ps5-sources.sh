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

set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
staging="$root/third_party/ps5-native-app-boilerplate/.local/discord-ps5"

rm -rf -- "$staging"
mkdir -p -- "$staging/src" "$staging/sce_sys"

cp -a -- "$root/core" "$staging/src/core"
mkdir -p -- "$staging/src/platform"
cp -a -- "$root/platform/ps5" "$staging/src/platform/ps5"
cp -a -- "$root/ui" "$staging/src/ui"

cp -a -- "$root/sce_sys/param.json" "$staging/sce_sys/param.json"

printf '%s\n' '==> [stage] core/ platform/ps5/ ui/ + sce_sys/param.json -> boilerplate/.local/discord-ps5'
