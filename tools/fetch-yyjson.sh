#!/usr/bin/env bash
# Accord - pinned yyjson download for core/json.
#
# The daemon and the title both parse Discord JSON, and core/json.cpp is built
# by both, so the parser has to be a plain C library that returns errors rather
# than throwing them: yyjson (MIT) is the choice, justified in
# reports/task-3.2.md. This script fetches the tagged release below into
# third_party/yyjson and leaves only what is needed to build and to attribute:
# src/yyjson.c, src/yyjson.h and the licence. It is a build input, so
# third_party/yyjson is not committed.
#
#   tools/fetch-yyjson.sh        # fetch when missing, then verify
#
# The pin is the commit the tag points at, not the tag name: the checkout is
# fetched by that commit and its HEAD is checked against it, so a moved tag
# cannot change what is built.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

version=0.13.0
commit=6447536015f3d600f3d65323b10976103b337ca7
url=https://github.com/ibireme/yyjson.git

scratch="$root/build/yyjson-src"
vendor="$root/third_party/yyjson"

if [[ ! -d $scratch/.git ]]; then
    printf '==> [json] fetching yyjson %s (%s)\n' "$version" "${commit:0:7}" >&2
    rm -rf -- "$scratch"
    git init -q -- "$scratch"
    git -C "$scratch" remote add origin "$url"
    git -C "$scratch" fetch -q --depth 1 origin "$commit"
    git -C "$scratch" checkout -q FETCH_HEAD
fi

actual=$(git -C "$scratch" rev-parse HEAD)
[[ $actual == "$commit" ]] || {
    printf 'yyjson checkout is %s, expected %s\n' "$actual" "$commit" >&2
    exit 2
}

mkdir -p -- "$vendor"
for file in src/yyjson.c src/yyjson.h LICENSE; do
    cmp -s "$scratch/$file" "$vendor/$(basename -- "$file")" ||
        cp -- "$scratch/$file" "$vendor/$(basename -- "$file")"
done

printf '%s\n' "$vendor"
