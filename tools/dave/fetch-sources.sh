#!/usr/bin/env bash
# T6.0-2: fetch the pinned sources of the DAVE dependency chain into
# build/dave/src. Runs inside the builder container; re-running is a no-op.
#
# Pins: OpenSSL 3.5.2 tarball verified against the SHA-256 that OpenSSL itself
# publishes; nlohmann/json at release tag v3.11.3 (the release mlspp's manifest
# names, not HEAD); mlspp and libdave by commit SHA, both recorded by T6.0-1.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
src="$root/build/dave/src"
mkdir -p "$src"

# --- OpenSSL: tarball plus the hash OpenSSL itself publishes -----------------
if [ ! -d "$src/openssl-3.5.2" ]; then
    cd "$src"
    if [ ! -f openssl-3.5.2.tar.gz ]; then
        curl -fsSL -o openssl-3.5.2.tar.gz \
            https://github.com/openssl/openssl/releases/download/openssl-3.5.2/openssl-3.5.2.tar.gz
        curl -fsSL -o openssl-3.5.2.tar.gz.sha256 \
            https://github.com/openssl/openssl/releases/download/openssl-3.5.2/openssl-3.5.2.tar.gz.sha256
    fi
    echo "--- weryfikacja sha256 OpenSSL ---"
    cat openssl-3.5.2.tar.gz.sha256
    sha256sum -c openssl-3.5.2.tar.gz.sha256
    tar xzf openssl-3.5.2.tar.gz
fi

# --- Repozytoria przypięte commitem -----------------------------------------
fetch_commit() {
    local name=$1 url=$2 sha=$3
    if [ ! -d "$src/$name/.git" ]; then
        rm -rf "$src/$name"
        git init -q "$src/$name"
        git -C "$src/$name" remote add origin "$url"
        git -C "$src/$name" fetch -q --depth 1 origin "$sha"
        git -C "$src/$name" checkout -q FETCH_HEAD
    fi
    printf '%-12s %s\n' "$name" "$(git -C "$src/$name" rev-parse HEAD)"
}

fetch_commit mlspp    https://github.com/cisco/mlspp.git     fc724c3100ce3b5d8565dbd6d93648a440991a8c
fetch_commit libdave  https://github.com/discord/libdave.git 8de72b1f8a2ac3c5a5270755bb8091a62e3c6169

# --- nlohmann/json: wydanie, nie HEAD ---------------------------------------
if [ ! -d "$src/nlohmann-json/.git" ]; then
    rm -rf "$src/nlohmann-json"
    git clone -q --depth 1 --branch v3.11.3 \
        https://github.com/nlohmann/json.git "$src/nlohmann-json"
fi
printf '%-12s %s (tag v3.11.3)\n' nlohmann "$(git -C "$src/nlohmann-json" rev-parse HEAD)"

echo "--- gotowe: $(ls "$src" | tr '\n' ' ')"
