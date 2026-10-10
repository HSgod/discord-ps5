#!/usr/bin/env bash
# Accord - pinned ps5-opengl SDK download.
#
# The kit in ui/kit/ draws with OpenGL, and on the PS5 that goes through
# ps5-opengl: a Mesa build for the console, published as a versioned release.
# This script downloads the pinned release into third_party/ps5-opengl, checks
# the archive and the SDK manifest against the hashes below, and prints the SDK
# prefix (the directory holding manifest.sha256).
#
#   tools/fetch-opengl-sdk.sh          # download when missing, then verify
#
# Set PS5_OPENGL_PREFIX to an SDK already on disk; it is verified all the same.
# The SDK is a build input: third_party/ps5-opengl is not committed.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

version=1.0.0
archive_sha256=f93643c04c843d56143b00951df1f8042ea7706ae9f19e4e9abf158e1ead77c5
manifest_sha256=f4b91f672be037fbac3f82494f1225deaf4c227a03f37ac3ffa56abb213b943f
url="https://github.com/blackbearreloaded/ps5-opengl/releases/download/v$version/ps5-opengl-sdk-$version.tar.gz"

cache="$root/third_party/ps5-opengl"
archive="$cache/ps5-opengl-sdk-$version.tar.gz"
prefix="$cache/sdk"

# macOS ships shasum, the builder container ships sha256sum.
sha256_of()
{
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum -- "$1" | cut -d' ' -f1
    else
        shasum -a 256 -- "$1" | cut -d' ' -f1
    fi
}

verify_manifest()
{
    local directory=$1 line expected path actual mismatched=0
    [[ -f $directory/manifest.sha256 ]] || {
        printf 'ps5-opengl SDK has no manifest: %s\n' "$directory" >&2
        exit 2
    }
    [[ $(sha256_of "$directory/manifest.sha256") == "$manifest_sha256" ]] || {
        printf 'unexpected ps5-opengl SDK manifest in %s\n' "$directory" >&2
        exit 2
    }
    # Verified file by file rather than with --check: --strict is GNU-only.
    while read -r expected path; do
        [[ -n ${path:-} ]] || continue
        actual=$(sha256_of "$directory/$path")
        if [[ $actual != "$expected" ]]; then
            printf 'ps5-opengl SDK file does not match the manifest: %s\n' "$path" >&2
            mismatched=1
        fi
    done < "$directory/manifest.sha256"
    (( mismatched == 0 )) || exit 2
}

if [[ -n ${PS5_OPENGL_PREFIX:-} ]]; then
    prefix=$(cd -- "$PS5_OPENGL_PREFIX" && pwd)
    verify_manifest "$prefix"
    printf '%s\n' "$prefix"
    exit 0
fi

mkdir -p -- "$cache"

if [[ ! -f $prefix/manifest.sha256 ]]; then
    if [[ ! -f $archive || $(sha256_of "$archive") != "$archive_sha256" ]]; then
        printf '==> [opengl] downloading ps5-opengl SDK %s\n' "$version" >&2
        curl -fL --retry 3 -o "$archive.part" "$url"
        mv -- "$archive.part" "$archive"
    fi
    [[ $(sha256_of "$archive") == "$archive_sha256" ]] || {
        printf 'ps5-opengl SDK archive checksum mismatch: %s\n' "$archive" >&2
        exit 2
    }
    rm -rf -- "$prefix"
    # The build needs the compiled SDK and its headers; the release's top-level
    # licence texts are kept next to it. Nothing else is unpacked.
    tar -xzf "$archive" -C "$cache" --strip-components=1 \
        "ps5-opengl-sdk-$version/sdk" \
        "ps5-opengl-sdk-$version/LICENSE" \
        "ps5-opengl-sdk-$version/LICENSES" \
        "ps5-opengl-sdk-$version/THIRD_PARTY_NOTICES.md"
    rm -f -- "$archive"
fi

verify_manifest "$prefix"
printf '%s\n' "$prefix"
